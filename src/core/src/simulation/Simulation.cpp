#include <core/simulation/Simulation.h>

#include <core/formula/Analysis.h>
#include <core/formula/Evaluation.h>
#include <core/formula/Function.h>

#include <QSet>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <utility>

namespace qds::core {
namespace {

void addIssue(SimulationCompileResult &result, EntityId id, const QString &message, const QString &code,
    SourceSpan span = {})
{
    result.issues.push_back({id, IssueSeverity::Error, message, code, span});
}

bool supportedCall(const CallNode &call)
{
    const FunctionSignature *signature = findFunctionSignature(call.nameKey());
    if (!signature)
        return true;
    if (signature->category == FunctionCategory::Mathematical || signature->category == FunctionCategory::Time
        || signature->category == FunctionCategory::Array)
        return true;
    return call.nameKey() == "log" || call.nameKey() == "if_then_else";
}

void scanSupportedCalls(const Document &document, const FormulaNode *node, EntityId owner,
    SimulationCompileResult &result, QSet<QString> &visitedMacros)
{
    if (!node)
        return;
    if (const auto *reference = dynamic_cast<const ReferenceNode *>(node)) {
        if (reference->nameKey() == "self")
            addIssue(result, owner, "SELF is not supported by the scalar simulator", "unsupported-simulation-function",
                reference->span());
        return;
    }
    if (const auto *call = dynamic_cast<const CallNode *>(node)) {
        if (!supportedCall(*call)) {
            addIssue(result, owner, "Function is not supported by the scalar simulator: " + call->nameKey(),
                "unsupported-simulation-function", call->span());
        } else if (!findFunctionSignature(call->nameKey())) {
            const MacroDefinition *macro = document.findMacro(call->nameKey(), int(call->argumentCount()), 1);
            const QString key = call->nameKey() + "/" + QString::number(call->argumentCount());
            if (macro && !visitedMacros.contains(key)) {
                visitedMacros.insert(key);
                scanSupportedCalls(document, macro->formula().root(), owner, result, visitedMacros);
                for (int index = int(call->argumentCount()); index < macro->parameters().size(); ++index) {
                    scanSupportedCalls(document, macro->parameters()[index].defaultFormula().root(), owner, result,
                        visitedMacros);
                }
            }
        }
        for (std::size_t index = 0; index < call->argumentCount(); ++index)
            scanSupportedCalls(document, call->argument(index), owner, result, visitedMacros);
        return;
    }
    if (const auto *unary = dynamic_cast<const UnaryNode *>(node)) {
        scanSupportedCalls(document, unary->operand(), owner, result, visitedMacros);
        return;
    }
    if (const auto *binary = dynamic_cast<const BinaryNode *>(node)) {
        scanSupportedCalls(document, binary->left(), owner, result, visitedMacros);
        scanSupportedCalls(document, binary->right(), owner, result, visitedMacros);
        return;
    }
    if (const auto *conditional = dynamic_cast<const ConditionalNode *>(node)) {
        scanSupportedCalls(document, conditional->condition(), owner, result, visitedMacros);
        scanSupportedCalls(document, conditional->thenExpression(), owner, result, visitedMacros);
        scanSupportedCalls(document, conditional->elseExpression(), owner, result, visitedMacros);
        return;
    }
    if (const auto *subscript = dynamic_cast<const SubscriptNode *>(node)) {
        scanSupportedCalls(document, subscript->target(), owner, result, visitedMacros);
        for (std::size_t index = 0; index < subscript->indexCount(); ++index)
            scanSupportedCalls(document, subscript->index(index), owner, result, visitedMacros);
        return;
    }
    if (const auto *range = dynamic_cast<const RangeNode *>(node)) {
        scanSupportedCalls(document, range->begin(), owner, result, visitedMacros);
        scanSupportedCalls(document, range->end(), owner, result, visitedMacros);
        return;
    }
    if (const auto *literal = dynamic_cast<const ArrayLiteralNode *>(node)) {
        for (std::size_t index = 0; index < literal->valueCount(); ++index)
            scanSupportedCalls(document, literal->value(index), owner, result, visitedMacros);
    }
}

void scanSupportedCalls(const Document &document, const Formula &formula, EntityId owner,
    SimulationCompileResult &result)
{
    QSet<QString> visitedMacros;
    scanSupportedCalls(document, formula.root(), owner, result, visitedMacros);
}

QVector<EntityId> sortedIds(const QSet<EntityId> &ids)
{
    QVector<EntityId> result(ids.begin(), ids.end());
    std::sort(result.begin(), result.end());
    return result;
}

QVector<EntityId> dependencies(const AnalysisReport &analysis, EntityId id)
{
    return sortedIds(analysis.references(id));
}

QVector<EntityId> topologicalOrder(const QVector<EntityId> &roots, const Document &document,
    const AnalysisReport &analysis, bool initialization, SimulationCompileResult &result)
{
    QVector<EntityId> order;
    QHash<EntityId, int> state;
    std::function<void(EntityId)> visit = [&](EntityId id) {
        if (state.value(id) == 2)
            return;
        if (state.value(id) == 1)
            return;
        state[id] = 1;
        for (EntityId dependency : dependencies(analysis, id)) {
            const DocumentElement *element = document.find(dependency);
            const bool currentState = element && (element->kind() == ElementKind::Stock
                || element->kind() == ElementKind::Constant);
            if (!initialization && currentState)
                continue;
            if (state.value(dependency) == 1) {
                addIssue(result, id,
                    initialization ? "Simultaneous initial equations" : "Simultaneous equations",
                    initialization ? "simultaneous-initial-equations" : "simultaneous-equations");
                continue;
            }
            visit(dependency);
        }
        state[id] = 2;
        order.push_back(id);
    };
    for (EntityId id : roots)
        visit(id);
    return order;
}

} // namespace

SimulationCompileResult compileSimulation(const Document &document)
{
    SimulationCompileResult result;
    const SimulationSettings &settings = document.simulationSettings();
    if (!settings.isValid())
        addIssue(result, 0, "Simulation settings are invalid", "invalid-simulation-settings");
    const AnalysisReport analysis = analyzeDocument(document);
    for (const ModelIssue &issue : analysis.issues()) {
        if (issue.severity == IssueSeverity::Error && issue.code != "algebraic-cycle")
            result.issues.push_back(issue);
    }

    QVector<EntityId> constants;
    QVector<EntityId> stocks;
    QVector<EntityId> runtimeRoots;
    QVector<EntityId> outputs;
    for (const ModelElement &element : document.model().elements()) {
        const EntityId id = element.id();
        if (element.kind() == ElementKind::Data) {
            addIssue(result, id, "External data is not supported by the scalar simulator", "unsupported-simulation-data");
            continue;
        }
        if (!element.dimensions().isEmpty() || !element.elementEquations().empty()) {
            addIssue(result, id, "Array simulation is not implemented", "unsupported-simulation-array");
            continue;
        }
        const Formula &definition = element.kind() == ElementKind::Stock
            ? static_cast<const Stock &>(element).initialFormula() : element.formula();
        if (!definition.root()) {
            addIssue(result, id, "Variable has no simulation equation: " + element.displayName(),
                "missing-simulation-equation");
            continue;
        }
        scanSupportedCalls(document, definition, id, result);
        outputs.push_back(id);
        if (element.kind() == ElementKind::Stock)
            stocks.push_back(id);
        else if (element.kind() == ElementKind::Constant)
            constants.push_back(id);
        else
            runtimeRoots.push_back(id);
    }
    std::sort(stocks.begin(), stocks.end());
    std::sort(constants.begin(), constants.end());
    std::sort(runtimeRoots.begin(), runtimeRoots.end());
    std::sort(outputs.begin(), outputs.end());
    if (!result.issues.empty())
        return result;

    SimulationPlan plan;
    plan.m_settings = settings;
    plan.m_outputOrder = outputs;
    plan.m_initializationOrder = topologicalOrder(constants, document, analysis, true, result);
    const QVector<EntityId> stockInitialization = topologicalOrder(stocks, document, analysis, true, result);
    for (EntityId id : stockInitialization) {
        if (!plan.m_initializationOrder.contains(id))
            plan.m_initializationOrder.push_back(id);
    }
    plan.m_evaluationOrder = topologicalOrder(runtimeRoots, document, analysis, false, result);
    for (EntityId stock : stocks) {
        StockFlows binding;
        binding.stock = stock;
        for (const ModelElement &element : document.model().elements()) {
            if (element.kind() != ElementKind::Flow)
                continue;
            const FlowEndpoints endpoints = static_cast<const Flow &>(element).endpoints();
            if (endpoints.to == stock)
                binding.inflows.push_back(element.id());
            if (endpoints.from == stock)
                binding.outflows.push_back(element.id());
        }
        std::sort(binding.inflows.begin(), binding.inflows.end());
        std::sort(binding.outflows.begin(), binding.outflows.end());
        plan.m_stockFlows.push_back(std::move(binding));
    }
    if (result.issues.empty())
        result.plan = std::move(plan);
    return result;
}

const QVector<double> *SimulationRun::series(EntityId id) const
{
    const auto found = m_series.constFind(id);
    return found == m_series.constEnd() ? nullptr : &found.value();
}

SimulationRunner::SimulationRunner(const Document &document, const SimulationPlan &plan)
    : m_document(document)
    , m_plan(plan)
{
}

SimulationRun SimulationRunner::run(const Cancellation &cancelled)
{
    SimulationRun run;
    run.m_settings = m_plan.settings();
    for (EntityId id : m_plan.m_outputOrder)
        run.m_series.insert(id, {});

    QHash<EntityId, double> values;
    double currentTime = m_plan.settings().startTime();
    double evaluationTime = currentTime;
    const auto stopWithIssue = [&](SimulationRunStatus status, EntityId id, QString message, QString code,
                                SourceSpan span = {}) {
        run.m_status = status;
        run.m_endTime = currentTime;
        run.m_issues.push_back({id, status == SimulationRunStatus::Cancelled ? IssueSeverity::Warning : IssueSeverity::Error,
            std::move(message), std::move(code), span});
        return run;
    };
    const auto evaluate = [&](const QVector<EntityId> &order, double time, double dt,
                            QHash<EntityId, double> &target) -> std::optional<ModelIssue> {
        evaluationTime = time;
        EvaluationContext context;
        context.time = time;
        context.dt = dt;
        context.startTime = m_plan.settings().startTime();
        context.stopTime = m_plan.settings().stopTime();
        context.readDimension = [this](const QString &name) { return m_document.findDimension(name); };
        context.readMacro = makeMacroReader(m_document);
        context.readVariable = [this, &target](const QString &name) -> std::optional<Value> {
            const ModelElement *element = m_document.model().findByName(name);
            if (!element || !target.contains(element->id()))
                return std::nullopt;
            return target.value(element->id());
        };
        for (EntityId id : order) {
            const auto *element = dynamic_cast<const ModelElement *>(m_document.find(id));
            if (!element)
                return ModelIssue {id, IssueSeverity::Error, "Simulation variable no longer exists", "missing-simulation-variable", {}};
            const Formula &formula = element->kind() == ElementKind::Stock
                ? static_cast<const Stock *>(element)->initialFormula() : element->formula();
            const EvaluationResult evaluated = evaluateFormula(formula.root(), context);
            if (!evaluated.value)
                return ModelIssue {id, IssueSeverity::Error, evaluated.error, "simulation-evaluation", evaluated.errorSpan};
            const auto *scalar = std::get_if<double>(&*evaluated.value);
            if (!scalar)
                return ModelIssue {id, IssueSeverity::Error, "Simulation formula did not produce a scalar", "non-scalar-simulation-value", {}};
            if (!std::isfinite(*scalar))
                return ModelIssue {id, IssueSeverity::Error, "Simulation formula produced a non-finite value", "non-finite-simulation-value", {}};
            target[id] = *scalar;
        }
        return std::nullopt;
    };
    const auto failEvaluation = [&](const ModelIssue &issue) {
        run.m_status = SimulationRunStatus::Failed;
        run.m_endTime = evaluationTime;
        run.m_issues.push_back(issue);
        return run;
    };

    if (cancelled && cancelled())
        return stopWithIssue(SimulationRunStatus::Cancelled, 0, "Simulation cancelled", "simulation-cancelled");
    if (const auto issue = evaluate(m_plan.m_initializationOrder, currentTime, m_plan.settings().dt(), values))
        return failEvaluation(*issue);

    const auto netRates = [this](const QHash<EntityId, double> &state) {
        QHash<EntityId, double> rates;
        for (const StockFlows &binding : m_plan.m_stockFlows) {
            double rate = 0.0;
            for (EntityId flow : binding.inflows)
                rate += state.value(flow);
            for (EntityId flow : binding.outflows)
                rate -= state.value(flow);
            rates.insert(binding.stock, rate);
        }
        return rates;
    };

    double nextSave = m_plan.settings().startTime() + m_plan.settings().saveInterval();
    bool firstPoint = true;
    quint64 stepIndex = 0;
    const double timeTolerance = std::numeric_limits<double>::epsilon()
        * std::max(1.0, std::abs(m_plan.settings().stopTime())) * 16.0;
    while (true) {
        if (cancelled && cancelled())
            return stopWithIssue(SimulationRunStatus::Cancelled, 0, "Simulation cancelled", "simulation-cancelled");
        if (m_plan.settings().stopTime() - currentTime <= timeTolerance)
            currentTime = m_plan.settings().stopTime();
        const double remaining = m_plan.settings().stopTime() - currentTime;
        const double dt = remaining > 0.0 ? std::min(m_plan.settings().dt(), remaining) : m_plan.settings().dt();
        if (const auto issue = evaluate(m_plan.m_evaluationOrder, currentTime, dt, values))
            return failEvaluation(*issue);

        if (firstPoint || currentTime + timeTolerance >= nextSave || remaining <= 0.0) {
            if (run.m_times.isEmpty() || run.m_times.back() != currentTime) {
                run.m_times.push_back(currentTime);
                for (EntityId id : m_plan.m_outputOrder)
                    run.m_series[id].push_back(values.value(id));
            }
            firstPoint = false;
            while (nextSave <= currentTime + timeTolerance)
                nextSave += m_plan.settings().saveInterval();
        }
        if (remaining <= 0.0)
            break;

        QHash<EntityId, double> nextValues = values;
        if (m_plan.settings().integrationMethod() == IntegrationMethod::Euler) {
            const QHash<EntityId, double> rates = netRates(values);
            for (const StockFlows &binding : m_plan.m_stockFlows)
                nextValues[binding.stock] = values.value(binding.stock) + dt * rates.value(binding.stock);
        } else {
            const QHash<EntityId, double> k1 = netRates(values);
            QHash<EntityId, double> stage = values;
            for (const StockFlows &binding : m_plan.m_stockFlows)
                stage[binding.stock] = values.value(binding.stock) + dt * k1.value(binding.stock) / 2.0;
            if (const auto issue = evaluate(m_plan.m_evaluationOrder, currentTime + dt / 2.0, dt, stage))
                return failEvaluation(*issue);
            const QHash<EntityId, double> k2 = netRates(stage);

            stage = values;
            for (const StockFlows &binding : m_plan.m_stockFlows)
                stage[binding.stock] = values.value(binding.stock) + dt * k2.value(binding.stock) / 2.0;
            if (const auto issue = evaluate(m_plan.m_evaluationOrder, currentTime + dt / 2.0, dt, stage))
                return failEvaluation(*issue);
            const QHash<EntityId, double> k3 = netRates(stage);

            stage = values;
            for (const StockFlows &binding : m_plan.m_stockFlows)
                stage[binding.stock] = values.value(binding.stock) + dt * k3.value(binding.stock);
            if (const auto issue = evaluate(m_plan.m_evaluationOrder, currentTime + dt, dt, stage))
                return failEvaluation(*issue);
            const QHash<EntityId, double> k4 = netRates(stage);

            for (const StockFlows &binding : m_plan.m_stockFlows) {
                nextValues[binding.stock] = values.value(binding.stock) + dt
                    * (k1.value(binding.stock) + 2.0 * k2.value(binding.stock)
                        + 2.0 * k3.value(binding.stock) + k4.value(binding.stock)) / 6.0;
            }
        }
        for (const StockFlows &binding : m_plan.m_stockFlows) {
            if (!std::isfinite(nextValues.value(binding.stock)))
                return stopWithIssue(SimulationRunStatus::Failed, binding.stock,
                    "Stock integration produced a non-finite value", "non-finite-simulation-value");
        }
        values = std::move(nextValues);
        const double previousTime = currentTime;
        ++stepIndex;
        currentTime = std::min(m_plan.settings().stopTime(),
            m_plan.settings().startTime() + static_cast<double>(stepIndex) * m_plan.settings().dt());
        if (m_plan.settings().stopTime() - currentTime <= timeTolerance)
            currentTime = m_plan.settings().stopTime();
        if (dt <= 0.0 || currentTime == previousTime) {
            return stopWithIssue(SimulationRunStatus::Failed, 0, "Simulation time did not advance", "simulation-time-stalled");
        }
    }
    run.m_status = SimulationRunStatus::Completed;
    run.m_endTime = currentTime;
    return run;
}

} // namespace qds::core
