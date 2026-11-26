#include <core/formula/Formula.h>

#include <core/Identifier.h>

#include <algorithm>
#include <limits>
#include <utility>

namespace qds::core {
namespace {

enum class TokenType {
    End,
    Invalid,
    Number,
    Identifier,
    Plus,
    Minus,
    Star,
    Slash,
    Caret,
    LeftParen,
    RightParen,
    LeftBracket,
    RightBracket,
    Comma,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    Equal,
    NotEqual,
    And,
    Or,
    Not,
    Mod,
    If,
    Then,
    Else,
    Apostrophe,
    Dot,
    Colon,
    At
};

struct Token {
    TokenType type = TokenType::End;
    QString text;
    SourceSpan span;
};

bool asciiDigit(QChar c)
{
    return c >= QChar('0') && c <= QChar('9');
}

bool identifierStart(QChar c)
{
    return (c >= QChar('A') && c <= QChar('Z')) || (c >= QChar('a') && c <= QChar('z'))
        || c == QChar('_') || c.unicode() >= 128;
}

bool identifierPart(QChar c)
{
    return identifierStart(c) || asciiDigit(c) || c == QChar('$');
}

bool space(QChar c)
{
    return c == QChar(' ') || c == QChar('\t') || c == QChar('\r') || c == QChar('\n')
        || c == QChar('\v') || c == QChar('\f');
}

QString upperAscii(QString value)
{
    for (int index = 0; index < value.size(); ++index) {
        const QChar c = value.at(index);
        if (c >= QChar('a') && c <= QChar('z'))
            value[index] = QChar(c.unicode() - QChar('a').unicode() + QChar('A').unicode());
    }
    return value;
}

class Lexer {
public:
    explicit Lexer(QString input)
        : m_input(input)
    {
    }

    Token next()
    {
        while (true) {
            skipWhitespace();
            if (m_position >= m_input.size())
                return {TokenType::End, {}, {m_position, m_position}};
            const QChar c = m_input.at(m_position);
            if (c == QChar('{')) {
                const int begin = m_position++;
                const int end = m_input.indexOf(QChar('}'), m_position);
                if (end < 0)
                    return {TokenType::Invalid, "Unterminated comment", {begin, m_input.size()}};
                m_position = end + 1;
                continue;
            }
            break;
        }

        const int begin = m_position;
        const QChar c = m_input.at(m_position);
        if (asciiDigit(c) || (c == QChar('.') && m_position + 1 < m_input.size() && asciiDigit(m_input.at(m_position + 1))))
            return number();
        if (identifierStart(c))
            return identifier();
        if (c == QChar('"'))
            return quotedIdentifier();

        ++m_position;
        const auto simple = [this, begin](TokenType type) {
            return Token {type, m_input.mid(begin, 1), {begin, begin + 1}};
        };
        switch (c.unicode()) {
        case '+': return simple(TokenType::Plus);
        case '-': return simple(TokenType::Minus);
        case '*': return simple(TokenType::Star);
        case '/': return simple(TokenType::Slash);
        case '^': return simple(TokenType::Caret);
        case '(': return simple(TokenType::LeftParen);
        case ')': return simple(TokenType::RightParen);
        case '[': return simple(TokenType::LeftBracket);
        case ']': return simple(TokenType::RightBracket);
        case ',': return simple(TokenType::Comma);
        case '=': return simple(TokenType::Equal);
        case '\'': return simple(TokenType::Apostrophe);
        case '.': return simple(TokenType::Dot);
        case ':': return simple(TokenType::Colon);
        case '@': return simple(TokenType::At);
        case '<':
            if (m_position < m_input.size() && m_input[m_position] == '=') {
                ++m_position;
                return {TokenType::LessEqual, "<=", {begin, m_position}};
            }
            if (m_position < m_input.size() && m_input[m_position] == '>') {
                ++m_position;
                return {TokenType::NotEqual, "<>", {begin, m_position}};
            }
            return simple(TokenType::Less);
        case '>':
            if (m_position < m_input.size() && m_input[m_position] == '=') {
                ++m_position;
                return {TokenType::GreaterEqual, ">=", {begin, m_position}};
            }
            return simple(TokenType::Greater);
        default:
            return {TokenType::Invalid, "Unexpected character", {begin, m_position}};
        }
    }

private:
    void skipWhitespace()
    {
        while (m_position < m_input.size() && space(m_input.at(m_position)))
            ++m_position;
    }

