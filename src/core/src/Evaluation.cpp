#include <qds/core/Evaluation.h>

#include <qds/core/Function.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace qds::core {
namespace {

EvaluationResult success(double value)
{
    return {value, {}, {}};
}

EvaluationResult failure(const FormulaNode *node, std::string message)
{
    return {std::nullopt, std::move(message), node ? node->span : SourceSpan {}};
}

bool truth(double value)
{
    return value != 0.0;
}

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
        switch (node->kind) {
        case FormulaNodeKind::Number:
            return success(static_cast<const NumberNode *>(node)->value);
        case FormulaNodeKind::Reference:
            return reference(*static_cast<const ReferenceNode *>(node));
        case FormulaNodeKind::Unary:
            return unary(*static_cast<const UnaryNode *>(node));
        case FormulaNodeKind::Binary:
            return binary(*static_cast<const BinaryNode *>(node));
        case FormulaNodeKind::Call:
            return call(*static_cast<const CallNode *>(node));
        case FormulaNodeKind::Conditional:
            return conditional(*static_cast<const ConditionalNode *>(node));
        case FormulaNodeKind::Subscript:
            return failure(node, "Subscript evaluation is not supported");
        }
        return failure(node, "Unknown formula node");
    }

private:
    EvaluationResult reference(const ReferenceNode &node)
    {
        if (const FunctionSignature *signature = findFunctionSignature(node.nameKey);
            signature && signature->minArguments == 0 && signature->maxArguments == 0) {
            return constant(node, node.nameKey);
        }
        if (node.entityId == 0)
            return failure(&node, "Unresolved variable: " + node.nameKey);
        if (!m_context.readVariable)
            return failure(&node, "No variable reader for: " + node.nameKey);
        const std::optional<double> value = m_context.readVariable(node.entityId);
        if (!value)
            return failure(&node, "Variable has no value: " + node.nameKey);
        return success(*value);
    }

    EvaluationResult constant(const FormulaNode &node, const std::string &nameKey)
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

    EvaluationResult unary(const UnaryNode &node)
    {
        if (node.op == UnaryOperator::Transpose)
            return failure(&node, "Transpose evaluation is not supported");
        EvaluationResult operand = evaluate(node.operand.get());
        if (!operand.value)
            return operand;
        switch (node.op) {
        case UnaryOperator::Positive:
            return operand;
        case UnaryOperator::Negative:
            return success(-*operand.value);
        case UnaryOperator::Not:
            return success(truth(*operand.value) ? 0.0 : 1.0);
        case UnaryOperator::Transpose:
            break;
        }
        return failure(&node, "Unknown unary operator");
    }

    EvaluationResult binary(const BinaryNode &node)
    {
        EvaluationResult left = evaluate(node.left.get());
        if (!left.value)
            return left;
        if (node.op == BinaryOperator::And && !truth(*left.value))
            return success(0.0);
        if (node.op == BinaryOperator::Or && truth(*left.value))
            return success(1.0);
        EvaluationResult right = evaluate(node.right.get());
        if (!right.value)
            return right;
        const double lhs = *left.value;
        const double rhs = *right.value;
        switch (node.op) {
        case BinaryOperator::Power:
            return success(std::pow(lhs, rhs));
        case BinaryOperator::Multiply:
            return success(lhs * rhs);
        case BinaryOperator::Divide:
            return success(lhs / rhs);
        case BinaryOperator::Modulo: {
            double value = std::fmod(lhs, rhs);
            if (value != 0.0 && (value < 0.0) != (rhs < 0.0))
                value += rhs;
            return success(value);
        }
        case BinaryOperator::Add:
            return success(lhs + rhs);
        case BinaryOperator::Subtract:
            return success(lhs - rhs);
        case BinaryOperator::Less:
            return success(lhs < rhs ? 1.0 : 0.0);
        case BinaryOperator::LessEqual:
            return success(lhs <= rhs ? 1.0 : 0.0);
        case BinaryOperator::Greater:
            return success(lhs > rhs ? 1.0 : 0.0);
        case BinaryOperator::GreaterEqual:
            return success(lhs >= rhs ? 1.0 : 0.0);
        case BinaryOperator::Equal:
            return success(lhs == rhs ? 1.0 : 0.0);
        case BinaryOperator::NotEqual:
            return success(lhs != rhs ? 1.0 : 0.0);
        case BinaryOperator::And:
        case BinaryOperator::Or:
            return success(truth(rhs) ? 1.0 : 0.0);
        }
        return failure(&node, "Unknown binary operator");
    }

    EvaluationResult conditional(const ConditionalNode &node)
    {
        EvaluationResult condition = evaluate(node.condition.get());
        if (!condition.value)
            return condition;
        return evaluate(truth(*condition.value) ? node.thenExpression.get() : node.elseExpression.get());
    }

    EvaluationResult call(const CallNode &node)
    {
        const FunctionSignature *signature = findFunctionSignature(node.nameKey);
        if (!signature)
            return failure(&node, "Function is not supported: " + node.nameKey);
        if (node.arguments.size() < signature->minArguments || node.arguments.size() > signature->maxArguments)
            return failure(&node, "Invalid argument count for function: " + node.nameKey);
        if (signature->category == FunctionCategory::Time || node.nameKey == "pi" || node.nameKey == "inf")
            return constant(node, node.nameKey);
        if (node.nameKey == "if_then_else") {
            EvaluationResult condition = evaluate(node.arguments[0].get());
            if (!condition.value)
                return condition;
            return evaluate(truth(*condition.value) ? node.arguments[1].get() : node.arguments[2].get());
        }
        if (signature->category != FunctionCategory::Mathematical && node.nameKey != "log")
            return failure(&node, "Function is not supported: " + node.nameKey);

        std::vector<double> arguments;
        arguments.reserve(node.arguments.size());
        for (const auto &argument : node.arguments) {
            EvaluationResult value = evaluate(argument.get());
            if (!value.value)
                return value;
            arguments.push_back(*value.value);
        }
        const double first = arguments.empty() ? 0.0 : arguments[0];
        if (node.nameKey == "abs")
            return success(std::abs(first));
        if (node.nameKey == "arccos")
            return success(std::acos(first));
        if (node.nameKey == "arcsin")
            return success(std::asin(first));
        if (node.nameKey == "arctan")
            return success(std::atan(first));
        if (node.nameKey == "cos")
            return success(std::cos(first));
        if (node.nameKey == "exp")
            return success(std::exp(first));
        if (node.nameKey == "int")
            return success(std::floor(first));
        if (node.nameKey == "ln" || node.nameKey == "log")
            return success(std::log(first));
        if (node.nameKey == "log10")
            return success(std::log10(first));
        if (node.nameKey == "max")
            return success(arguments.size() == 1 ? first : std::max(first, arguments[1]));
        if (node.nameKey == "min")
            return success(arguments.size() == 1 ? first : std::min(first, arguments[1]));
        if (node.nameKey == "sin")
            return success(std::sin(first));
        if (node.nameKey == "sqrt")
            return success(std::sqrt(first));
        if (node.nameKey == "tan")
            return success(std::tan(first));
        return failure(&node, "Function is not supported: " + node.nameKey);
    }

    const EvaluationContext &m_context;
};

} // namespace

EvaluationResult evaluateFormula(const FormulaNode *node, const EvaluationContext &context)
{
    return Evaluator(context).evaluate(node);
}

} // namespace qds::core
