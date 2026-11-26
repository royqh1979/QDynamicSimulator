#include <core/formula/Formula.h>

#include <core/Identifier.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
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
    std::string text;
    SourceSpan span;
};

bool identifierStart(unsigned char c)
{
    return std::isalpha(c) || c == '_' || c >= 128;
}

bool identifierPart(unsigned char c)
{
    return std::isalnum(c) || c == '_' || c == '$' || c >= 128;
}

std::string upperAscii(std::string value)
{
    for (char &c : value)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return value;
}

class Lexer {
public:
    explicit Lexer(std::string_view input)
        : m_input(input)
    {
    }

    Token next()
    {
        while (true) {
            skipWhitespace();
            if (m_position >= m_input.size())
                return {TokenType::End, {}, {m_position, m_position}};
            const unsigned char c = static_cast<unsigned char>(m_input[m_position]);
            if (c == '{') {
                const std::size_t begin = m_position++;
                const std::size_t end = m_input.find('}', m_position);
                if (end == std::string_view::npos)
                    return {TokenType::Invalid, "Unterminated comment", {begin, m_input.size()}};
                m_position = end + 1;
                continue;
            }
            break;
        }

        const std::size_t begin = m_position;
        const unsigned char c = static_cast<unsigned char>(m_input[m_position]);
        if (std::isdigit(c) || (c == '.' && m_position + 1 < m_input.size() && std::isdigit(static_cast<unsigned char>(m_input[m_position + 1]))))
            return number();
        if (identifierStart(c))
            return identifier();
        if (c == '"')
            return quotedIdentifier();

        ++m_position;
        const auto simple = [this, begin](TokenType type) {
            return Token {type, std::string(m_input.substr(begin, 1)), {begin, begin + 1}};
        };
        switch (c) {
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
        while (m_position < m_input.size() && std::isspace(static_cast<unsigned char>(m_input[m_position])))
            ++m_position;
    }

    Token number()
    {
        const std::size_t begin = m_position;
        const std::string remaining(m_input.substr(m_position));
        char *end = nullptr;
        std::strtod(remaining.c_str(), &end);
        const std::size_t length = static_cast<std::size_t>(end - remaining.c_str());
        m_position += length;
        const bool followedByIdentifier = m_position < m_input.size()
            && identifierStart(static_cast<unsigned char>(m_input[m_position]));
        const bool followedByDotDigit = m_position + 1 < m_input.size() && m_input[m_position] == '.'
            && std::isdigit(static_cast<unsigned char>(m_input[m_position + 1]));
        if (length == 0 || followedByIdentifier || followedByDotDigit)
            return {TokenType::Invalid, "Invalid numeric literal", {begin, m_position}};
        return {TokenType::Number, remaining.substr(0, length), {begin, m_position}};
    }

    Token identifier()
    {
        const std::size_t begin = m_position++;
        while (m_position < m_input.size() && identifierPart(static_cast<unsigned char>(m_input[m_position])))
            ++m_position;
        const std::string text(m_input.substr(begin, m_position - begin));
        const std::string keyword = upperAscii(text);
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
        const std::size_t begin = m_position++;
        std::string decoded;
        bool terminated = false;
        while (m_position < m_input.size()) {
            const char c = m_input[m_position++];
            if (c == '"') {
                terminated = true;
                break;
            }
            if (c == '\\' && m_position < m_input.size()) {
                const char escaped = m_input[m_position++];
                decoded.push_back(escaped == 'n' ? '\n' : escaped);
            } else {
                decoded.push_back(c);
            }
        }
        if (!terminated)
            return {TokenType::Invalid, "Unterminated quoted identifier", {begin, m_input.size()}};
        if (decoded.empty())
            return {TokenType::Invalid, "Empty identifier", {begin, m_position}};
        return {TokenType::Identifier, decoded, {begin, m_position}};
    }

    std::string_view m_input;
    std::size_t m_position = 0;
};

class Parser {
public:
    explicit Parser(std::string_view equation)
        : m_lexer(equation)
    {
        advance();
    }

