#pragma once

#include <cstddef>
#include <memory>
#include <QString>
#include <QVector>

#include <utility>
#include <vector>

namespace qds::core {

struct SourceSpan {
    int begin = 0;
    int end = 0;

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
    ArrayLiteral,
    BoundReference,
    LocalReference,
    BoundCall
};

class FormulaNode
{
public:
    FormulaNodeKind kind() const { return m_kind; }
    const SourceSpan &span() const { return m_span; }
    virtual ~FormulaNode() = default;

protected:
    FormulaNode(FormulaNodeKind kind, SourceSpan span)
        : m_kind(kind)
        , m_span(span)
    {
    }

private:
    FormulaNodeKind m_kind;
    SourceSpan m_span;
};

class NumberNode final : public FormulaNode
{
public:
    NumberNode(double number, SourceSpan span)
        : FormulaNode(FormulaNodeKind::Number, span)
        , m_value(number)
    {
    }

    double value() const { return m_value; }

private:
    double m_value;
};

class ReferenceNode final : public FormulaNode
{
public:
    ReferenceNode(QString nameKey, SourceSpan span)
        : FormulaNode(FormulaNodeKind::Reference, span)
        , m_nameKey(std::move(nameKey))
    {
    }

    const QString &nameKey() const { return m_nameKey; }

private:
    QString m_nameKey;
};

enum class UnaryOperator { Positive, Negative, Not, Transpose };

class UnaryNode final : public FormulaNode
{
public:
    UnaryNode(UnaryOperator operation, std::unique_ptr<FormulaNode> operand, SourceSpan span)
        : FormulaNode(FormulaNodeKind::Unary, span)
        , m_op(operation)
        , m_operand(std::move(operand))
    {
    }

    UnaryOperator op() const { return m_op; }
    const FormulaNode *operand() const { return m_operand.get(); }

private:
    UnaryOperator m_op;
    std::unique_ptr<FormulaNode> m_operand;
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

class BinaryNode final : public FormulaNode
{
public:
    BinaryNode(BinaryOperator operation, std::unique_ptr<FormulaNode> left, std::unique_ptr<FormulaNode> right, SourceSpan span)
        : FormulaNode(FormulaNodeKind::Binary, span)
        , m_op(operation)
        , m_left(std::move(left))
        , m_right(std::move(right))
    {
    }

    BinaryOperator op() const { return m_op; }
    const FormulaNode *left() const { return m_left.get(); }
    const FormulaNode *right() const { return m_right.get(); }

private:
    BinaryOperator m_op;
    std::unique_ptr<FormulaNode> m_left;
    std::unique_ptr<FormulaNode> m_right;
};

class CallNode final : public FormulaNode
{
public:
    CallNode(QString nameKey, std::vector<std::unique_ptr<FormulaNode>> arguments, SourceSpan span)
        : FormulaNode(FormulaNodeKind::Call, span)
        , m_nameKey(std::move(nameKey))
        , m_arguments(std::move(arguments))
    {
    }

    const QString &nameKey() const { return m_nameKey; }
    std::size_t argumentCount() const { return m_arguments.size(); }
    const FormulaNode *argument(std::size_t position) const { return m_arguments[position].get(); }

private:
    QString m_nameKey;
    std::vector<std::unique_ptr<FormulaNode>> m_arguments;
};

class ConditionalNode final : public FormulaNode
{
public:
    ConditionalNode(std::unique_ptr<FormulaNode> condition, std::unique_ptr<FormulaNode> thenExpression,
        std::unique_ptr<FormulaNode> elseExpression, SourceSpan span)
        : FormulaNode(FormulaNodeKind::Conditional, span)
        , m_condition(std::move(condition))
        , m_thenExpression(std::move(thenExpression))
        , m_elseExpression(std::move(elseExpression))
    {
    }

    const FormulaNode *condition() const { return m_condition.get(); }
    const FormulaNode *thenExpression() const { return m_thenExpression.get(); }
    const FormulaNode *elseExpression() const { return m_elseExpression.get(); }

private:
    std::unique_ptr<FormulaNode> m_condition;
    std::unique_ptr<FormulaNode> m_thenExpression;
    std::unique_ptr<FormulaNode> m_elseExpression;
};

class SubscriptNode final : public FormulaNode
{
public:
    SubscriptNode(std::unique_ptr<FormulaNode> target, std::vector<std::unique_ptr<FormulaNode>> indices, SourceSpan span)
        : FormulaNode(FormulaNodeKind::Subscript, span)
        , m_target(std::move(target))
        , m_indices(std::move(indices))
    {
    }

    const FormulaNode *target() const { return m_target.get(); }
    std::size_t indexCount() const { return m_indices.size(); }
    const FormulaNode *index(std::size_t position) const { return m_indices[position].get(); }

private:
    std::unique_ptr<FormulaNode> m_target;
    std::vector<std::unique_ptr<FormulaNode>> m_indices;
};

class WildcardNode final : public FormulaNode
{
public:
    explicit WildcardNode(SourceSpan span)
        : FormulaNode(FormulaNodeKind::Wildcard, span)
    {
    }
};

class RangeNode final : public FormulaNode
{
public:
    RangeNode(std::unique_ptr<FormulaNode> begin, std::unique_ptr<FormulaNode> end, SourceSpan span)
        : FormulaNode(FormulaNodeKind::Range, span)
        , m_begin(std::move(begin))
        , m_end(std::move(end))
    {
    }

    const FormulaNode *begin() const { return m_begin.get(); }
    const FormulaNode *end() const { return m_end.get(); }

private:
    std::unique_ptr<FormulaNode> m_begin;
    std::unique_ptr<FormulaNode> m_end;
};

class DimensionPositionNode final : public FormulaNode
{
public:
    DimensionPositionNode(std::size_t position, SourceSpan span)
        : FormulaNode(FormulaNodeKind::DimensionPosition, span)
        , m_position(position)
    {
    }

    std::size_t position() const { return m_position; }

private:
    std::size_t m_position;
};

class ArrayLiteralNode final : public FormulaNode
{
public:
    ArrayLiteralNode(std::vector<std::unique_ptr<FormulaNode>> values, SourceSpan span)
        : FormulaNode(FormulaNodeKind::ArrayLiteral, span)
        , m_values(std::move(values))
    {
    }

    std::size_t valueCount() const { return m_values.size(); }
    const FormulaNode *value(std::size_t position) const { return m_values[position].get(); }

private:
    std::vector<std::unique_ptr<FormulaNode>> m_values;
};

struct FormulaIssue {
    SourceSpan span;
    QString message;

    bool operator==(const FormulaIssue &other) const { return span == other.span && message == other.message; }
};

class Formula
{
public:
    Formula() = default;
    Formula(QString source, std::unique_ptr<FormulaNode> ast, QVector<FormulaIssue> issues)
        : m_source(std::move(source))
        , m_ast(std::move(ast))
        , m_issues(std::move(issues))
    {
    }

    const QString &source() const { return m_source; }
    const FormulaNode *root() const { return m_ast.get(); }
    const QVector<FormulaIssue> &issues() const { return m_issues; }

private:
    QString m_source;
    std::unique_ptr<FormulaNode> m_ast;
    QVector<FormulaIssue> m_issues;
};

Formula analyzeFormula(QString equation);
bool isConstantFormula(QString source);
std::size_t formulaNodeCount(const FormulaNode *node);

} // namespace qds::core
