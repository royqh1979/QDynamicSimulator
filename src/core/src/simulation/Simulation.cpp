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
        || signature->category == FunctionCategory::Array || signature->category == FunctionCategory::TestInput
        || signature->category == FunctionCategory::Statistical)
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
            if (element && element->kind() == ElementKind::Lookup) {
                state[dependency] = 2;
                continue;
            }
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

void advanceIndices(QVector<std::size_t> &indices, const QVector<std::size_t> &shape)
{
    for (std::size_t position = indices.size(); position-- > 0;) {
        ++indices[position];
        if (indices[position] < shape[position])
            return;
        indices[position] = 0;
    }
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
        const auto accumulate = [&](const FlowRatePlan &alignment) -> std::optional<ModelIssue> {
            const Value &flow = state[alignment.flow];
            Value &rate = result.values[binding.stock];
            const double sign = alignment.subtract ? -1.0 : 1.0;
            if (!alignment.compatible) {
                return ModelIssue {binding.stock, IssueSeverity::Error,
                    std::get_if<double>(&rate) ? "Array flow is incompatible with scalar stock"
                        : "Flow dimensions are incompatible with stock", "incompatible-stock-flow", {}};
            }
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
                const std::size_t flowFlat = alignment.direct ? flat : alignment.mapping[flat];
                rateArray.values[flat] += sign * flowArray.values[flowFlat];
            }
            return std::nullopt;
        };
        for (const FlowRatePlan &flow : binding.flows) {
            if (const auto issue = accumulate(flow)) {
                result.issue = *issue;
                return result;
            }
        }
    }
    return result;
}

} // namespace

class SimulationCompiler
{
public:
    SimulationCompiler(const Document &document, SimulationPlan &plan)
        : m_document(document)
        , m_plan(plan)
    {
    }

    bool compile(SimulationCompileResult &result)
    {
        for (const MacroDefinition &macro : m_document.macros())
            m_plan.m_macros.emplace(macro.signature(), std::make_unique<CompiledMacroDefinition>());
        for (const MacroDefinition &macro : m_document.macros()) {
            CompiledMacroDefinition *compiled = m_plan.m_macros.find(macro.signature())->second.get();
            m_locals.clear();
            for (int index = 0; index < macro.parameters().size(); ++index)
                m_locals.insert(macro.parameters()[index].nameKey(), index);
            for (const MacroParameter &parameter : macro.parameters()) {
                CompiledMacroParameter compiledParameter;
                compiledParameter.m_nameKey = parameter.nameKey();
                compiledParameter.m_defaultFormula = compileFormula(parameter.defaultFormula());
                if (!checkFormula(parameter.defaultFormula(), compiledParameter.m_defaultFormula, 0, result))
                    return false;
                compiled->m_parameters.push_back(std::move(compiledParameter));
            }
            compiled->m_formula = compileFormula(macro.formula());
            if (!checkFormula(macro.formula(), compiled->m_formula, 0, result))
                return false;
        }
        m_locals.clear();
        for (const ModelElement &element : m_document.model().elements()) {
            if (element.kind() == ElementKind::Lookup)
                continue;
            CompiledElementFormula compiled;
            const Formula &definition = element.kind() == ElementKind::Stock
                ? static_cast<const Stock &>(element).initialFormula() : element.formula();
            compiled.formula = compileFormula(definition);
            if (!checkFormula(definition, compiled.formula, element.id(), result))
                return false;
            for (const ArrayElementFormula &entry : element.elementEquations()) {
                CompiledArrayElementFormula compiledEntry;
                compiledEntry.indices = entry.indices();
                compiledEntry.formula = compileFormula(entry.formula());
                if (!checkFormula(entry.formula(), compiledEntry.formula, element.id(), result))
                    return false;
                compiled.elements.push_back(std::move(compiledEntry));
            }
            m_plan.m_formulas.emplace(element.id(), std::move(compiled));
        }
        return true;
    }

private:
    CompiledFormula compileFormula(const Formula &formula)
    {
        return CompiledFormula(compileNode(formula.root()));
    }

    bool checkFormula(const Formula &source, const CompiledFormula &compiled, EntityId owner,
        SimulationCompileResult &result)
    {
        if (!source.root() || compiled.root())
            return true;
        addIssue(result, owner, "Failed to bind simulation formula", "simulation-binding", source.root()->span());
        return false;
    }

