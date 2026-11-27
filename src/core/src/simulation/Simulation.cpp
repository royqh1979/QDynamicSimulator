#include <core/simulation/Simulation.h>

#include <core/formula/Analysis.h>
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
            addIssue(result, owner, "SELF is not supported by the simulator", "unsupported-simulation-function",
                reference->span());
        return;
    }
    if (const auto *call = dynamic_cast<const CallNode *>(node)) {
        if (!supportedCall(*call)) {
            addIssue(result, owner, "Function is not supported by the simulator: " + call->nameKey(),
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
            const bool currentState = element && element->kind() == ElementKind::Stock;
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

std::size_t elementCount(const QVector<std::size_t> &shape)
{
    std::size_t count = 1;
    for (const std::size_t size : shape) {
        if (size == 0 || count > std::numeric_limits<std::size_t>::max() / size)
            return 0;
        count *= size;
    }
    return count;
}

QVector<std::size_t> strides(const QVector<std::size_t> &shape)
{
    QVector<std::size_t> result(shape.size(), 1);
    for (std::size_t index = shape.size(); index-- > 1;)
        result[index - 1] = result[index] * shape[index];
    return result;
}

QVector<std::size_t> coordinates(std::size_t flat, const QVector<std::size_t> &shape)
{
    const QVector<std::size_t> steps = strides(shape);
    QVector<std::size_t> result(shape.size());
    for (std::size_t index = 0; index < shape.size(); ++index) {
        result[index] = flat / steps[index];
        flat %= steps[index];
    }
    return result;
}

std::optional<std::size_t> flatIndex(const ArrayValue &array, const QVector<QString> &dimensions,
    const QVector<std::size_t> &indices)
{
    if (array.dimensions.size() != dimensions.size() || array.shape.size() != dimensions.size()
        || indices.size() != dimensions.size() || elementCount(array.shape) != array.values.size())
        return std::nullopt;
    const QVector<std::size_t> steps = strides(array.shape);
    std::size_t flat = 0;
    for (std::size_t position = 0; position < dimensions.size(); ++position) {
        const auto found = std::find(array.dimensions.begin(), array.dimensions.end(), dimensions[position]);
        if (found == array.dimensions.end())
            return std::nullopt;
        const std::size_t dimensionIndex = static_cast<std::size_t>(found - array.dimensions.begin());
        if (indices[position] >= array.shape[dimensionIndex])
            return std::nullopt;
        flat += indices[position] * steps[dimensionIndex];
    }
    return flat;
}

bool finiteValue(const Value &value)
{
    if (const auto *scalar = std::get_if<double>(&value))
        return std::isfinite(*scalar);
    const ArrayValue &array = std::get<ArrayValue>(value);
    return std::all_of(array.values.begin(), array.values.end(), [](double item) { return std::isfinite(item); });
}

std::optional<double> evaluatedScalar(const EvaluationResult &evaluated, const QVector<QString> &dimensions,
    const QVector<std::size_t> &indices, EntityId owner, ModelIssue &issue)
{
    if (!evaluated.value) {
        issue = {owner, IssueSeverity::Error, evaluated.error, "simulation-evaluation", evaluated.errorSpan};
        return std::nullopt;
    }
    if (const auto *scalar = std::get_if<double>(&*evaluated.value))
        return *scalar;
    const ArrayValue &array = std::get<ArrayValue>(*evaluated.value);
    const auto flat = flatIndex(array, dimensions, indices);
    if (!flat) {
        issue = {owner, IssueSeverity::Error, "Array value has incompatible dimensions", "incompatible-array-value", {}};
        return std::nullopt;
    }
    return array.values[*flat];
}

Value integratedValue(const Value &stock, const Value &rate, double factor)
{
    if (const auto *scalar = std::get_if<double>(&stock))
        return *scalar + factor * std::get<double>(rate);
    const ArrayValue &current = std::get<ArrayValue>(stock);
    const ArrayValue &change = std::get<ArrayValue>(rate);
    ArrayValue result = current;
    for (std::size_t index = 0; index < result.values.size(); ++index)
        result.values[index] += factor * change.values[index];
    return result;
}

Value weightedRate(const Value &stock, const Value &k1, const Value &k2, const Value &k3, const Value &k4, double dt)
{
    if (std::get_if<double>(&stock))
        return dt * (std::get<double>(k1) + 2.0 * std::get<double>(k2)
            + 2.0 * std::get<double>(k3) + std::get<double>(k4)) / 6.0;
    ArrayValue result = std::get<ArrayValue>(k1);
    const ArrayValue &second = std::get<ArrayValue>(k2);
    const ArrayValue &third = std::get<ArrayValue>(k3);
    const ArrayValue &fourth = std::get<ArrayValue>(k4);
    for (std::size_t index = 0; index < result.values.size(); ++index) {
        result.values[index] = dt * (result.values[index] + 2.0 * second.values[index]
            + 2.0 * third.values[index] + fourth.values[index]) / 6.0;
    }
    return result;
}

struct NetRateResult {
    QHash<EntityId, Value> values;
    std::optional<ModelIssue> issue;
};

NetRateResult netRates(const QVector<StockFlows> &bindings, const QHash<EntityId, Value> &state)
{
    NetRateResult result;
    for (const StockFlows &binding : bindings) {
        const Value &stock = state[binding.stock];
        if (const auto *stockArray = std::get_if<ArrayValue>(&stock))
            result.values.insert(binding.stock, ArrayValue {stockArray->dimensions, stockArray->shape,
                QVector<double>(stockArray->values.size(), 0.0)});
        else
            result.values.insert(binding.stock, 0.0);
        const auto accumulate = [&](EntityId flowId, bool subtract) -> std::optional<ModelIssue> {
            const Value &flow = state[flowId];
            Value &rate = result.values[binding.stock];
            const double sign = subtract ? -1.0 : 1.0;
            if (auto *scalar = std::get_if<double>(&rate)) {
                const auto *flowScalar = std::get_if<double>(&flow);
                if (!flowScalar)
                    return ModelIssue {binding.stock, IssueSeverity::Error,
                        "Array flow is incompatible with scalar stock", "incompatible-stock-flow", {}};
                *scalar += sign * *flowScalar;
                return std::nullopt;
            }
            ArrayValue &rateArray = std::get<ArrayValue>(rate);
            if (const auto *flowScalar = std::get_if<double>(&flow)) {
                for (double &item : rateArray.values)
                    item += sign * *flowScalar;
                return std::nullopt;
            }
            const ArrayValue &flowArray = std::get<ArrayValue>(flow);
            for (std::size_t flat = 0; flat < rateArray.values.size(); ++flat) {
                const auto flowIndex = flatIndex(flowArray, rateArray.dimensions, coordinates(flat, rateArray.shape));
                if (!flowIndex)
                    return ModelIssue {binding.stock, IssueSeverity::Error,
                        "Flow dimensions are incompatible with stock", "incompatible-stock-flow", {}};
                rateArray.values[flat] += sign * flowArray.values[*flowIndex];
            }
            return std::nullopt;
        };
        for (const EntityId flow : binding.inflows) {
            if (const auto issue = accumulate(flow, false)) {
                result.issue = *issue;
                return result;
            }
        }
        for (const EntityId flow : binding.outflows) {
            if (const auto issue = accumulate(flow, true)) {
                result.issue = *issue;
                return result;
            }
        }
    }
    return result;
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

    QVector<EntityId> initializationRoots;
    QVector<EntityId> stocks;
    QVector<EntityId> runtimeRoots;
    QVector<EntityId> outputs;
    for (const ModelElement &element : document.model().elements()) {
        const EntityId id = element.id();
        const Formula &definition = element.kind() == ElementKind::Stock
            ? static_cast<const Stock &>(element).initialFormula() : element.formula();
        if (!definition.root() && element.elementEquations().empty()) {
            addIssue(result, id, "Variable has no simulation equation: " + element.displayName(),
                "missing-simulation-equation");
            continue;
        }
        if (definition.root())
            scanSupportedCalls(document, definition, id, result);
        for (const ArrayElementFormula &entry : element.elementEquations())
            scanSupportedCalls(document, entry.formula(), id, result);
        outputs.push_back(id);
        if (element.kind() == ElementKind::Stock) {
            stocks.push_back(id);
        } else {
            initializationRoots.push_back(id);
            runtimeRoots.push_back(id);
        }
    }
    std::sort(stocks.begin(), stocks.end());
    std::sort(initializationRoots.begin(), initializationRoots.end());
    std::sort(runtimeRoots.begin(), runtimeRoots.end());
    std::sort(outputs.begin(), outputs.end());
    if (!result.issues.empty())
        return result;

    SimulationPlan plan;
    plan.m_settings = settings;
    plan.m_outputOrder = outputs;
    plan.m_initializationOrder = topologicalOrder(initializationRoots, document, analysis, true, result);
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

RunVariableInfo::RunVariableInfo(EntityId id, QString displayName, QString nameKey, QString unit, ElementKind kind, bool constant)
    : m_id(id)
    , m_displayName(std::move(displayName))
    , m_nameKey(std::move(nameKey))
    , m_unit(std::move(unit))
    , m_kind(kind)
    , m_constant(constant)
{
}

SimulationRun SimulationRun::withMetadata(SimulationRunMetadata metadata) const
{
    SimulationRun copy = *this;
    copy.m_id = metadata.id;
    copy.m_name = std::move(metadata.name);
    copy.m_modelRevision = metadata.modelRevision;
    copy.m_variables = std::move(metadata.variables);
    copy.m_causes = std::move(metadata.causes);
    return copy;
}

const RunVariableInfo *SimulationRun::variable(const QString &nameKey) const
{
    const auto found = std::find_if(m_variables.begin(), m_variables.end(), [&nameKey](const RunVariableInfo &variable) {
        return variable.nameKey() == nameKey;
    });
    return found == m_variables.end() ? nullptr : &*found;
}

const RunVariableInfo *SimulationRun::variable(EntityId id) const
{
    const auto found = std::find_if(m_variables.begin(), m_variables.end(), [id](const RunVariableInfo &variable) {
        return variable.id() == id;
    });
    return found == m_variables.end() ? nullptr : &*found;
}

QVector<QString> SimulationRun::causes(const QString &nameKey) const
{
    const RunVariableInfo *dependent = variable(nameKey);
    QVector<QString> result;
    if (!dependent)
        return result;
    for (const EntityId id : causes(dependent->id())) {
        if (const RunVariableInfo *cause = variable(id))
            result.push_back(cause->nameKey());
    }
    return result;
}

const QVector<double> *SimulationRun::series(const QString &nameKey) const
{
    const RunVariableInfo *match = variable(nameKey);
    return match ? series(match->id()) : nullptr;
}

const QVector<ArrayValue> *SimulationRun::arraySeries(const QString &nameKey) const
{
    const RunVariableInfo *match = variable(nameKey);
    return match ? arraySeries(match->id()) : nullptr;
}

const QVector<double> *SimulationRun::series(EntityId id) const
{
    const auto found = m_series.constFind(id);
    return found == m_series.constEnd() ? nullptr : &found.value();
}

const QVector<ArrayValue> *SimulationRun::arraySeries(EntityId id) const
{
    const auto found = m_arraySeries.constFind(id);
    return found == m_arraySeries.constEnd() ? nullptr : &found.value();
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
    for (const EntityId id : m_plan.m_outputOrder) {
        const auto *element = dynamic_cast<const ModelElement *>(m_document.find(id));
        if (element) {
            run.m_variables.push_back(RunVariableInfo(id, element->displayName(), element->nameKey(),
                element->unit(), element->kind(),
                element->kind() == ElementKind::Variable && isConstantFormula(element->equation())));
        }
    }

    QHash<EntityId, Value> values;
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
                            QHash<EntityId, Value> &target) -> std::optional<ModelIssue> {
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
            context.scope = {};
            if (element->dimensions().isEmpty()) {
                const EvaluationResult evaluated = evaluateFormula(formula.root(), context);
                if (!evaluated.value)
                    return ModelIssue {id, IssueSeverity::Error, evaluated.error, "simulation-evaluation", evaluated.errorSpan};
                const auto *scalar = std::get_if<double>(&*evaluated.value);
                if (!scalar)
                    return ModelIssue {id, IssueSeverity::Error, "Simulation formula did not produce a scalar", "non-scalar-simulation-value", {}};
                target[id] = *scalar;
            } else {
                QVector<std::size_t> shape;
                for (const QString &name : element->dimensions()) {
                    const Dimension *dimension = m_document.findDimension(name);
                    if (!dimension)
                        return ModelIssue {id, IssueSeverity::Error, "Unknown array dimension", "invalid-simulation-array-shape", {}};
                    shape.push_back(dimension->elements.size());
                }
                const std::size_t count = elementCount(shape);
                if (count == 0)
                    return ModelIssue {id, IssueSeverity::Error, "Array shape is invalid or too large", "invalid-simulation-array-shape", {}};
                ArrayValue output {element->dimensions(), shape, QVector<double>(count)};
                if (element->elementEquations().empty()) {
                    for (std::size_t flat = 0; flat < count; ++flat) {
                        const QVector<std::size_t> indices = coordinates(flat, shape);
                        context.scope = {element->dimensions(), indices, element->nameKey()};
                        const EvaluationResult evaluated = evaluateFormula(formula.root(), context);
                        ModelIssue issue;
                        const auto value = evaluatedScalar(evaluated, element->dimensions(), indices, id, issue);
                        if (!value)
                            return issue;
                        output.values[flat] = *value;
                    }
                } else {
                    for (const ArrayElementFormula &entry : element->elementEquations()) {
                        const auto flat = flatIndex(output, element->dimensions(), entry.indices());
                        if (!flat)
                            return ModelIssue {id, IssueSeverity::Error, "Array element index is invalid", "invalid-array-element-index", {}};
                        context.scope = {element->dimensions(), entry.indices(), element->nameKey()};
                        const EvaluationResult evaluated = evaluateFormula(entry.formula().root(), context);
                        ModelIssue issue;
                        const auto value = evaluatedScalar(evaluated, element->dimensions(), entry.indices(), id, issue);
                        if (!value)
                            return issue;
                        output.values[*flat] = *value;
                    }
                }
                target[id] = std::move(output);
            }
            if (!finiteValue(target[id]))
                return ModelIssue {id, IssueSeverity::Error, "Simulation formula produced a non-finite value", "non-finite-simulation-value", {}};
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
                for (const EntityId id : m_plan.m_outputOrder) {
                    const Value &value = values[id];
                    if (const auto *scalar = std::get_if<double>(&value))
                        run.m_series[id].push_back(*scalar);
                    else
                        run.m_arraySeries[id].push_back(std::get<ArrayValue>(value));
                }
            }
            firstPoint = false;
            while (nextSave <= currentTime + timeTolerance)
                nextSave += m_plan.settings().saveInterval();
        }
        if (remaining <= 0.0)
            break;

        QHash<EntityId, Value> nextValues = values;
        if (m_plan.settings().integrationMethod() == IntegrationMethod::Euler) {
            const NetRateResult rates = netRates(m_plan.m_stockFlows, values);
            if (rates.issue)
                return failEvaluation(*rates.issue);
            for (const StockFlows &binding : m_plan.m_stockFlows)
                nextValues[binding.stock] = integratedValue(values[binding.stock], rates.values[binding.stock], dt);
        } else {
            const NetRateResult k1 = netRates(m_plan.m_stockFlows, values);
            if (k1.issue)
                return failEvaluation(*k1.issue);
            QHash<EntityId, Value> stage = values;
            for (const StockFlows &binding : m_plan.m_stockFlows)
                stage[binding.stock] = integratedValue(values[binding.stock], k1.values[binding.stock], dt / 2.0);
            if (const auto issue = evaluate(m_plan.m_evaluationOrder, currentTime + dt / 2.0, dt, stage))
                return failEvaluation(*issue);
            const NetRateResult k2 = netRates(m_plan.m_stockFlows, stage);
            if (k2.issue)
                return failEvaluation(*k2.issue);

            stage = values;
            for (const StockFlows &binding : m_plan.m_stockFlows)
                stage[binding.stock] = integratedValue(values[binding.stock], k2.values[binding.stock], dt / 2.0);
            if (const auto issue = evaluate(m_plan.m_evaluationOrder, currentTime + dt / 2.0, dt, stage))
                return failEvaluation(*issue);
            const NetRateResult k3 = netRates(m_plan.m_stockFlows, stage);
            if (k3.issue)
                return failEvaluation(*k3.issue);

            stage = values;
            for (const StockFlows &binding : m_plan.m_stockFlows)
                stage[binding.stock] = integratedValue(values[binding.stock], k3.values[binding.stock], dt);
            if (const auto issue = evaluate(m_plan.m_evaluationOrder, currentTime + dt, dt, stage))
                return failEvaluation(*issue);
            const NetRateResult k4 = netRates(m_plan.m_stockFlows, stage);
            if (k4.issue)
                return failEvaluation(*k4.issue);

            for (const StockFlows &binding : m_plan.m_stockFlows) {
                const Value rate = weightedRate(values[binding.stock], k1.values[binding.stock],
                    k2.values[binding.stock], k3.values[binding.stock], k4.values[binding.stock], dt);
                nextValues[binding.stock] = integratedValue(values[binding.stock], rate, 1.0);
            }
        }
        for (const StockFlows &binding : m_plan.m_stockFlows) {
            if (!finiteValue(nextValues[binding.stock]))
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