    Token number()
    {
        const int begin = m_position;
        bool digits = false;
        while (m_position < m_input.size() && asciiDigit(m_input.at(m_position))) {
            digits = true;
            ++m_position;
        }
        if (m_position < m_input.size() && m_input.at(m_position) == QChar('.')) {
            ++m_position;
            while (m_position < m_input.size() && asciiDigit(m_input.at(m_position))) {
                digits = true;
                ++m_position;
            }
        }
        if (m_position < m_input.size() && (m_input.at(m_position) == QChar('e') || m_input.at(m_position) == QChar('E'))) {
            ++m_position;
            if (m_position < m_input.size() && (m_input.at(m_position) == QChar('+') || m_input.at(m_position) == QChar('-')))
                ++m_position;
            const int exponent = m_position;
            while (m_position < m_input.size() && asciiDigit(m_input.at(m_position)))
                ++m_position;
            if (m_position == exponent)
                digits = false;
        }
        const QString text = m_input.mid(begin, m_position - begin);
        bool valid = false;
        text.toDouble(&valid);
        const bool followedByIdentifier = m_position < m_input.size() && identifierStart(m_input.at(m_position));
        const bool followedByDotDigit = m_position + 1 < m_input.size() && m_input.at(m_position) == QChar('.')
            && asciiDigit(m_input.at(m_position + 1));
        if (!digits || !valid || followedByIdentifier || followedByDotDigit)
            return {TokenType::Invalid, "Invalid numeric literal", {begin, m_position}};
        return {TokenType::Number, text, {begin, m_position}};
    }

    Token identifier()
    {
        const int begin = m_position++;
        while (m_position < m_input.size() && identifierPart(m_input.at(m_position)))
            ++m_position;
        const QString text = m_input.mid(begin, m_position - begin);
        const QString keyword = upperAscii(text);
        const SourceSpan span {begin, m_position};
        if (keyword == "AND") return {TokenType::And, text, span};
        if (keyword == "OR") return {TokenType::Or, text, span};
        if (keyword == "NOT") return {TokenType::Not, text, span};
        if (keyword == "MOD") return {TokenType::Mod, text, span};
        if (keyword == "IF") return {TokenType::If, text, span};
        if (keyword == "THEN") return {TokenType::Then, text, span};
        if (keyword == "ELSE") return {TokenType::Else, text, span};
        return {TokenType::Identifier, text, span};
    }

    Token quotedIdentifier()
    {
        const int begin = m_position++;
        QString decoded;
        bool terminated = false;
        while (m_position < m_input.size()) {
            const QChar c = m_input.at(m_position++);
            if (c == QChar('"')) {
                terminated = true;
                break;
            }
            if (c == QChar('\\') && m_position < m_input.size()) {
                const QChar escaped = m_input.at(m_position++);
                decoded.push_back(escaped == QChar('n') ? QChar('\n') : escaped);
            } else {
                decoded.push_back(c);
            }
        }
        if (!terminated)
            return {TokenType::Invalid, "Unterminated quoted identifier", {begin, m_input.size()}};
        if (decoded.isEmpty())
            return {TokenType::Invalid, "Empty identifier", {begin, m_position}};
        return {TokenType::Identifier, decoded, {begin, m_position}};
    }

    QString m_input;
    int m_position = 0;
};

class Parser {
public:
    explicit Parser(QString equation)
        : m_equation(std::move(equation))
        , m_lexer(m_equation)
    {
        advance();
    }

    Formula parse()
    {
        if (m_current.type == TokenType::End)
            return Formula(m_equation, nullptr, {});
        auto ast = expression(0);
        if (m_current.type == TokenType::Comma && ast) {
            const int begin = ast->span().begin;
            int end = ast->span().end;
            std::vector<std::unique_ptr<FormulaNode>> values;
            values.push_back(std::move(ast));
            while (m_current.type == TokenType::Comma) {
                consume();
                auto value = expression(0);
                if (!value)
                    break;
                end = value->span().end;
                values.push_back(std::move(value));
            }
            ast = std::make_unique<ArrayLiteralNode>(std::move(values), SourceSpan {begin, end});
        }
        if (m_current.type != TokenType::End)
            fail("Unexpected token");
        return Formula(m_equation, std::move(ast), std::move(m_issues));
    }

private:
    void advance()
    {
        m_current = m_lexer.next();
    }

    Token consume()
    {
        Token token = m_current;
        advance();
        return token;
    }

    void fail(const QString &message)
    {
        m_issues.push_back({m_current.span, message});
    }

    bool expect(TokenType type, const QString &message)
    {
        if (m_current.type != type) {
            fail(message);
            return false;
        }
        advance();
        return true;
    }