    std::unique_ptr<FormulaNode> reference(const ReferenceNode &node)
    {
        if (m_locals.contains(node.nameKey()))
            return std::make_unique<LocalReferenceNode>(node.nameKey(), m_locals.value(node.nameKey()), node.span());
        if (const FunctionSignature *signature = findFunctionSignature(node.nameKey());
            signature && signature->minArguments == 0 && signature->maxArguments == 0) {
            if (node.nameKey() == "time")
                return bound(node, BoundReferenceKind::Time);
            if (node.nameKey() == "dt")
                return bound(node, BoundReferenceKind::Dt);
            if (node.nameKey() == "starttime")
                return bound(node, BoundReferenceKind::StartTime);
            if (node.nameKey() == "stoptime")
                return bound(node, BoundReferenceKind::StopTime);
            if (node.nameKey() == "pi")
                return bound(node, BoundReferenceKind::Pi);
            if (node.nameKey() == "inf")
                return bound(node, BoundReferenceKind::Infinity);
            return nullptr;
        }
        const int separator = node.nameKey().indexOf('.');
        if (separator >= 0) {
            const Dimension *dimension = m_document.findDimension(node.nameKey().left(separator));
            const DimensionElement *element = dimension
                ? dimension->findElement(node.nameKey().mid(separator + 1)) : nullptr;
            if (dimension && element) {
                return std::make_unique<BoundReferenceNode>(BoundReferenceKind::DimensionElement, node.nameKey(), 0,
                    static_cast<std::size_t>(element - dimension->elements.data() + 1), node.span());
            }
        }
        if (m_document.findDimension(node.nameKey()))
            return bound(node, BoundReferenceKind::Dimension);
        if (const ModelElement *model = m_document.findByName(node.nameKey()))
            return bound(node, BoundReferenceKind::Variable, model->id());
        return nullptr;
    }

    std::unique_ptr<FormulaNode> bound(const ReferenceNode &node, BoundReferenceKind kind, EntityId variable = 0,
        std::size_t element = 0)
    {
        return std::make_unique<BoundReferenceNode>(kind, node.nameKey(), variable, element, node.span());
    }

    std::unique_ptr<FormulaNode> call(const CallNode &node)
    {
        std::vector<std::unique_ptr<FormulaNode>> arguments;
        arguments.reserve(node.argumentCount());
        for (std::size_t position = 0; position < node.argumentCount(); ++position) {
            auto argument = compileNode(node.argument(position));
            if (!argument)
                return nullptr;
            arguments.push_back(std::move(argument));
        }
        if (const FunctionSignature *signature = findFunctionSignature(node.nameKey())) {
            return std::make_unique<BoundCallNode>(node.nameKey(), *signature, std::move(arguments), node.span());
        }
        if (const MacroDefinition *macro = m_document.findMacro(node.nameKey(), int(node.argumentCount()), 1)) {
            const auto compiled = m_plan.m_macros.find(macro->signature());
            if (compiled == m_plan.m_macros.end())
                return nullptr;
            return std::make_unique<BoundCallNode>(node.nameKey(), compiled->second.get(), std::move(arguments),
                node.span());
        }
        if (const ModelElement *model = m_document.findByName(node.nameKey());
            model && model->kind() == ElementKind::Lookup && model->lookup() && node.argumentCount() == 1) {
            return std::make_unique<BoundCallNode>(node.nameKey(), &*model->lookup(), std::move(arguments),
                node.span());
        }
        return nullptr;
    }