    FormulaAnalysis parse()
    {
        FormulaAnalysis result;
        if (m_current.type == TokenType::End)
            return result;
        result.ast = expression(0);
        if (m_current.type == TokenType::Comma && result.ast) {
            const std::size_t begin = result.ast->span.begin;
            std::size_t end = result.ast->span.end;
            std::vector<std::unique_ptr<FormulaNode>> values;
            values.push_back(std::move(result.ast));
            while (m_current.type == TokenType::Comma) {
                consume();
                auto value = expression(0);
                if (!value)
                    break;
                end = value->span.end;
                values.push_back(std::move(value));
            }
            result.ast = std::make_unique<ArrayLiteralNode>(std::move(values), SourceSpan {begin, end});
        }
        if (m_current.type != TokenType::End)
            fail("Unexpected token");
        result.issues = std::move(m_issues);
        return result;
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

    void fail(const std::string &message)
    {
        m_issues.push_back({m_current.span, message});
    }

    bool expect(TokenType type, const std::string &message)
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
            const SourceSpan span {left->span.begin, right->span.end};
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
        return std::make_unique<UnaryNode>(op, std::move(operand), SourceSpan {operation.span.begin, operand->span.end});
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
                    SourceSpan {target->span.begin, apostrophe.span.end});
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
                SourceSpan {target->span.begin, m_current.span.begin});
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
            const bool digits = !number.text.empty()
                && std::all_of(number.text.begin(), number.text.end(), [](unsigned char c) { return std::isdigit(c); });
            const unsigned long long position = digits ? std::strtoull(number.text.c_str(), nullptr, 10) : 0;
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
        const SourceSpan span {begin->span.begin, end->span.end};
        return std::make_unique<RangeNode>(std::move(begin), std::move(end), span);
    }

    std::unique_ptr<FormulaNode> primary()
    {
        if (m_current.type == TokenType::Number) {
            const Token number = consume();
            return std::make_unique<NumberNode>(std::strtod(number.text.c_str(), nullptr), number.span);
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
        std::string qualified = identifier.text;
        while (m_current.type == TokenType::Dot) {
            consume();
            if (m_current.type != TokenType::Identifier) {
                fail("Expected namespace identifier");
                return nullptr;
            }
            qualified += "." + consume().text;
        }
        const std::string key = normalizeIdentifier(qualified);
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
            std::move(elseExpression), SourceSpan {ifToken.span.begin, elseExpression->span.end});
    }

    Lexer m_lexer;
    Token m_current;
    std::vector<FormulaIssue> m_issues;
};

} // namespace

FormulaAnalysis analyzeFormula(std::string_view equation)
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
        count += formulaNodeCount(unary->operand.get());
    } else if (const auto *binary = dynamic_cast<const BinaryNode *>(node)) {
        count += formulaNodeCount(binary->left.get()) + formulaNodeCount(binary->right.get());
    } else if (const auto *call = dynamic_cast<const CallNode *>(node)) {
        for (const auto &argument : call->arguments)
            count += formulaNodeCount(argument.get());
    } else if (const auto *conditional = dynamic_cast<const ConditionalNode *>(node)) {
        count += formulaNodeCount(conditional->condition.get())
            + formulaNodeCount(conditional->thenExpression.get())
            + formulaNodeCount(conditional->elseExpression.get());
    } else if (const auto *subscript = dynamic_cast<const SubscriptNode *>(node)) {
        count += formulaNodeCount(subscript->target.get());
        for (const auto &index : subscript->indices)
            count += formulaNodeCount(index.get());
    } else if (const auto *range = dynamic_cast<const RangeNode *>(node)) {
        count += formulaNodeCount(range->begin.get()) + formulaNodeCount(range->end.get());
    } else if (const auto *literal = dynamic_cast<const ArrayLiteralNode *>(node)) {
        for (const auto &value : literal->values)
            count += formulaNodeCount(value.get());
    }
    return count;
}

} // namespace qds::core