    static int precedence(TokenType type)
    {
        switch (type) {
        case TokenType::Or: return 1;
        case TokenType::And: return 2;
        case TokenType::Equal:
        case TokenType::NotEqual: return 3;
        case TokenType::Less:
        case TokenType::LessEqual:
        case TokenType::Greater:
        case TokenType::GreaterEqual: return 4;
        case TokenType::Plus:
        case TokenType::Minus: return 5;
        case TokenType::Star:
        case TokenType::Slash:
        case TokenType::Mod: return 6;
        case TokenType::Caret: return 8;
        default: return -1;
        }
    }

    static BinaryOperator binaryOperator(TokenType type)
    {
        switch (type) {
        case TokenType::Caret: return BinaryOperator::Power;
        case TokenType::Star: return BinaryOperator::Multiply;
        case TokenType::Slash: return BinaryOperator::Divide;
        case TokenType::Mod: return BinaryOperator::Modulo;
        case TokenType::Plus: return BinaryOperator::Add;
        case TokenType::Minus: return BinaryOperator::Subtract;
        case TokenType::Less: return BinaryOperator::Less;
        case TokenType::LessEqual: return BinaryOperator::LessEqual;
        case TokenType::Greater: return BinaryOperator::Greater;
        case TokenType::GreaterEqual: return BinaryOperator::GreaterEqual;
        case TokenType::Equal: return BinaryOperator::Equal;
        case TokenType::NotEqual: return BinaryOperator::NotEqual;
        case TokenType::And: return BinaryOperator::And;
        default: return BinaryOperator::Or;
        }
    }

    std::unique_ptr<FormulaNode> expression(int minimumPrecedence)
    {
        auto left = unary();
        if (!left)
            return nullptr;
        while (true) {
            const TokenType type = m_current.type;
            const int currentPrecedence = precedence(type);
            if (currentPrecedence < minimumPrecedence)
                break;
            consume();
            auto right = expression(type == TokenType::Caret ? currentPrecedence : currentPrecedence + 1);
            if (!right)
                return left;
            const SourceSpan span {left->span().begin, right->span().end};
            left = std::make_unique<BinaryNode>(binaryOperator(type), std::move(left), std::move(right), span);
        }
        return left;
    }

    std::unique_ptr<FormulaNode> unary()
    {
        const TokenType type = m_current.type;
        if (type != TokenType::Plus && type != TokenType::Minus && type != TokenType::Not)
            return postfix();
        const Token operation = consume();
        auto operand = expression(7);
        if (!operand)
            return nullptr;
        const UnaryOperator op = type == TokenType::Plus ? UnaryOperator::Positive
            : type == TokenType::Minus ? UnaryOperator::Negative : UnaryOperator::Not;
        return std::make_unique<UnaryNode>(op, std::move(operand), SourceSpan {operation.span.begin, operand->span().end});
    }

    std::unique_ptr<FormulaNode> postfix()
    {
        auto target = primary();
        if (!target)
            return nullptr;
        while (m_current.type == TokenType::LeftBracket || m_current.type == TokenType::Apostrophe) {
            if (m_current.type == TokenType::Apostrophe) {
                const Token apostrophe = consume();
                target = std::make_unique<UnaryNode>(UnaryOperator::Transpose, std::move(target),
                    SourceSpan {target->span().begin, apostrophe.span.end});
                continue;
            }
            consume();
            std::vector<std::unique_ptr<FormulaNode>> indices;
            if (m_current.type != TokenType::RightBracket) {
                while (true) {
                    auto index = subscriptExpression();
                    if (!index)
                        break;
                    indices.push_back(std::move(index));
                    if (m_current.type != TokenType::Comma)
                        break;
                    consume();
                }
            }
            if (indices.empty())
                fail("Expected subscript expression");
            if (!expect(TokenType::RightBracket, "Expected closing subscript bracket"))
                return target;
            target = std::make_unique<SubscriptNode>(std::move(target), std::move(indices),
                SourceSpan {target->span().begin, m_current.span.begin});
        }
        return target;
    }

    std::unique_ptr<FormulaNode> subscriptExpression()
    {
        if (m_current.type == TokenType::Star) {
            const Token wildcard = consume();
            return std::make_unique<WildcardNode>(wildcard.span);
        }
        if (m_current.type == TokenType::At) {
            const Token at = consume();
            if (m_current.type != TokenType::Number) {
                fail("Expected dimension position");
                return nullptr;
            }
            const Token number = consume();
            const bool digits = !number.text.isEmpty()
                && std::all_of(number.text.begin(), number.text.end(), [](QChar c) { return asciiDigit(c); });
            const unsigned long long position = digits ? number.text.toULongLong() : 0;
            if (position == 0) {
                fail("Dimension position must be a positive integer");
                return nullptr;
            }
            return std::make_unique<DimensionPositionNode>(static_cast<std::size_t>(position),
                SourceSpan {at.span.begin, number.span.end});
        }
        auto begin = expression(0);
        if (!begin || m_current.type != TokenType::Colon)
            return begin;
        consume();
        auto end = expression(0);
        if (!end)
            return begin;
        const SourceSpan span {begin->span().begin, end->span().end};
        return std::make_unique<RangeNode>(std::move(begin), std::move(end), span);
    }