    std::unique_ptr<FormulaNode> compileNode(const FormulaNode *node)
    {
        if (!node)
            return nullptr;
        if (const auto *number = dynamic_cast<const NumberNode *>(node))
            return std::make_unique<NumberNode>(number->value(), node->span());
        if (const auto *referenceNode = dynamic_cast<const ReferenceNode *>(node))
            return reference(*referenceNode);
        if (const auto *unary = dynamic_cast<const UnaryNode *>(node))
            return std::make_unique<UnaryNode>(unary->op(), compileNode(unary->operand()), node->span());
        if (const auto *binary = dynamic_cast<const BinaryNode *>(node)) {
            return std::make_unique<BinaryNode>(binary->op(), compileNode(binary->left()), compileNode(binary->right()),
                node->span());
        }
        if (const auto *callNode = dynamic_cast<const CallNode *>(node))
            return call(*callNode);
        if (const auto *conditional = dynamic_cast<const ConditionalNode *>(node)) {
            return std::make_unique<ConditionalNode>(compileNode(conditional->condition()),
                compileNode(conditional->thenExpression()), compileNode(conditional->elseExpression()), node->span());
        }
        if (const auto *subscript = dynamic_cast<const SubscriptNode *>(node)) {
            std::vector<std::unique_ptr<FormulaNode>> indices;
            indices.reserve(subscript->indexCount());
            for (std::size_t position = 0; position < subscript->indexCount(); ++position)
                indices.push_back(compileNode(subscript->index(position)));
            return std::make_unique<SubscriptNode>(compileNode(subscript->target()), std::move(indices), node->span());
        }
        if (dynamic_cast<const WildcardNode *>(node))
            return std::make_unique<WildcardNode>(node->span());
        if (const auto *range = dynamic_cast<const RangeNode *>(node))
            return std::make_unique<RangeNode>(compileNode(range->begin()), compileNode(range->end()), node->span());
        if (const auto *position = dynamic_cast<const DimensionPositionNode *>(node))
            return std::make_unique<DimensionPositionNode>(position->position(), node->span());
        if (const auto *literal = dynamic_cast<const ArrayLiteralNode *>(node)) {
            std::vector<std::unique_ptr<FormulaNode>> values;
            values.reserve(literal->valueCount());
            for (std::size_t position = 0; position < literal->valueCount(); ++position)
                values.push_back(compileNode(literal->value(position)));
            return std::make_unique<ArrayLiteralNode>(std::move(values), node->span());
        }
        return nullptr;
    }

    const Document &m_document;
    SimulationPlan &m_plan;
    QHash<QString, int> m_locals;
};

ArrayLayout::ArrayLayout(QVector<QString> dimensions, QVector<std::size_t> shape,
    QVector<std::size_t> strides, std::size_t count)
    : m_dimensions(std::move(dimensions))
    , m_shape(std::move(shape))
    , m_strides(std::move(strides))
    , m_count(count)
{
}

std::optional<std::size_t> ArrayLayout::flatIndex(const QVector<std::size_t> &indices) const
{
    if (indices.size() != m_shape.size())
        return std::nullopt;
    std::size_t flat = 0;
    for (std::size_t position = 0; position < indices.size(); ++position) {
        if (indices[position] >= m_shape[position])
            return std::nullopt;
        flat += indices[position] * m_strides[position];
    }
    return flat;
}

