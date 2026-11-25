#pragma once

#include <qds/core/Model.h>

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace qds::core {

struct SourceSpan {
    std::size_t begin = 0;
    std::size_t end = 0;

    bool operator==(const SourceSpan &other) const { return begin == other.begin && end == other.end; }
    bool operator!=(const SourceSpan &other) const { return !(*this == other); }
};

enum class FormulaNodeKind {
    Number,
    Reference,
    Unary,
    Binary,
    Call,
    Conditional,
    Subscript,
    Wildcard,
    Range,
    DimensionPosition,
    ArrayLiteral
};

struct FormulaNode {
    explicit FormulaNode(FormulaNodeKind nodeKind, SourceSpan nodeSpan)
        : kind(nodeKind)
        , span(nodeSpan)
    {
    }
    virtual ~FormulaNode() = default;

    FormulaNodeKind kind;
    SourceSpan span;
};

struct NumberNode final : FormulaNode {
    NumberNode(double number, SourceSpan nodeSpan)
        : FormulaNode(FormulaNodeKind::Number, nodeSpan)
        , value(number)
    {
    }
    double value;
};

struct ReferenceNode final : FormulaNode {
    ReferenceNode(std::string key, SourceSpan nodeSpan)
        : FormulaNode(FormulaNodeKind::Reference, nodeSpan)
        , nameKey(std::move(key))
    {
    }
    std::string nameKey;
    EntityId entityId = 0;
};

enum class UnaryOperator { Positive, Negative, Not, Transpose };

struct UnaryNode final : FormulaNode {
    UnaryNode(UnaryOperator operation, std::unique_ptr<FormulaNode> child, SourceSpan nodeSpan)
        : FormulaNode(FormulaNodeKind::Unary, nodeSpan)
        , op(operation)
        , operand(std::move(child))
    {
    }
    UnaryOperator op;
    std::unique_ptr<FormulaNode> operand;
};

enum class BinaryOperator {
    Power,
    Multiply,
    Divide,
    Modulo,
    Add,
    Subtract,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    Equal,
    NotEqual,
    And,
    Or
};

struct BinaryNode final : FormulaNode {
    BinaryNode(BinaryOperator operation, std::unique_ptr<FormulaNode> lhs, std::unique_ptr<FormulaNode> rhs, SourceSpan nodeSpan)
        : FormulaNode(FormulaNodeKind::Binary, nodeSpan)
        , op(operation)
        , left(std::move(lhs))
        , right(std::move(rhs))
    {
    }
    BinaryOperator op;
    std::unique_ptr<FormulaNode> left;
    std::unique_ptr<FormulaNode> right;
};

struct CallNode final : FormulaNode {
    CallNode(std::string key, std::vector<std::unique_ptr<FormulaNode>> args, SourceSpan nodeSpan)
        : FormulaNode(FormulaNodeKind::Call, nodeSpan)
        , nameKey(std::move(key))
        , arguments(std::move(args))
    {
    }
    std::string nameKey;
    std::vector<std::unique_ptr<FormulaNode>> arguments;
};

struct ConditionalNode final : FormulaNode {
    ConditionalNode(std::unique_ptr<FormulaNode> ifCondition, std::unique_ptr<FormulaNode> thenValue,
        std::unique_ptr<FormulaNode> elseValue, SourceSpan nodeSpan)
        : FormulaNode(FormulaNodeKind::Conditional, nodeSpan)
        , condition(std::move(ifCondition))
        , thenExpression(std::move(thenValue))
        , elseExpression(std::move(elseValue))
    {
    }
    std::unique_ptr<FormulaNode> condition;
    std::unique_ptr<FormulaNode> thenExpression;
    std::unique_ptr<FormulaNode> elseExpression;
};

struct SubscriptNode final : FormulaNode {
    SubscriptNode(std::unique_ptr<FormulaNode> base, std::vector<std::unique_ptr<FormulaNode>> subscripts, SourceSpan nodeSpan)
        : FormulaNode(FormulaNodeKind::Subscript, nodeSpan)
        , target(std::move(base))
        , indices(std::move(subscripts))
    {
    }
    std::unique_ptr<FormulaNode> target;
    std::vector<std::unique_ptr<FormulaNode>> indices;
};

struct WildcardNode final : FormulaNode {
    explicit WildcardNode(SourceSpan nodeSpan)
        : FormulaNode(FormulaNodeKind::Wildcard, nodeSpan)
    {
    }
};

struct RangeNode final : FormulaNode {
    RangeNode(std::unique_ptr<FormulaNode> first, std::unique_ptr<FormulaNode> last, SourceSpan nodeSpan)
        : FormulaNode(FormulaNodeKind::Range, nodeSpan)
        , begin(std::move(first))
        , end(std::move(last))
    {
    }
    std::unique_ptr<FormulaNode> begin;
    std::unique_ptr<FormulaNode> end;
};

struct DimensionPositionNode final : FormulaNode {
    DimensionPositionNode(std::size_t dimensionPosition, SourceSpan nodeSpan)
        : FormulaNode(FormulaNodeKind::DimensionPosition, nodeSpan)
        , position(dimensionPosition)
    {
    }
    std::size_t position;
};

struct ArrayLiteralNode final : FormulaNode {
    ArrayLiteralNode(std::vector<std::unique_ptr<FormulaNode>> items, SourceSpan nodeSpan)
        : FormulaNode(FormulaNodeKind::ArrayLiteral, nodeSpan)
        , values(std::move(items))
    {
    }
    std::vector<std::unique_ptr<FormulaNode>> values;
};

struct FormulaIssue {
    SourceSpan span;
    std::string message;

    bool operator==(const FormulaIssue &other) const { return span == other.span && message == other.message; }
};

struct FormulaAnalysis {
    std::unique_ptr<FormulaNode> ast;
    std::vector<FormulaIssue> issues;
};

FormulaAnalysis analyzeFormula(std::string_view equation);
std::size_t formulaNodeCount(const FormulaNode *node);

} // namespace qds::core