    std::unique_ptr<FormulaNode> primary()
    {
        if (m_current.type == TokenType::Number) {
            const Token number = consume();
            return std::make_unique<NumberNode>(number.text.toDouble(), number.span);
        }
        if (m_current.type == TokenType::Identifier)
            return identifierOrCall();
        if (m_current.type == TokenType::LeftParen) {
            consume();
            auto node = expression(0);
            if (!expect(TokenType::RightParen, "Expected closing parenthesis"))
                return node;
            return node;
        }
        if (m_current.type == TokenType::If)
            return conditional();
        if (m_current.type == TokenType::Invalid) {
            fail(m_current.text);
            consume();
            return nullptr;
        }
        fail("Expected expression");
        return nullptr;
    }

    std::unique_ptr<FormulaNode> identifierOrCall()
    {
        const Token identifier = consume();
        QString qualified = identifier.text;
        while (m_current.type == TokenType::Dot) {
            consume();
            if (m_current.type != TokenType::Identifier) {
                fail("Expected namespace identifier");
                return nullptr;
            }
            qualified += "." + consume().text;
        }
        const QString key = normalizeIdentifier(qualified);
        if (m_current.type != TokenType::LeftParen)
            return std::make_unique<ReferenceNode>(key, identifier.span);

        const Token open = consume();
        std::vector<std::unique_ptr<FormulaNode>> arguments;
        if (m_current.type != TokenType::RightParen) {
            while (true) {
                auto argument = expression(0);
                if (!argument)
                    break;
                arguments.push_back(std::move(argument));
                if (m_current.type != TokenType::Comma)
                    break;
                consume();
            }
        }
        if (!expect(TokenType::RightParen, "Expected closing function parenthesis"))
            return nullptr;
        return std::make_unique<CallNode>(key, std::move(arguments), SourceSpan {identifier.span.begin, m_current.span.begin});
    }

    std::unique_ptr<FormulaNode> conditional()
    {
        const Token ifToken = consume();
        auto condition = expression(0);
        if (!condition || !expect(TokenType::Then, "Expected THEN"))
            return condition;
        auto thenExpression = expression(0);
        if (!thenExpression || !expect(TokenType::Else, "Expected ELSE"))
            return thenExpression;
        auto elseExpression = expression(0);
        if (!elseExpression)
            return thenExpression;
        return std::make_unique<ConditionalNode>(std::move(condition), std::move(thenExpression),
            std::move(elseExpression), SourceSpan {ifToken.span.begin, elseExpression->span().end});
    }

    QString m_equation;
    Lexer m_lexer;
    Token m_current;
    QVector<FormulaIssue> m_issues;
};

} // namespace

Formula analyzeFormula(QString equation)
{
    Parser parser(equation);
    return parser.parse();
}

std::size_t formulaNodeCount(const FormulaNode *node)
{
    if (!node)
        return 0;
    std::size_t count = 1;
    if (const auto *unary = dynamic_cast<const UnaryNode *>(node)) {
        count += formulaNodeCount(unary->operand());
    } else if (const auto *binary = dynamic_cast<const BinaryNode *>(node)) {
        count += formulaNodeCount(binary->left()) + formulaNodeCount(binary->right());
    } else if (const auto *call = dynamic_cast<const CallNode *>(node)) {
        for (std::size_t index = 0; index < call->argumentCount(); ++index)
            count += formulaNodeCount(call->argument(index));
    } else if (const auto *conditional = dynamic_cast<const ConditionalNode *>(node)) {
        count += formulaNodeCount(conditional->condition())
            + formulaNodeCount(conditional->thenExpression())
            + formulaNodeCount(conditional->elseExpression());
    } else if (const auto *subscript = dynamic_cast<const SubscriptNode *>(node)) {
        count += formulaNodeCount(subscript->target());
        for (std::size_t position = 0; position < subscript->indexCount(); ++position)
            count += formulaNodeCount(subscript->index(position));
    } else if (const auto *range = dynamic_cast<const RangeNode *>(node)) {
        count += formulaNodeCount(range->begin()) + formulaNodeCount(range->end());
    } else if (const auto *literal = dynamic_cast<const ArrayLiteralNode *>(node)) {
        for (std::size_t position = 0; position < literal->valueCount(); ++position)
            count += formulaNodeCount(literal->value(position));
    }
    return count;
}

} // namespace qds::core