QVector<std::size_t> ArrayLayout::mappingFrom(const ArrayLayout &source) const
{
    if (m_dimensions.size() != source.m_dimensions.size())
        return {};
    QVector<std::size_t> sourcePositions;
    sourcePositions.reserve(m_dimensions.size());
    for (std::size_t position = 0; position < m_dimensions.size(); ++position) {
        const auto found = std::find(source.m_dimensions.begin(), source.m_dimensions.end(), m_dimensions[position]);
        if (found == source.m_dimensions.end()
            || m_shape[position] != source.m_shape[found - source.m_dimensions.begin()])
            return {};
        sourcePositions.push_back(found - source.m_dimensions.begin());
    }

    QVector<std::size_t> mapping;
    mapping.reserve(m_count);
    for (std::size_t flat = 0; flat < m_count; ++flat) {
        const QVector<std::size_t> targetIndices = coordinates(flat, m_shape);
        QVector<std::size_t> sourceIndices(sourcePositions.size());
        for (std::size_t position = 0; position < sourcePositions.size(); ++position)
            sourceIndices[sourcePositions[position]] = targetIndices[position];
        const auto sourceFlat = source.flatIndex(sourceIndices);
        if (!sourceFlat)
            return {};
        mapping.push_back(*sourceFlat);
    }
    return mapping;
}

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
    if (const ModelElement *noiseRng = document.findByName("noise_rng"))
        addIssue(result, noiseRng->id(), "NOISE RNG is not supported", "unsupported-noise-rng");
    quint32 noiseSeed = RandomStreamState::defaultNoiseSeed;
    if (const ModelElement *noise = document.findByName("noise_seed")) {
        bool numeric = false;
        const double value = noise->equation().toDouble(&numeric);
        if (!isConstantFormula(noise->equation()) || !numeric || !std::isfinite(value) || value < 0.0
            || std::floor(value) != value || value > 2147483647.0) {
            addIssue(result, noise->id(), "NOISE SEED must be a non-negative integer literal", "invalid-noise-seed");
        } else {
            noiseSeed = static_cast<quint32>(value);
        }
    }

    QVector<EntityId> initializationRoots;
    QVector<EntityId> stocks;
    QVector<EntityId> runtimeRoots;
    QVector<EntityId> outputs;
    for (const ModelElement &element : document.model().elements()) {
        const EntityId id = element.id();
        if (element.kind() == ElementKind::Lookup) {
            if (!element.lookup())
                addIssue(result, id, "Lookup has no definition: " + element.displayName(), "missing-lookup-definition");
            continue;
        }
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
    SimulationCompiler compiler(document, plan);
    if (!compiler.compile(result))
        return result;
    plan.m_settings = settings;
    plan.m_noiseSeed = noiseSeed;
    plan.m_outputOrder = outputs;
    plan.m_initializationOrder = topologicalOrder(initializationRoots, document, analysis, true, result);
    const QVector<EntityId> stockInitialization = topologicalOrder(stocks, document, analysis, true, result);
    for (EntityId id : stockInitialization) {
        if (!plan.m_initializationOrder.contains(id))
            plan.m_initializationOrder.push_back(id);
    }
    plan.m_evaluationOrder = topologicalOrder(runtimeRoots, document, analysis, false, result);
    for (const ModelElement &element : document.model().elements()) {
        if (element.dimensions().isEmpty())
            continue;
        QVector<std::size_t> shape;
        for (const QString &name : element.dimensions()) {
            const Dimension *dimension = document.findDimension(name);
            if (!dimension) {
                addIssue(result, element.id(), "Unknown array dimension: " + name, "invalid-simulation-array-shape");
                continue;
            }
            shape.push_back(dimension->elements.size());
        }
        const std::size_t count = elementCount(shape);
        if (count == 0) {
            addIssue(result, element.id(), "Array shape is invalid or too large", "invalid-simulation-array-shape");
            continue;
        }
        const QVector<std::size_t> steps = strides(shape);
        plan.m_arrayLayouts.insert(element.id(),
            ArrayLayout(element.dimensions(), std::move(shape), steps, count));
    }
    for (EntityId stock : stocks) {
        StockFlows binding;
        binding.stock = stock;
        QVector<EntityId> inflows;
        QVector<EntityId> outflows;
        for (const ModelElement &element : document.model().elements()) {
            if (element.kind() != ElementKind::Flow)
                continue;
            const FlowEndpoints endpoints = static_cast<const Flow &>(element).endpoints();
            if (endpoints.to == stock)
                inflows.push_back(element.id());
            if (endpoints.from == stock)
                outflows.push_back(element.id());
        }
        std::sort(inflows.begin(), inflows.end());
        std::sort(outflows.begin(), outflows.end());
        const auto appendFlow = [&](EntityId flow, bool subtract) {
            FlowRatePlan rate;
            rate.flow = flow;
            rate.subtract = subtract;
            const auto stockLayout = plan.m_arrayLayouts.constFind(stock);
            const auto flowLayout = plan.m_arrayLayouts.constFind(flow);
            const bool stockArray = stockLayout != plan.m_arrayLayouts.constEnd();
            const bool flowArray = flowLayout != plan.m_arrayLayouts.constEnd();
            if (stockArray && flowArray) {
                if (stockLayout->dimensions() == flowLayout->dimensions()
                    && stockLayout->shape() == flowLayout->shape()) {
                    rate.direct = true;
                } else {
                    rate.mapping = stockLayout->mappingFrom(*flowLayout);
                    rate.compatible = rate.mapping.size() == stockLayout->count();
                }
            } else if (!stockArray && flowArray) {
                rate.compatible = false;
            }
            binding.flows.push_back(std::move(rate));
        };
        for (EntityId flow : inflows)
            appendFlow(flow, false);
        for (EntityId flow : outflows)
            appendFlow(flow, true);
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

QVector<double> SimulationRun::series(const QString &nameKey) const
{
    const RunVariableInfo *match = variable(nameKey);
    return match ? series(match->id()) : QVector<double> {};
}

QVector<ArrayValue> SimulationRun::arraySeries(const QString &nameKey) const
{
    const RunVariableInfo *match = variable(nameKey);
    return match ? arraySeries(match->id()) : QVector<ArrayValue> {};
}

QVector<double> SimulationRun::series(EntityId id) const
{
    return m_series.value(id);
}

QVector<ArrayValue> SimulationRun::arraySeries(EntityId id) const
{
    return m_arraySeries.value(id);
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
    EvaluationContext context;
    context.executionStack.reserveValues(ExecutionStack::runValueStorageBytes / int(sizeof(Value)));
    context.randomStreams.setDefaultSeed(m_plan.m_noiseSeed);
    context.startTime = m_plan.settings().startTime();
    context.stopTime = m_plan.settings().stopTime();
    context.inputTime = currentTime;
    context.inputDt = m_plan.settings().dt();
    struct ExecutionContext {
        const Document &document;
        EvaluationContext &evaluation;
        QHash<EntityId, Value> *target;
    };
    ExecutionContext resolved {m_document, context, &values};
    context.readDimension = [&resolved](const QString &name) {
        return resolved.document.findDimension(name);
    };
    context.readEntity = [&resolved](EntityId id) -> std::optional<Value> {
        const auto value = resolved.target->constFind(id);
        return value == resolved.target->constEnd() ? std::nullopt : std::optional<Value>(value.value());
    };
    const auto evaluate = [&](const QVector<EntityId> &order, double time, double dt,
                            QHash<EntityId, Value> &target) -> std::optional<ModelIssue> {
        evaluationTime = time;
        context.time = time;
        context.dt = dt;
        resolved.target = &target;
        for (EntityId id : order) {
            const auto *element = dynamic_cast<const ModelElement *>(m_document.find(id));
            if (!element)
                return ModelIssue {id, IssueSeverity::Error, "Simulation variable no longer exists", "missing-simulation-variable", {}};
            const auto compiled = m_plan.m_formulas.find(id);
            if (compiled == m_plan.m_formulas.end())
                return ModelIssue {id, IssueSeverity::Error, "Simulation formula is not compiled", "missing-simulation-formula", {}};
            const FormulaNode *formula = compiled->second.formula.root();
            context.scope = {};
            if (element->dimensions().isEmpty()) {
                const EvaluationResult evaluated = evaluateFormula(formula, context);
                if (!evaluated.value)
                    return ModelIssue {id, IssueSeverity::Error, evaluated.error, "simulation-evaluation", evaluated.errorSpan};
                const auto *scalar = std::get_if<double>(&*evaluated.value);
                if (!scalar)
                    return ModelIssue {id, IssueSeverity::Error, "Simulation formula did not produce a scalar", "non-scalar-simulation-value", {}};
                double value = *scalar;
                if (element->lookup())
                    value = element->lookup()->evaluate(value);
                target[id] = value;
            } else {
                const auto layout = m_plan.m_arrayLayouts.constFind(id);
                if (layout == m_plan.m_arrayLayouts.constEnd())
                    return ModelIssue {id, IssueSeverity::Error, "Array shape is invalid or too large", "invalid-simulation-array-shape", {}};
                const std::size_t count = layout->count();
                ArrayValue output {layout->dimensions(), layout->shape(), QVector<double>(count)};
                if (compiled->second.elements.empty()) {
                    context.scope = {layout->dimensions(), QVector<std::size_t>(layout->shape().size(), 0),
                        element->nameKey()};
                    for (std::size_t flat = 0; flat < count; ++flat) {
                        const EvaluationResult evaluated = evaluateFormula(formula, context);
                        ModelIssue issue;
                        const auto value = evaluatedScalar(evaluated, element->dimensions(), context.scope.indices, id, issue);
                        if (!value)
                            return issue;
                        output.values[flat] = *value;
                        advanceIndices(context.scope.indices, layout->shape());
                    }
                } else {
                    for (const CompiledArrayElementFormula &entry : compiled->second.elements) {
                        const auto flat = layout->flatIndex(entry.indices);
                        if (!flat)
                            return ModelIssue {id, IssueSeverity::Error, "Array element index is invalid", "invalid-array-element-index", {}};
                        context.scope = {element->dimensions(), entry.indices, element->nameKey()};
                        const EvaluationResult evaluated = evaluateFormula(entry.formula.root(), context);
                        ModelIssue issue;
                        const auto value = evaluatedScalar(evaluated, element->dimensions(), entry.indices, id, issue);
                        if (!value)
                            return issue;
                        output.values[*flat] = *value;
                    }
                }
                if (element->lookup()) {
                    for (double &value : output.values)
                        value = element->lookup()->evaluate(value);
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
        context.inputTime = currentTime;
        context.inputDt = dt;
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
