#include <core/formula/Evaluation.h>

#include <core/formula/Binding.h>
#include <core/formula/Function.h>
#include <core/Lookup.h>
#include <core/Macro.h>

#include <QHash>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <utility>

namespace qds::core {

ExecutionStack::ExecutionStack()
{
    m_frames.reserve(maximumFrameDepth);
}

void ExecutionStack::restore(const Mark &mark)
{
    m_frames.resize(mark.frames);
    m_values.resize(mark.values);
}

void ExecutionStack::pushFrame(std::size_t base, const MacroDefinition *source)
{
    m_frames.push_back({base, source, nullptr});
}

void ExecutionStack::pushFrame(std::size_t base, const CompiledMacroDefinition *compiled)
{
    m_frames.push_back({base, nullptr, compiled});
}

namespace {

EvaluationResult success(Value value)
{
    return {std::move(value), {}, {}};
}

EvaluationResult success(double value)
{
    return success(Value(value));
}

EvaluationResult failure(const FormulaNode *node, QString message)
{
    return {std::nullopt, std::move(message), node ? node->span() : SourceSpan {}};
}

bool truth(double value)
{
    return value != 0.0;
}

const ArrayValue *arrayOf(const Value &value)
{
    return std::get_if<ArrayValue>(&value);
}

std::size_t elementCount(const QVector<std::size_t> &shape)
{
    std::size_t count = 1;
    for (std::size_t size : shape) {
        if (size == 0 || count > std::numeric_limits<std::size_t>::max() / size)
            return 0;
        count *= size;
    }
    return count;
}

bool validArray(const ArrayValue &array)
{
    return !array.dimensions.isEmpty() && array.dimensions.size() == array.shape.size()
        && elementCount(array.shape) == array.values.size();
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

bool subscriptIndex(double value, std::size_t size, std::size_t &index)
{
    if (!std::isfinite(value) || value < 1.0 || std::floor(value) != value || value > static_cast<double>(size))
        return false;
    index = static_cast<std::size_t>(value) - 1;
    return true;
}

class ExecutionStackGuard
{
public:
    explicit ExecutionStackGuard(ExecutionStack &stack)
        : m_stack(stack)
        , m_mark(stack.mark())
    {
    }
    ~ExecutionStackGuard() { m_stack.restore(m_mark); }

    ExecutionStackGuard(const ExecutionStackGuard &) = delete;
    ExecutionStackGuard &operator=(const ExecutionStackGuard &) = delete;

private:
    ExecutionStack &m_stack;
    ExecutionStack::Mark m_mark;
};

class Evaluator
{
public:
    explicit Evaluator(const EvaluationContext &context)
        : m_context(context)
    {
    }

    EvaluationResult evaluate(const FormulaNode *node)
    {
        if (!node)
            return failure(node, "Missing expression");
        switch (node->kind()) {
        case FormulaNodeKind::Number:
            return success(static_cast<const NumberNode *>(node)->value());
        case FormulaNodeKind::Reference:
            return reference(*static_cast<const ReferenceNode *>(node));
        case FormulaNodeKind::BoundReference:
            return boundReference(*static_cast<const BoundReferenceNode *>(node));
        case FormulaNodeKind::LocalReference:
            return localReference(*static_cast<const LocalReferenceNode *>(node));
        case FormulaNodeKind::Unary:
            return unary(*static_cast<const UnaryNode *>(node));
        case FormulaNodeKind::Binary:
            return binary(*static_cast<const BinaryNode *>(node));
        case FormulaNodeKind::Call:
            return call(*static_cast<const CallNode *>(node));
        case FormulaNodeKind::BoundCall:
            return boundCall(*static_cast<const BoundCallNode *>(node));
        case FormulaNodeKind::Conditional:
            return conditional(*static_cast<const ConditionalNode *>(node));
        case FormulaNodeKind::Subscript:
            return subscript(*static_cast<const SubscriptNode *>(node));
        case FormulaNodeKind::Wildcard:
        case FormulaNodeKind::Range:
        case FormulaNodeKind::DimensionPosition:
            return failure(node, "Subscript component requires an array subscript");
        case FormulaNodeKind::ArrayLiteral:
            return arrayLiteral(*static_cast<const ArrayLiteralNode *>(node));
        }
        return failure(node, "Unknown formula node");
    }

private:
    EvaluationResult constant(const FormulaNode &node, const QString &nameKey)
    {
        if (nameKey == "time")
            return success(m_context.time);
        if (nameKey == "dt")
            return success(m_context.dt);
        if (nameKey == "starttime")
            return success(m_context.startTime);
        if (nameKey == "stoptime")
            return success(m_context.stopTime);
        if (nameKey == "pi")
            return success(3.14159265358979323846);
        if (nameKey == "inf")
            return success(std::numeric_limits<double>::infinity());
        return failure(&node, "Constant is not supported: " + nameKey);
    }

    EvaluationResult checkedValue(std::optional<Value> value, const QString &nameKey, const FormulaNode &node)
    {
        if (!value)
            return failure(&node, "Variable has no value: " + nameKey);
        if (const ArrayValue *array = arrayOf(*value); array && !validArray(*array))
            return failure(&node, "Invalid array value for: " + nameKey);
        return success(std::move(*value));
    }

    EvaluationResult readVariable(const QString &nameKey, const FormulaNode &node)
    {
        if (nameKey.isEmpty())
            return failure(&node, "Unresolved variable");
        if (!m_context.readVariable)
            return failure(&node, "No variable reader for: " + nameKey);
        return checkedValue(m_context.readVariable(nameKey), nameKey, node);
    }

    std::optional<std::size_t> localIndex(const QString &nameKey) const
    {
        ExecutionStack &stack = m_context.executionStack;
        if (stack.frameCount() == 0)
            return std::nullopt;
        const ExecutionStack::Frame &frame = stack.currentFrame();
        if (frame.source) {
            for (std::size_t index = 0; index < frame.source->parameters().size(); ++index) {
                if (frame.source->parameters()[index].nameKey() == nameKey)
                    return index;
            }
        } else if (frame.compiled) {
            for (std::size_t index = 0; index < frame.compiled->parameters().size(); ++index) {
                if (frame.compiled->parameters()[index].nameKey() == nameKey)
                    return index;
            }
        }
        return std::nullopt;
    }

    bool hasLocal(const QString &nameKey) const
    {
        return localIndex(nameKey).has_value();
    }

    EvaluationResult boundReference(const BoundReferenceNode &node)
    {
        switch (node.referenceKind()) {
        case BoundReferenceKind::Variable:
            if (!m_context.readEntity)
                return failure(&node, "No variable reader for: " + node.nameKey());
            return checkedValue(m_context.readEntity(node.variable()), node.nameKey(), node);
        case BoundReferenceKind::Dimension:
            for (std::size_t index = 0; index < m_context.scope.dimensions.size(); ++index) {
                if (m_context.scope.dimensions[index] == node.nameKey()
                    && index < m_context.scope.indices.size())
                    return success(static_cast<double>(m_context.scope.indices[index] + 1));
            }
            return failure(&node, "Dimension requires a current array element: " + node.nameKey());
        case BoundReferenceKind::DimensionElement:
            return success(static_cast<double>(node.element()));
        case BoundReferenceKind::Time:
            return success(m_context.time);
        case BoundReferenceKind::Dt:
            return success(m_context.dt);
        case BoundReferenceKind::StartTime:
            return success(m_context.startTime);
        case BoundReferenceKind::StopTime:
            return success(m_context.stopTime);
        case BoundReferenceKind::Pi:
            return success(3.14159265358979323846);
        case BoundReferenceKind::Infinity:
            return success(std::numeric_limits<double>::infinity());
        }
        return failure(&node, "Unknown bound reference");
    }

    EvaluationResult localReference(const LocalReferenceNode &node)
    {
        ExecutionStack &stack = m_context.executionStack;
        if (stack.frameCount() == 0)
            return failure(&node, "Unresolved macro parameter: " + node.nameKey());
        const ExecutionStack::Frame &frame = stack.currentFrame();
        return success(stack.value(frame.base + node.slot()));
    }

    EvaluationResult reference(const ReferenceNode &node)
    {
        if (const auto index = localIndex(node.nameKey())) {
            const ExecutionStack::Frame &frame = m_context.executionStack.currentFrame();
            return success(m_context.executionStack.value(frame.base + *index));
        }
        if (const FunctionSignature *signature = findFunctionSignature(node.nameKey());
            signature && signature->minArguments == 0 && signature->maxArguments == 0) {
            if (node.nameKey() == "self")
                return readVariable(m_context.scope.self, node);
            return constant(node, node.nameKey());
        }
        const int separator = node.nameKey().indexOf('.');
        if (separator >= 0) {
            const Dimension *dimension = m_context.readDimension
                ? m_context.readDimension(node.nameKey().left(separator)) : nullptr;
            const DimensionElement *element = dimension
                ? dimension->findElement(node.nameKey().mid(separator + 1)) : nullptr;
            if (element)
                return success(static_cast<double>(element - dimension->elements.data() + 1));
        }
        if (m_context.readDimension) {
            if (const Dimension *dimension = m_context.readDimension(node.nameKey())) {
                for (std::size_t index = 0; index < m_context.scope.dimensions.size(); ++index) {
                    if (m_context.scope.dimensions[index] == dimension->nameKey
                        && index < m_context.scope.indices.size())
                        return success(static_cast<double>(m_context.scope.indices[index] + 1));
                }
                return failure(&node, "Dimension requires a current array element: " + node.nameKey());
            }
        }
        return readVariable(node.nameKey(), node);
    }

    EvaluationResult broadcast(const Value &value, const QVector<QString> &dimensions, const FormulaNode &node)
    {
        if (dimensions.isEmpty())
            return success(value);
        QVector<std::size_t> shape;
        shape.reserve(dimensions.size());
        for (const QString &name : dimensions) {
            const Dimension *dimension = m_context.readDimension ? m_context.readDimension(name) : nullptr;
            if (!dimension)
                return failure(&node, "Unknown output dimension: " + name);
            shape.push_back(dimension->elements.size());
        }
        const std::size_t count = elementCount(shape);
        if (const auto *scalar = std::get_if<double>(&value))
            return success(ArrayValue {dimensions, shape, QVector<double>(count, *scalar)});

        const ArrayValue &input = *arrayOf(value);
        if (!validArray(input) || input.dimensions.size() != dimensions.size())
            return failure(&node, "Array dimensions are incompatible");
        QVector<std::size_t> inputPositions;
        inputPositions.reserve(dimensions.size());
        for (const QString &name : dimensions) {
            const auto found = std::find(input.dimensions.begin(), input.dimensions.end(), name);
            if (found == input.dimensions.end())
                return failure(&node, "Array dimensions are incompatible");
            inputPositions.push_back(static_cast<std::size_t>(found - input.dimensions.begin()));
        }
        for (std::size_t index = 0; index < input.shape.size(); ++index) {
            const Dimension *dimension = m_context.readDimension ? m_context.readDimension(input.dimensions[index]) : nullptr;
            if (!dimension || input.shape[index] != dimension->elements.size())
                return failure(&node, "Array shape does not match its dimensions");
        }
        if (input.dimensions == dimensions && input.shape == shape)
            return success(input);

        ArrayValue output {dimensions, shape, {}};
        output.values.resize(count);
        const QVector<std::size_t> inputSteps = strides(input.shape);
        for (std::size_t flat = 0; flat < count; ++flat) {
            const QVector<std::size_t> outputCoordinates = coordinates(flat, shape);
            std::size_t inputFlat = 0;
            for (std::size_t index = 0; index < dimensions.size(); ++index)
                inputFlat += outputCoordinates[index] * inputSteps[inputPositions[index]];
            output.values[flat] = input.values[inputFlat];
        }
        return success(std::move(output));
    }

    template <typename Operation>
    EvaluationResult elementwise(const Value &left, const Value &right, const FormulaNode &node, Operation operation)
    {
        if (const auto *lhs = std::get_if<double>(&left)) {
            if (const auto *rhs = std::get_if<double>(&right))
                return success(operation(*lhs, *rhs));
        }
        QVector<QString> dimensions = m_context.scope.indices.isEmpty()
            ? m_context.scope.dimensions : QVector<QString> {};
        if (dimensions.isEmpty()) {
            if (arrayOf(left))
                dimensions = arrayOf(left)->dimensions;
            else if (arrayOf(right))
                dimensions = arrayOf(right)->dimensions;
        }
        EvaluationResult lhs = broadcast(left, dimensions, node);
        if (!lhs.value)
            return lhs;
        EvaluationResult rhs = broadcast(right, dimensions, node);
        if (!rhs.value)
            return rhs;
        const ArrayValue &leftArray = *arrayOf(*lhs.value);
        const ArrayValue &rightArray = *arrayOf(*rhs.value);
        ArrayValue output {leftArray.dimensions, leftArray.shape, {}};
        output.values.reserve(leftArray.values.size());
        for (std::size_t index = 0; index < leftArray.values.size(); ++index)
            output.values.push_back(operation(leftArray.values[index], rightArray.values[index]));
        return success(std::move(output));
    }

    template <typename Operation>
    EvaluationResult elementwise(const Value &input, const FormulaNode &node, Operation operation)
    {
        if (const auto *scalar = std::get_if<double>(&input))
            return success(operation(*scalar));
        EvaluationResult value = broadcast(input, m_context.scope.dimensions.isEmpty() || !m_context.scope.indices.isEmpty()
                ? arrayOf(input)->dimensions : m_context.scope.dimensions, node);
        if (!value.value)
            return value;
        ArrayValue output = *arrayOf(*value.value);
        for (double &item : output.values)
            item = operation(item);
        return success(std::move(output));
    }

    EvaluationResult unary(const UnaryNode &node)
    {
        if (node.op() == UnaryOperator::Transpose)
            return transpose(node);
        EvaluationResult operand = evaluate(node.operand());
        if (!operand.value)
            return operand;
        switch (node.op()) {
        case UnaryOperator::Positive:
            return operand;
        case UnaryOperator::Negative:
            return elementwise(*operand.value, node, [](double value) { return -value; });
        case UnaryOperator::Not:
            return elementwise(*operand.value, node, [](double value) { return truth(value) ? 0.0 : 1.0; });
        case UnaryOperator::Transpose:
            break;
        }
        return failure(&node, "Unknown unary operator");
    }

    EvaluationResult binary(const BinaryNode &node)
    {
        EvaluationResult left = evaluate(node.left());
        if (!left.value)
            return left;
        if (const auto *scalar = std::get_if<double>(&*left.value)) {
            if (node.op() == BinaryOperator::And && !truth(*scalar))
                return success(0.0);
            if (node.op() == BinaryOperator::Or && truth(*scalar))
                return success(1.0);
        }
        EvaluationResult right = evaluate(node.right());
        if (!right.value)
            return right;
        return elementwise(*left.value, *right.value, node, [&node](double lhs, double rhs) {
            switch (node.op()) {
            case BinaryOperator::Power: return std::pow(lhs, rhs);
            case BinaryOperator::Multiply: return lhs * rhs;
            case BinaryOperator::Divide: return lhs / rhs;
            case BinaryOperator::Modulo: {
                double value = std::fmod(lhs, rhs);
                if (value != 0.0 && (value < 0.0) != (rhs < 0.0))
                    value += rhs;
                return value;
            }
            case BinaryOperator::Add: return lhs + rhs;
            case BinaryOperator::Subtract: return lhs - rhs;
            case BinaryOperator::Less: return lhs < rhs ? 1.0 : 0.0;
            case BinaryOperator::LessEqual: return lhs <= rhs ? 1.0 : 0.0;
            case BinaryOperator::Greater: return lhs > rhs ? 1.0 : 0.0;
            case BinaryOperator::GreaterEqual: return lhs >= rhs ? 1.0 : 0.0;
            case BinaryOperator::Equal: return lhs == rhs ? 1.0 : 0.0;
            case BinaryOperator::NotEqual: return lhs != rhs ? 1.0 : 0.0;
            case BinaryOperator::And: return truth(lhs) && truth(rhs) ? 1.0 : 0.0;
            case BinaryOperator::Or: return truth(lhs) || truth(rhs) ? 1.0 : 0.0;
            }
            return std::numeric_limits<double>::quiet_NaN();
        });
    }

    EvaluationResult select(const Value &condition, const FormulaNode *thenNode, const FormulaNode *elseNode,
        const FormulaNode &node)
    {
        if (const auto *scalar = std::get_if<double>(&condition))
            return evaluate(truth(*scalar) ? thenNode : elseNode);
        EvaluationResult thenValue = evaluate(thenNode);
        if (!thenValue.value)
            return thenValue;
        EvaluationResult elseValue = evaluate(elseNode);
        if (!elseValue.value)
            return elseValue;
        return elementwise(condition, *thenValue.value, *elseValue.value, node);
    }

    EvaluationResult elementwise(const Value &condition, const Value &thenValue, const Value &elseValue,
        const FormulaNode &node)
    {
        QVector<QString> dimensions = m_context.scope.dimensions.isEmpty() || !m_context.scope.indices.isEmpty()
            ? arrayOf(condition)->dimensions : m_context.scope.dimensions;
        EvaluationResult test = broadcast(condition, dimensions, node);
        if (!test.value)
            return test;
        EvaluationResult yes = broadcast(thenValue, dimensions, node);
        if (!yes.value)
            return yes;
        EvaluationResult no = broadcast(elseValue, dimensions, node);
        if (!no.value)
            return no;
        const ArrayValue &conditions = *arrayOf(*test.value);
        const ArrayValue &thenArray = *arrayOf(*yes.value);
        const ArrayValue &elseArray = *arrayOf(*no.value);
        ArrayValue output {conditions.dimensions, conditions.shape, {}};
        output.values.reserve(conditions.values.size());
        for (std::size_t index = 0; index < conditions.values.size(); ++index)
            output.values.push_back(truth(conditions.values[index]) ? thenArray.values[index] : elseArray.values[index]);
        return success(std::move(output));
    }

    EvaluationResult conditional(const ConditionalNode &node)
    {
        EvaluationResult condition = evaluate(node.condition());
        if (!condition.value)
            return condition;
        return select(*condition.value, node.thenExpression(), node.elseExpression(), node);
    }

    EvaluationResult transpose(const UnaryNode &node)
    {
        EvaluationResult operand = evaluate(node.operand());
        if (!operand.value)
            return operand;
        if (std::get_if<double>(&*operand.value))
            return operand;
        const ArrayValue &input = *arrayOf(*operand.value);
        ArrayValue output {input.dimensions, input.shape, input.values};
        std::reverse(output.dimensions.begin(), output.dimensions.end());
        std::reverse(output.shape.begin(), output.shape.end());
        const QVector<std::size_t> outputSteps = strides(output.shape);
        for (std::size_t flat = 0; flat < input.values.size(); ++flat) {
            const QVector<std::size_t> inputCoordinates = coordinates(flat, input.shape);
            std::size_t outputFlat = 0;
            for (std::size_t index = 0; index < inputCoordinates.size(); ++index)
                outputFlat += inputCoordinates[index] * outputSteps[output.shape.size() - 1 - index];
            output.values[outputFlat] = input.values[flat];
        }
        return success(std::move(output));
    }

    EvaluationResult subscript(const SubscriptNode &node)
    {
        EvaluationResult target = evaluate(node.target());
        if (!target.value)
            return target;
        const ArrayValue *input = arrayOf(*target.value);
        if (!input)
            return failure(&node, "Subscript target is not an array");
        if (node.indexCount() > input->dimensions.size())
            return failure(&node, "Too many array subscripts");

        struct Selection {
            std::size_t begin = 0;
            std::size_t end = 0;
            bool preserve = false;
        };
        QVector<Selection> selections(input->dimensions.size());
        for (std::size_t position = 0; position < selections.size(); ++position) {
            selections[position].end = input->shape[position] - 1;
            selections[position].preserve = true;
        }
        for (std::size_t position = 0; position < node.indexCount(); ++position) {
            Selection selection {0, input->shape[position] - 1, false};
            const FormulaNode *indexNode = node.index(position);
            if (dynamic_cast<const WildcardNode *>(indexNode)) {
                selection.preserve = true;
            } else if (const auto *range = dynamic_cast<const RangeNode *>(indexNode)) {
                EvaluationResult first = evaluate(range->begin());
                EvaluationResult last = evaluate(range->end());
                if (!first.value)
                    return first;
                if (!last.value)
                    return last;
                const auto *begin = std::get_if<double>(&*first.value);
                const auto *end = std::get_if<double>(&*last.value);
                if (!begin || !end || !subscriptIndex(*begin, input->shape[position], selection.begin)
                    || !subscriptIndex(*end, input->shape[position], selection.end)
                    || selection.begin > selection.end)
                    return success(0.0);
                selection.preserve = true;
            } else {
                bool resolved = false;
                bool preserve = false;
                QString dimensionName;
                if (const auto *dimensionPosition = dynamic_cast<const DimensionPositionNode *>(indexNode)) {
                    if (dimensionPosition->position() == 0
                        || dimensionPosition->position() > m_context.scope.dimensions.size())
                        return success(0.0);
                    dimensionName = m_context.scope.dimensions[dimensionPosition->position() - 1];
                    resolved = true;
                } else if (const auto *referenceNode = dynamic_cast<const ReferenceNode *>(indexNode)) {
                    if (hasLocal(referenceNode->nameKey())) {
                        EvaluationResult value = reference(*referenceNode);
                        if (!value.value)
                            return value;
                        const auto *number = std::get_if<double>(&*value.value);
                        if (!number || !subscriptIndex(*number, input->shape[position], selection.begin))
                            return success(0.0);
                        selection.end = selection.begin;
                        resolved = true;
                    } else {
                        const Dimension *dimension = m_context.readDimension
                            ? m_context.readDimension(input->dimensions[position]) : nullptr;
                        if (!dimension)
                            return failure(indexNode, "Unknown array dimension: " + input->dimensions[position]);
                        if (referenceNode->nameKey() == dimension->nameKey) {
                            dimensionName = dimension->nameKey;
                            resolved = true;
                        } else if (const DimensionElement *element = dimension->findElement(referenceNode->nameKey())) {
                            selection.begin = selection.end = static_cast<std::size_t>(element - dimension->elements.data());
                            resolved = true;
                        } else if (referenceNode->nameKey().contains(QChar('.'))) {
                            EvaluationResult value = reference(*referenceNode);
                            if (!value.value)
                                return value;
                            const double number = *std::get_if<double>(&*value.value);
                            if (!subscriptIndex(number, input->shape[position], selection.begin))
                                return success(0.0);
                            selection.end = selection.begin;
                            resolved = true;
                        }
                    }
                } else if (const auto *bound = dynamic_cast<const BoundReferenceNode *>(indexNode);
                    bound && bound->referenceKind() == BoundReferenceKind::Dimension) {
                    dimensionName = bound->nameKey();
                    resolved = true;
                }
                if (resolved && !dimensionName.isEmpty()) {
                    const auto scope = std::find(m_context.scope.dimensions.begin(), m_context.scope.dimensions.end(), dimensionName);
                    if (scope != m_context.scope.dimensions.end()) {
                        const std::size_t scopeIndex = static_cast<std::size_t>(scope - m_context.scope.dimensions.begin());
                        if (scopeIndex < m_context.scope.indices.size()) {
                            selection.begin = selection.end = m_context.scope.indices[scopeIndex];
                            if (selection.begin >= input->shape[position])
                                return success(0.0);
                        } else {
                            preserve = true;
                        }
                    } else {
                        preserve = true;
                    }
                    selection.preserve = preserve;
                } else if (!resolved) {
                    EvaluationResult value = evaluate(indexNode);
                    if (!value.value)
                        return value;
                    const auto *number = std::get_if<double>(&*value.value);
                    if (!number || !subscriptIndex(*number, input->shape[position], selection.begin))
                        return success(0.0);
                    selection.end = selection.begin;
                }
            }
            selections[position] = selection;
        }

        const QVector<std::size_t> inputSteps = strides(input->shape);
        const bool sliced = std::any_of(selections.begin(), selections.end(), [](const Selection &selection) {
            return selection.preserve;
        });
        if (!sliced) {
            std::size_t flat = 0;
            for (std::size_t position = 0; position < selections.size(); ++position)
                flat += selections[position].begin * inputSteps[position];
            return success(input->values[flat]);
        }
        ArrayValue output;
        for (std::size_t position = 0; position < selections.size(); ++position) {
            if (!selections[position].preserve)
                continue;
            output.dimensions.push_back(input->dimensions[position]);
            output.shape.push_back(selections[position].end - selections[position].begin + 1);
        }
        for (std::size_t flat = 0; flat < input->values.size(); ++flat) {
            const QVector<std::size_t> inputCoordinates = coordinates(flat, input->shape);
            bool included = true;
            for (std::size_t position = 0; position < selections.size(); ++position) {
                const std::size_t coordinate = inputCoordinates[position];
                if (coordinate < selections[position].begin || coordinate > selections[position].end) {
                    included = false;
                    break;
                }
            }
            if (included)
                output.values.push_back(input->values[flat]);
        }
        if (elementCount(output.shape) != output.values.size())
            return failure(&node, "Invalid array slice");
        return success(std::move(output));
    }

    EvaluationResult arrayLiteral(const ArrayLiteralNode &node)
    {
        if (m_context.scope.dimensions.isEmpty())
            return failure(&node, "Array literal requires output dimensions");
        QVector<std::size_t> shape;
        for (const QString &name : m_context.scope.dimensions) {
            const Dimension *dimension = m_context.readDimension ? m_context.readDimension(name) : nullptr;
            if (!dimension)
                return failure(&node, "Unknown output dimension: " + name);
            shape.push_back(dimension->elements.size());
        }
        const std::size_t count = elementCount(shape);
        if (node.valueCount() != count)
            return failure(&node, "Array literal size does not match output dimensions");
        QVector<double> values;
        values.reserve(count);
        for (std::size_t position = 0; position < node.valueCount(); ++position) {
            const FormulaNode *item = node.value(position);
            EvaluationResult value = evaluate(item);
            if (!value.value)
                return value;
            const auto *scalar = std::get_if<double>(&*value.value);
            if (!scalar)
                return failure(item, "Array literal items must be scalar");
            values.push_back(*scalar);
        }
        if (!m_context.scope.indices.isEmpty()) {
            if (m_context.scope.indices.size() != shape.size())
                return failure(&node, "Array literal scope does not match output dimensions");
            const QVector<std::size_t> steps = strides(shape);
            std::size_t flat = 0;
            for (std::size_t index = 0; index < shape.size(); ++index) {
                if (m_context.scope.indices[index] >= shape[index])
                    return success(0.0);
                flat += m_context.scope.indices[index] * steps[index];
            }
            return success(values[flat]);
        }
        return success(ArrayValue {m_context.scope.dimensions, shape, std::move(values)});
    }

    template <typename Call>
    EvaluationResult reduce(const Call &node, const Value &input)
    {
        const ArrayValue *array = arrayOf(input);
        if (!array) {
            const double scalar = *std::get_if<double>(&input);
            if (node.nameKey() == "size")
                return success(1.0);
            if (node.nameKey() == "stddev")
                return success(0.0);
            return success(scalar);
        }
        if (node.nameKey() == "size")
            return success(static_cast<double>(array->values.size()));
        if (node.nameKey() == "sum")
            return success(std::accumulate(array->values.begin(), array->values.end(), 0.0));
        if (node.nameKey() == "mean")
            return success(std::accumulate(array->values.begin(), array->values.end(), 0.0) / array->values.size());
        if (node.nameKey() == "stddev") {
            const double mean = std::accumulate(array->values.begin(), array->values.end(), 0.0) / array->values.size();
            double sum = 0.0;
            for (double value : array->values)
                sum += (value - mean) * (value - mean);
            return success(std::sqrt(sum / array->values.size()));
        }
        if (node.nameKey() == "max")
            return success(*std::max_element(array->values.begin(), array->values.end()));
        if (node.nameKey() == "min")
            return success(*std::min_element(array->values.begin(), array->values.end()));
        return failure(&node, "Unknown array reduction: " + node.nameKey());
    }

    EvaluationResult rank(const FormulaNode &node, const QVector<Value> &arguments)
    {
        const auto *rankValue = std::get_if<double>(&arguments[1]);
        const ArrayValue *primary = arrayOf(arguments[0]);
        const std::size_t count = primary ? primary->values.size() : 1;
        std::size_t rankIndex = 0;
        if (!rankValue || !subscriptIndex(*rankValue, count, rankIndex))
            return success(0.0);
        const auto number = [&arguments](std::size_t index) {
            if (const ArrayValue *array = arrayOf(arguments[0]))
                return array->values[index];
            return *std::get_if<double>(&arguments[0]);
        };
        QVector<std::size_t> order(count);
        std::iota(order.begin(), order.end(), 0);
        EvaluationResult alignedSecondary;
        const ArrayValue *secondary = nullptr;
        if (arguments.size() == 3 && primary) {
            alignedSecondary = broadcast(arguments[2], primary->dimensions, node);
            if (!alignedSecondary.value)
                return alignedSecondary;
            secondary = arrayOf(*alignedSecondary.value);
        } else if (arguments.size() == 3) {
            secondary = arrayOf(arguments[2]);
        }
        if (arguments.size() == 3 && !std::get_if<double>(&arguments[2]) && (!secondary || secondary->values.size() != count))
            return failure(&node, "RANK arrays must have the same number of elements");
        std::sort(order.begin(), order.end(), [&](std::size_t left, std::size_t right) {
            if (number(left) != number(right))
                return number(left) < number(right);
            if (secondary && secondary->values[left] != secondary->values[right])
                return secondary->values[left] < secondary->values[right];
            return left < right;
        });
        return success(static_cast<double>(order[rankIndex] + 1));
    }

    template <typename Call, typename Definition>
    EvaluationResult invokeMacro(const Call &node, const Definition &definition)
    {
        ExecutionStack &stack = m_context.executionStack;
        if (stack.frameCount() >= ExecutionStack::maximumFrameDepth)
            return failure(&node, "Macro recursion depth limit exceeded");
        const ExecutionStackGuard guard(stack);
        const std::size_t base = stack.mark().values;
        for (int position = 0; position < node.argumentCount(); ++position) {
            EvaluationResult value = evaluate(node.argument(position));
            if (!value.value)
                return value;
            stack.pushValue(std::move(*value.value));
        }

        stack.pushFrame(base, &definition);
        for (int index = int(node.argumentCount()); index < definition.parameters().size(); ++index) {
            EvaluationResult evaluated = evaluate(definition.parameters()[index].defaultFormula().root());
            if (!evaluated.value)
                return evaluated;
            stack.pushValue(std::move(*evaluated.value));
        }
        return evaluate(definition.formula().root());
    }

    template <typename Call, typename Reader>
    EvaluationResult lookup(const Call &node, Reader evaluateOne)
    {
        EvaluationResult input = evaluate(node.argument(0));
        if (!input.value)
            return input;
        if (const auto *scalar = std::get_if<double>(&*input.value)) {
            const auto value = evaluateOne(*scalar);
            return value ? success(*value) : failure(&node, "Unknown function: " + node.nameKey());
        }
        ArrayValue output = *arrayOf(*input.value);
        for (double &value : output.values) {
            const auto evaluated = evaluateOne(value);
            if (!evaluated)
                return failure(&node, "Unknown function: " + node.nameKey());
            value = *evaluated;
        }
        return success(std::move(output));
    }

    template <typename Call, typename Operation>
    EvaluationResult timeFunction(const Call &node, Operation operation)
    {
        ExecutionStack &stack = m_context.executionStack;
        const ExecutionStackGuard guard(stack);
        const std::size_t base = stack.mark().values;
        QVector<QString> dimensions;
        for (int position = 0; position < node.argumentCount(); ++position) {
            EvaluationResult input = evaluate(node.argument(position));
            if (!input.value)
                return input;
            if (dimensions.isEmpty()) {
                if (const ArrayValue *array = arrayOf(*input.value))
                    dimensions = array->dimensions;
            }
            stack.pushValue(std::move(*input.value));
        }
        std::array<double, ExecutionStack::maximumTimeFunctionArguments> values {};
        if (dimensions.isEmpty()) {
            for (int position = 0; position < node.argumentCount(); ++position)
                values[position] = std::get<double>(stack.value(base + position));
            return success(operation(values));
        }
        for (int position = 0; position < node.argumentCount(); ++position) {
            EvaluationResult broadcasted = broadcast(stack.value(base + position), dimensions, node);
            if (!broadcasted.value)
                return broadcasted;
            stack.value(base + position) = std::move(*broadcasted.value);
        }
        const ArrayValue &first = *arrayOf(stack.value(base));
        ArrayValue output {first.dimensions, first.shape, QVector<double>(first.values.size())};
        for (int flat = 0; flat < output.values.size(); ++flat) {
            for (int position = 0; position < node.argumentCount(); ++position)
                values[position] = arrayOf(stack.value(base + position))->values[flat];
            output.values[flat] = operation(values);
        }
        return success(std::move(output));
    }

    EvaluationResult call(const CallNode &node)
    {
        if (const FunctionSignature *signature = findFunctionSignature(node.nameKey()))
            return builtin(node, *signature);
        if (m_context.readMacro) {
            const auto macro = m_context.readMacro(node.nameKey(), node.argumentCount(), 1);
            if (macro) {
                if (!macro->definition)
                    return failure(&node, "Invalid macro definition");
                return invokeMacro(node, *macro->definition);
            }
        }
        if (m_context.readLookup && node.argumentCount() == 1) {
            return lookup(node, [this, &node](double value) -> std::optional<double> {
                return m_context.readLookup(node.nameKey(), value);
            });
        }
        return failure(&node, "Unknown function: " + node.nameKey());
    }

    EvaluationResult boundCall(const BoundCallNode &node)
    {
        switch (node.callKind()) {
        case BoundCallKind::Builtin:
            return builtin(node, node.signature());
        case BoundCallKind::Macro:
            return node.macro() ? invokeMacro(node, *node.macro()) : failure(&node, "Invalid macro definition");
        case BoundCallKind::Lookup:
            if (!node.lookup())
                return failure(&node, "Invalid Lookup definition");
            return lookup(node, [&node](double value) -> std::optional<double> {
                return node.lookup()->evaluate(value);
            });
        }
        return failure(&node, "Unknown bound call");
    }

    template <typename Call>
    EvaluationResult builtin(const Call &node, const FunctionSignature &signature)
    {
        if (node.argumentCount() < signature.minArguments || node.argumentCount() > signature.maxArguments)
            return failure(&node, "Invalid argument count for function: " + node.nameKey());
        if (signature.category == FunctionCategory::Time || node.nameKey() == "pi" || node.nameKey() == "inf")
            return constant(node, node.nameKey());
        if (node.nameKey() == "if_then_else") {
            EvaluationResult condition = evaluate(node.argument(0));
            if (!condition.value)
                return condition;
            return select(*condition.value, node.argument(1), node.argument(2), node);
        }
        if (node.nameKey() == "step") {
            return timeFunction(node, [this](const std::array<double, ExecutionStack::maximumTimeFunctionArguments> &values) {
                return m_context.inputTime + m_context.inputDt / 2.0 > values[1] ? values[0] : 0.0;
            });
        }
        if (node.nameKey() == "pulse") {
            return timeFunction(node, [this](const std::array<double, ExecutionStack::maximumTimeFunctionArguments> &values) {
                const double width = values[1] == 0.0 ? m_context.inputDt : values[1];
                const double time = m_context.inputTime + m_context.inputDt / 2.0;
                return time > values[0] && time < values[0] + width ? 1.0 : 0.0;
            });
        }
        if (node.nameKey() == "pulse_train") {
            return timeFunction(node, [this](const std::array<double, ExecutionStack::maximumTimeFunctionArguments> &values) {
                const double time = m_context.inputTime + m_context.inputDt / 2.0;
                const double width = values[1] <= m_context.inputDt ? m_context.inputDt : values[1];
                if (time <= values[0] || time > values[3])
                    return 0.0;
                if (values[2] <= 0.0)
                    return time < values[0] + width ? 1.0 : 0.0;
                if (width >= values[2])
                    return 1.0;
                const double phase = std::fmod(time - values[0], values[2]);
                return phase >= 0.0 && phase < width ? 1.0 : 0.0;
            });
        }
        if (node.nameKey() == "ramp") {
            return timeFunction(node, [this](const std::array<double, ExecutionStack::maximumTimeFunctionArguments> &values) {
                if (m_context.inputTime <= values[1])
                    return 0.0;
                if (m_context.inputTime < values[2])
                    return values[0] * (m_context.inputTime - values[1]);
                return values[0] * (values[2] - values[1]);
            });
        }
        const bool arrayReduction = node.nameKey() == "sum" || node.nameKey() == "mean" || node.nameKey() == "stddev"
            || node.nameKey() == "size";
        if (signature.category != FunctionCategory::Mathematical && node.nameKey() != "log"
            && !arrayReduction && node.nameKey() != "rank")
            return failure(&node, "Function is not supported: " + node.nameKey());

        QVector<Value> arguments;
        arguments.reserve(node.argumentCount());
        for (std::size_t position = 0; position < node.argumentCount(); ++position) {
            EvaluationResult value = evaluate(node.argument(position));
            if (!value.value)
                return value;
            arguments.push_back(std::move(*value.value));
        }
        if (arrayReduction)
            return reduce(node, arguments[0]);
        if (node.nameKey() == "rank")
            return rank(node, arguments);
        if ((node.nameKey() == "max" || node.nameKey() == "min") && arguments.size() == 1 && arrayOf(arguments[0]))
            return reduce(node, arguments[0]);
        const double first = std::get_if<double>(&arguments[0]) ? *std::get_if<double>(&arguments[0]) : 0.0;
        if (node.nameKey() == "max" || node.nameKey() == "min") {
            const bool maximum = node.nameKey() == "max";
            return elementwise(arguments[0], arguments.size() == 1 ? arguments[0] : arguments[1], node,
                [maximum](double left, double right) { return maximum ? std::max(left, right) : std::min(left, right); });
        }
        return elementwise(arguments[0], node, [&node, first](double value) {
            if (node.nameKey() == "abs") return std::abs(value);
            if (node.nameKey() == "arccos") return std::acos(value);
            if (node.nameKey() == "arcsin") return std::asin(value);
            if (node.nameKey() == "arctan") return std::atan(value);
            if (node.nameKey() == "cos") return std::cos(value);
            if (node.nameKey() == "exp") return std::exp(value);
            if (node.nameKey() == "int") return std::floor(value);
            if (node.nameKey() == "ln" || node.nameKey() == "log") return std::log(value);
            if (node.nameKey() == "log10") return std::log10(value);
            if (node.nameKey() == "sin") return std::sin(value);
            if (node.nameKey() == "sqrt") return std::sqrt(value);
            if (node.nameKey() == "tan") return std::tan(value);
            return first;
        });
    }

    const EvaluationContext &m_context;
};

} // namespace

EvaluationResult evaluateFormula(const FormulaNode *node, const EvaluationContext &context)
{
    return Evaluator(context).evaluate(node);
}

} // namespace qds::core
