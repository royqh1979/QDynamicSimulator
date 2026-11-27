#include <io/MdlFile.h>

#include <core/Identifier.h>

#include <QFile>
#include <QHash>
#include <QPointF>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

namespace qds::io {
namespace {

using namespace qds::core;

struct VariableDef {
    ElementData data;
    QString baseName;
    QVector<QString> subscripts;
    QStringList netExpressions;
    bool hasElementEquations = false;
};

struct LayoutDef {
    int type = 0;
    int objectId = 0;
    int fromId = 0;
    int toId = 0;
    int bits = 0;
    EntityId entityId = 0;
    QString name;
    QString text;
    QPointF position;
    QList<QPointF> points;
};

struct NetTerm {
    QString nameKey;
    int sign = 1;
};

struct FlowConnection {
    EntityId stock = 0;
    int sign = 1;
};

struct Assignment {
    QString left;
    QString right;
    QString operation;
};

struct Symbol {
    QString displayName;
    QString nameKey;
};

void setError(QString *errorMessage, const QString &message)
{
    if (errorMessage)
        *errorMessage = message;
}

bool fail(QString *errorMessage, const QString &message)
{
    setError(errorMessage, message);
    return false;
}

void addWarning(QVector<ModelIssue> *warnings, const QString &code, const QString &message)
{
    if (warnings)
        warnings->push_back({0, IssueSeverity::Warning, message, code, {}});
}

bool identifierStart(QChar c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c.unicode() >= 128;
}

bool identifierPart(QChar c)
{
    return identifierStart(c) || (c >= '0' && c <= '9') || c == '$';
}

bool nameSeparator(QChar c)
{
    return c == '_' || c.isSpace();
}

QString joinContinuations(const QString &text)
{
    QString result;
    result.reserve(text.size());
    for (int index = 0; index < text.size(); ++index) {
        if (text.at(index) == '\\' && index + 1 < text.size()) {
            int next = index + 1;
            if (text.at(next) == '\r')
                ++next;
            if (next < text.size() && text.at(next) == '\n') {
                index = next;
                if (!result.isEmpty() && !result.back().isSpace())
                    result += ' ';
                continue;
            }
        }
        result += text.at(index);
    }
    return result;
}

QStringList splitOutside(const QString &text, QChar delimiter)
{
    QStringList result;
    QString current;
    QChar quote;
    int braces = 0;
    int parentheses = 0;
    int brackets = 0;
    for (int index = 0; index < text.size(); ++index) {
        const QChar c = text.at(index);
        if (c == '\\') {
            current += c;
            if (index + 1 < text.size())
                current += text.at(++index);
            continue;
        }
        if (!quote.isNull()) {
            current += c;
            if (c == quote)
                quote = QChar();
            continue;
        }
        if (c == '"') {
            quote = c;
            current += c;
            continue;
        }
        if (c == '{')
            ++braces;
        else if (c == '}' && braces > 0)
            --braces;
        else if (c == '(')
            ++parentheses;
        else if (c == ')' && parentheses > 0)
            --parentheses;
        else if (c == '[')
            ++brackets;
        else if (c == ']' && brackets > 0)
            --brackets;
        if (c == delimiter && braces == 0 && parentheses == 0 && brackets == 0) {
            result.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    result.push_back(current);
    return result;
}

int findOutside(const QString &text, QChar needle)
{
    QChar quote;
    int braces = 0;
    int parentheses = 0;
    int brackets = 0;
    for (int index = 0; index < text.size(); ++index) {
        const QChar c = text.at(index);
        if (c == '\\') {
            ++index;
            continue;
        }
        if (!quote.isNull()) {
            if (c == quote)
                quote = QChar();
            continue;
        }
        if (c == '"') {
            quote = c;
            continue;
        }
        if (c == needle && braces == 0 && parentheses == 0 && brackets == 0)
            return index;
        if (c == '{')
            ++braces;
        else if (c == '}' && braces > 0)
            --braces;
        else if (c == '(')
            ++parentheses;
        else if (c == ')' && parentheses > 0)
            --parentheses;
        else if (c == '[')
            ++brackets;
        else if (c == ']' && brackets > 0)
            --brackets;
    }
    return -1;
}

QString stripBraceComments(const QString &text)
{
    QString result;
    QChar quote;
    int depth = 0;
    for (int index = 0; index < text.size(); ++index) {
        const QChar c = text.at(index);
        if (c == '\\') {
            if (depth == 0)
                result += c;
            if (index + 1 < text.size()) {
                if (depth == 0)
                    result += text.at(index + 1);
                ++index;
            }
            continue;
        }
        if (!quote.isNull()) {
            if (depth == 0)
                result += c;
            if (c == quote)
                quote = QChar();
            continue;
        }
        if (c == '"') {
            quote = c;
            if (depth == 0)
                result += c;
            continue;
        }
        if (c == '{') {
            ++depth;
            continue;
        }
        if (c == '}' && depth > 0) {
            --depth;
            continue;
        }
        if (depth == 0)
            result += c;
    }
    return result;
}

QString cleanExpression(const QString &text)
{
    return stripBraceComments(joinContinuations(text)).trimmed();
}

QString decodeIdentifier(const QString &name)
{
    QString result;
    for (int index = 0; index < name.size(); ++index) {
        if (name.at(index) == '\\' && index + 1 < name.size()) {
            const QChar escaped = name.at(++index);
            result += escaped == 'n' ? QChar('\n') : escaped;
        } else {
            result += name.at(index);
        }
    }
    return result;
}

QString unquote(const QString &name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.size() >= 2 && trimmed.front() == '"' && trimmed.back() == '"')
        return decodeIdentifier(trimmed.mid(1, trimmed.size() - 2));
    return trimmed;
}

QString quoteIdentifier(const QString &name)
{
    bool plain = !name.isEmpty() && identifierStart(name.front()) && !name.contains(QRegularExpression(QStringLiteral("\\s")));
    for (const QChar c : name)
        plain = plain && identifierPart(c);
    if (plain)
        return name;
    QString quoted = "\"";
    for (const QChar c : name) {
        if (c == '\\' || c == '"')
            quoted += '\\';
        if (c == '\n')
            quoted += "\\n";
        else
            quoted += c;
    }
    quoted += '"';
    return quoted;
}

bool matchSymbolAt(const QString &text, int position, const QString &name, int &end)
{
    int source = position;
    for (int index = 0; index < name.size();) {
        if (nameSeparator(name.at(index))) {
            while (index < name.size() && nameSeparator(name.at(index)))
                ++index;
            if (source >= text.size() || !nameSeparator(text.at(source)))
                return false;
            while (source < text.size() && nameSeparator(text.at(source)))
                ++source;
            continue;
        }
        if (source >= text.size() || text.at(source).toCaseFolded() != name.at(index).toCaseFolded())
            return false;
        ++source;
        ++index;
    }
    if (source < text.size() && identifierPart(text.at(source)))
        return false;
    end = source;
    return source > position;
}

QVector<Symbol> sortedSymbols(QVector<Symbol> symbols)
{
    std::sort(symbols.begin(), symbols.end(), [](const Symbol &left, const Symbol &right) {
        return left.displayName.size() != right.displayName.size()
            ? left.displayName.size() > right.displayName.size() : left.nameKey < right.nameKey;
    });
    return symbols;
}

QString normalizeExpression(const QString &expression, const QVector<Symbol> &symbols, QString *errorMessage)
{
    QString result;
    for (int index = 0; index < expression.size();) {
        const QChar c = expression.at(index);
        if (c == '"') {
            ++index;
            QString decoded;
            bool closed = false;
            while (index < expression.size()) {
                if (expression.at(index) == '\\' && index + 1 < expression.size()) {
                    decoded += expression.at(index++);
                    decoded += expression.at(index++);
                } else if (expression.at(index) == '"') {
                    ++index;
                    closed = true;
                    break;
                } else {
                    decoded += expression.at(index++);
                }
            }
            if (!closed) {
                setError(errorMessage, QStringLiteral("Unterminated quoted identifier in equation"));
                return {};
            }
            result += quoteIdentifier(unquote(decoded));
            continue;
        }
        if (c == '\'') {
            const int begin = index++;
            while (index < expression.size()) {
                if (expression.at(index) == '\\' && index + 1 < expression.size())
                    index += 2;
                else if (expression.at(index++) == '\'')
                    break;
            }
            result += expression.mid(begin, index - begin);
            continue;
        }
        if (c == ':') {
            static const QHash<QString, QString> operators {
                {QStringLiteral(":and:"), QStringLiteral("AND")},
                {QStringLiteral(":or:"), QStringLiteral("OR")},
                {QStringLiteral(":not:"), QStringLiteral("NOT")}
            };
            bool replaced = false;
            for (auto it = operators.constBegin(); it != operators.constEnd(); ++it) {
                if (expression.mid(index, it.key().size()).compare(it.key(), Qt::CaseInsensitive) == 0) {
                    result += it.value();
                    index += it.key().size();
                    replaced = true;
                    break;
                }
            }
            if (replaced)
                continue;
        }
        if (identifierStart(c) && (index == 0 || !identifierPart(expression.at(index - 1)))) {
            static const Symbol ifThenElse {QStringLiteral("IF THEN ELSE"), QStringLiteral("if_then_else")};
            int end = index;
            if (matchSymbolAt(expression, index, ifThenElse.displayName, end)) {
                result += ifThenElse.nameKey;
                index = end;
                continue;
            }
            const Symbol *matched = nullptr;
            int matchedEnd = index;
            for (const Symbol &symbol : symbols) {
                if (matchSymbolAt(expression, index, symbol.displayName, end)) {
                    matched = &symbol;
                    matchedEnd = end;
                    break;
                }
            }
            if (matched) {
                result += quoteIdentifier(matched->displayName);
                index = matchedEnd;
                continue;
            }
        }
        result += c;
        ++index;
    }
    return result;
}

bool normalizeInPlace(QString &equation, const QVector<Symbol> &symbols, QString *errorMessage)
{
    const QString converted = normalizeExpression(equation, symbols, errorMessage);
    if (converted.isEmpty() && !equation.trimmed().isEmpty())
        return false;
    const Formula formula = analyzeFormula(converted);
    if (!formula.issues().isEmpty())
        return fail(errorMessage, QStringLiteral("Invalid equation after MDL conversion: %1")
            .arg(formula.issues().front().message));
    equation = converted;
    return true;
}

Assignment parseAssignment(const QString &text)
{
    QChar quote;
    int brackets = 0;
    for (int index = 0; index < text.size(); ++index) {
        const QChar c = text.at(index);
        if (c == '\\') {
            ++index;
            continue;
        }
        if (!quote.isNull()) {
            if (c == quote)
                quote = QChar();
            continue;
        }
        if (c == '"') {
            quote = c;
            continue;
        }
        if (c == '[')
            ++brackets;
        else if (c == ']' && brackets > 0)
            --brackets;
        if (brackets != 0)
            continue;
        if (c == ':' && index + 1 < text.size() && text.at(index + 1) == '=') {
            return {text.left(index).trimmed(), text.mid(index + 2).trimmed(), QStringLiteral(":=")};
        }
        if (c == '=') {
            const bool unchangeable = index + 1 < text.size() && text.at(index + 1) == '=';
            return {text.left(index).trimmed(), text.mid(index + (unchangeable ? 2 : 1)).trimmed(),
                unchangeable ? QStringLiteral("==") : QStringLiteral("=")};
        }
    }
    return {};
}

bool parseSubscript(const QString &left, QString &base, QVector<QString> &subscripts, QString *errorMessage)
{
    const int open = findOutside(left, '[');
    if (open < 0) {
        base = unquote(left);
        return !base.isEmpty();
    }
    if (!left.endsWith(']'))
        return fail(errorMessage, QStringLiteral("Invalid subscript declaration: %1").arg(left));
    base = unquote(left.left(open));
    const QString body = left.mid(open + 1, left.size() - open - 2);
    if (body.contains(QStringLiteral(":EXCEPT:"), Qt::CaseInsensitive))
        return fail(errorMessage, QStringLiteral("Subscript exceptions are not supported: %1").arg(left));
    for (const QString &part : splitOutside(body, ',')) {
        const QString subscript = part.trimmed();
        if (subscript.isEmpty())
            return fail(errorMessage, QStringLiteral("Empty subscript: %1").arg(left));
        subscripts.push_back(unquote(subscript));
    }
    return !base.isEmpty() && !subscripts.isEmpty();
}

bool parseInteg(const QString &expression, QString &net, QString &initial)
{
    const int open = findOutside(expression, '(');
    if (open < 0 || !expression.endsWith(')'))
        return false;
    const QStringList arguments = splitOutside(expression.mid(open + 1, expression.size() - open - 2), ',');
    if (arguments.size() != 2)
        return false;
    net = cleanExpression(arguments[0]);
    initial = cleanExpression(arguments[1]);
    return !net.isEmpty() && !initial.isEmpty();
}

QVector<NetTerm> netTerms(const QString &expression)
{
    QVector<NetTerm> result;
    int sign = 1;
    QChar quote;
    int brackets = 0;
    int begin = 0;
    const auto appendTerm = [&result, &sign](const QString &term) {
        const QString trimmed = term.trimmed();
        if (trimmed.isEmpty())
            return false;
        QString base;
        QVector<QString> subscripts;
        if (!parseSubscript(trimmed, base, subscripts, nullptr) || base.isEmpty())
            return false;
        if (!identifierStart(base.front()) && base.front() != '"')
            return false;
        result.push_back({normalizeIdentifier(base), sign});
        return true;
    };
    for (int index = 0; index <= expression.size(); ++index) {
        const QChar c = index < expression.size() ? expression.at(index) : QChar();
        if (index < expression.size() && c == '\\') {
            ++index;
            continue;
        }
        if (!quote.isNull()) {
            if (c == quote)
                quote = QChar();
            continue;
        }
        if (c == '"') {
            quote = c;
            continue;
        }
        if (c == '[')
            ++brackets;
        else if (c == ']' && brackets > 0)
            --brackets;
        if (brackets == 0 && (c == '*' || c == '/' || c == '(' || c == ')'))
            return {};
        if (index == expression.size() || (brackets == 0 && (c == '+' || c == '-'))) {
            const QString term = expression.mid(begin, index - begin);
            if (term.trimmed().isEmpty()) {
                if (index == expression.size())
                    return {};
                sign = c == '-' ? -sign : sign;
            } else {
                if (!appendTerm(term))
                    return {};
                sign = c == '-' ? -1 : 1;
            }
            begin = index + 1;
        }
    }
    return result;
}

Vec2 corePoint(const QPointF &point)
{
    return {point.x(), point.y()};
}

QPointF normalFor(const QPointF &from, const QPointF &to)
{
    const QPointF delta = to - from;
    const double length = std::sqrt(delta.x() * delta.x() + delta.y() * delta.y());
    return length > 1e-9 ? QPointF(-delta.y() / length, delta.x() / length) : QPointF();
}

Route makeRoute(const QPointF &from, const QPointF &to, const QList<QPointF> &points)
{
    Route route;
    route.manual = true;
    route.mode = points.size() == 1 ? RouteMode::Bezier : RouteMode::Straight;
    const QPointF delta = to - from;
    const double lengthSquared = delta.x() * delta.x() + delta.y() * delta.y();
    if (lengthSquared < 1e-12)
        return route;
    const QPointF normal = normalFor(from, to);
    for (const QPointF &point : points) {
        const QPointF offset = point - from;
        route.controlPoints.push_back({QPointF::dotProduct(offset, delta) / lengthSquared,
            QPointF::dotProduct(offset, normal), false});
    }
    return route;
}

QList<QPointF> parsePoints(const QString &text)
{
    QList<QPointF> result;
    const QRegularExpression pattern(QStringLiteral("\\((-?\\d+(?:\\.\\d+)?),(-?\\d+(?:\\.\\d+)?)\\)"));
    QRegularExpressionMatchIterator matches = pattern.globalMatch(text);
    while (matches.hasNext()) {
        const auto match = matches.next();
        result.push_back(QPointF(match.captured(1).toDouble(), match.captured(2).toDouble()));
    }
    return result;
}

QString unitAndRange(const QString &metadata, bool &hasRange)
{
    const int bracket = findOutside(metadata, '[');
    hasRange = bracket >= 0;
    return bracket >= 0 ? metadata.left(bracket).trimmed() : metadata.trimmed();
}

bool parseVariableRecord(const QString &record, VariableDef &definition, bool &stock, bool &hasRange,
    QString *errorMessage)
{
    const QStringList parts = splitOutside(record, '~');
    const Assignment assignment = parseAssignment(parts.value(0));
    if (assignment.left.isEmpty() || assignment.operation.isEmpty())
        return fail(errorMessage, QStringLiteral("Invalid equation assignment"));
    const QString right = cleanExpression(assignment.right);
    if (right.isEmpty() && assignment.operation != QStringLiteral(":="))
        return fail(errorMessage, QStringLiteral("Empty equation: %1").arg(assignment.left));
    stock = right.startsWith(QStringLiteral("INTEG"), Qt::CaseInsensitive)
        && right.mid(5).trimmed().startsWith('(');
    if (!parseSubscript(assignment.left, definition.baseName, definition.subscripts, errorMessage))
        return false;
    definition.data.displayName = definition.baseName;
    definition.data.dimensions = definition.subscripts;
    definition.data.kind = stock ? ElementKind::Stock : ElementKind::Variable;
    if (parts.size() > 1)
        definition.data.unit = unitAndRange(parts[1], hasRange);
    if (parts.size() > 2)
        definition.data.comment = parts.mid(2).join(QStringLiteral("~")).trimmed();
    if (stock) {
        QString net;
        if (!parseInteg(right, net, definition.data.initialEquation))
            return fail(errorMessage, QStringLiteral("Invalid INTEG definition: %1").arg(definition.baseName));
        definition.netExpressions.push_back(net);
        definition.data.equation = net;
    } else {
        definition.data.equation = right;
    }
    return true;
}

bool expandRangeElement(const QString &element, QStringList &expanded)
{
    const QString value = element.trimmed();
    if (!value.startsWith('(') || !value.endsWith(')')) {
        expanded.push_back(unquote(value));
        return !value.isEmpty();
    }
    const QString body = value.mid(1, value.size() - 2);
    const int dash = body.indexOf('-');
    if (dash <= 0)
        return false;
    const QString first = body.left(dash).trimmed();
    const QString last = body.mid(dash + 1).trimmed();
    const int firstNumber = first.indexOf(QRegularExpression(QStringLiteral("\\d+$")));
    const int lastNumber = last.indexOf(QRegularExpression(QStringLiteral("\\d+$")));
    if (firstNumber < 0 || lastNumber < 0 || first.left(firstNumber) != last.left(lastNumber))
        return false;
    bool firstOk = false;
    bool lastOk = false;
    const int begin = first.mid(firstNumber).toInt(&firstOk);
    const int end = last.mid(lastNumber).toInt(&lastOk);
    if (!firstOk || !lastOk || end < begin)
        return false;
    const QString prefix = first.left(firstNumber);
    for (int number = begin; number <= end; ++number)
        expanded.push_back(prefix + QString::number(number));
    return true;
}

QString indexKey(const QVector<std::size_t> &indices)
{
    QString result;
    for (const std::size_t index : indices)
        result += QString::number(index) + ',';
    return result;
}

bool expandLiteral(const QString &expression, const Document &document, VariableDef &definition,
    const QVector<std::optional<std::size_t>> &fixed, QString *errorMessage)
{
    QStringList rows = splitOutside(expression, ';');
    while (!rows.isEmpty() && rows.back().trimmed().isEmpty())
        rows.removeLast();
    QVector<QStringList> valuesByRow;
    QStringList flat;
    bool literal = rows.size() > 1;
    for (const QString &row : rows) {
        const QStringList values = splitOutside(row, ',');
        if (values.size() > 1)
            literal = true;
        QStringList cleaned;
        for (const QString &value : values) {
            if (value.trimmed().isEmpty())
                return fail(errorMessage, QStringLiteral("Empty array literal value: %1").arg(definition.baseName));
            cleaned.push_back(value.trimmed());
            flat.push_back(value.trimmed());
        }
        valuesByRow.push_back(cleaned);
    }
    const bool anyFixed = std::any_of(fixed.begin(), fixed.end(), [](const auto &index) { return index.has_value(); });
    if (!literal) {
        if (!anyFixed)
            return true;
        if (fixed.size() != definition.data.dimensions.size())
            return false;
    }
    if (definition.data.dimensions.isEmpty())
        return fail(errorMessage, QStringLiteral("Array literal on scalar variable: %1").arg(definition.baseName));

    QVector<std::size_t> sizes;
    std::size_t expected = 1;
    int fixedCount = 0;
    for (int position = 0; position < definition.data.dimensions.size(); ++position) {
        const Dimension *dimension = document.findDimension(definition.data.dimensions[position]);
        if (!dimension)
            return fail(errorMessage, QStringLiteral("Unknown dimension: %1").arg(definition.data.dimensions[position]));
        sizes.push_back(dimension->elements.size());
        if (!fixed[position])
            expected *= sizes.back();
        else
            ++fixedCount;
    }
    const int varying = static_cast<int>(fixed.size()) - fixedCount;
    if (literal) {
        if (flat.size() != expected)
            return fail(errorMessage, QStringLiteral("Array literal size mismatch: %1").arg(definition.baseName));
        if (varying == 2) {
            int firstVarying = 0;
            int secondVarying = 1;
            for (int index = 0; index < fixed.size(); ++index) {
                if (!fixed[index]) {
                    firstVarying = index;
                    break;
                }
            }
            for (int index = firstVarying + 1; index < fixed.size(); ++index) {
                if (!fixed[index]) {
                    secondVarying = index;
                    break;
                }
            }
            if (valuesByRow.size() != static_cast<int>(sizes[firstVarying]))
                return fail(errorMessage, QStringLiteral("Array literal row count mismatch: %1").arg(definition.baseName));
            for (const QStringList &row : valuesByRow) {
                if (row.size() != static_cast<int>(sizes[secondVarying]))
                    return fail(errorMessage, QStringLiteral("Array literal column count mismatch: %1").arg(definition.baseName));
            }
        }
    }

    const QString equation = definition.data.kind == ElementKind::Stock
        ? definition.data.initialEquation : definition.data.equation;
    for (std::size_t linear = 0; linear < expected; ++linear) {
        QVector<std::size_t> indices;
        std::size_t remainder = linear;
        indices.resize(fixed.size());
        for (int position = fixed.size() - 1; position >= 0; --position) {
            if (fixed[position]) {
                indices[position] = *fixed[position];
            } else {
                indices[position] = remainder % sizes[position];
                remainder /= sizes[position];
            }
        }
        definition.data.elementEquations.push_back({indices, literal ? flat[linear] : equation});
    }
    definition.hasElementEquations = true;
    definition.data.equation.clear();
    definition.data.initialEquation.clear();
    return true;
}

bool mapSubscripts(const Document &document, VariableDef &definition, QString *errorMessage)
{
    definition.data.dimensions.clear();
    if (definition.subscripts.isEmpty()) {
        if (splitOutside(definition.data.equation, ',').size() > 1
            || splitOutside(definition.data.equation, ';').size() > 1)
            return fail(errorMessage, QStringLiteral("Array literal on scalar variable: %1").arg(definition.baseName));
        return true;
    }
    QVector<std::optional<std::size_t>> fixed;
    fixed.resize(definition.subscripts.size());
    for (int position = 0; position < definition.subscripts.size(); ++position) {
        const QString &subscript = definition.subscripts[position];
        if (const Dimension *dimension = document.findDimension(subscript)) {
            definition.data.dimensions.push_back(dimension->displayName);
            continue;
        }
        bool found = false;
        for (const Dimension &dimension : document.dimensions()) {
            for (int index = 0; index < dimension.elements.size(); ++index) {
                if (normalizeIdentifier(dimension.elements[index].displayName) == normalizeIdentifier(subscript)) {
                    definition.data.dimensions.push_back(dimension.displayName);
                    fixed[position] = static_cast<std::size_t>(index);
                    found = true;
                    break;
                }
            }
            if (found)
                break;
        }
        if (!found)
            return fail(errorMessage, QStringLiteral("Unknown subscript element: %1").arg(subscript));
    }
    return expandLiteral(definition.data.kind == ElementKind::Stock
            ? definition.data.initialEquation : definition.data.equation,
        document, definition, fixed, errorMessage);
}

bool mergeDefinitions(QVector<VariableDef> &definitions, QString *errorMessage)
{
    QVector<VariableDef> merged;
    QHash<QString, int> indices;
    for (VariableDef &definition : definitions) {
        const QString key = normalizeIdentifier(definition.baseName);
        const auto found = indices.constFind(key);
        if (found == indices.constEnd()) {
            indices.insert(key, merged.size());
            merged.push_back(std::move(definition));
            continue;
        }
        VariableDef &target = merged[*found];
        if (target.data.kind != definition.data.kind || target.data.dimensions != definition.data.dimensions
            || target.hasElementEquations != definition.hasElementEquations) {
            return fail(errorMessage, QStringLiteral("Incompatible multiple definitions: %1").arg(definition.baseName));
        }
        if (!target.hasElementEquations)
            return fail(errorMessage, QStringLiteral("Duplicate apply-to-all definition: %1").arg(definition.baseName));
        QSet<QString> occupied;
        for (const ArrayElementEquation &entry : target.data.elementEquations)
            occupied.insert(indexKey(entry.indices));
        for (const ArrayElementEquation &entry : definition.data.elementEquations) {
            if (occupied.contains(indexKey(entry.indices)))
                return fail(errorMessage, QStringLiteral("Duplicate array element definition: %1").arg(definition.baseName));
            occupied.insert(indexKey(entry.indices));
            target.data.elementEquations.push_back(entry);
        }
        if (target.data.unit.isEmpty())
            target.data.unit = definition.data.unit;
        if (target.data.comment.isEmpty())
            target.data.comment = definition.data.comment;
        target.netExpressions += definition.netExpressions;
    }
    definitions = std::move(merged);
    return true;
}

bool restoreElement(Document &document, const ElementData &data, QHash<QString, EntityId> &ids, QString *errorMessage)
{
    const QString name = data.displayName;
    if (!document.restore(data))
        return fail(errorMessage, QStringLiteral("Cannot restore model element: %1").arg(name));
    ids.insert(normalizeIdentifier(name), document.findByName(name)->id());
    return true;
}

QString uniqueFlowName(const Document &document, const QString &base)
{
    QString candidate = base + QStringLiteral(" net flow");
    for (int suffix = 2; document.findByName(candidate); ++suffix)
        candidate = base + QStringLiteral(" net flow ") + QString::number(suffix);
    return candidate;
}

bool parseSketchObject(const QString &line, LayoutDef &layout)
{
    const QStringList parts = splitOutside(line, ',');
    if (parts.size() < 2)
        return false;
    bool typeOk = false;
    bool idOk = false;
    layout.type = parts[0].toInt(&typeOk);
    layout.objectId = parts[1].toInt(&idOk);
    if (!typeOk || !idOk)
        return false;
    if (layout.type == 1) {
        if (parts.size() < 4)
            return false;
        layout.fromId = parts[2].toInt();
        layout.toId = parts[3].toInt();
        const int split = line.indexOf('|');
        layout.points = parsePoints(split >= 0 ? line.mid(split + 1) : QString());
        return true;
    }
    if (parts.size() < 9)
        return false;
    layout.name = unquote(parts[2]);
    layout.position = QPointF(parts[3].toDouble(), parts[4].toDouble());
    layout.bits = parts[8].toInt();
    return true;
}

bool importSketch(const QString &sketch, Document &document, const QHash<QString, EntityId> &ids,
    QVector<ModelIssue> *warnings, QString *errorMessage)
{
    const QStringList lines = sketch.split('\n');
    QHash<int, LayoutDef> objects;
    bool finished = false;
    bool viewSeen = false;
    bool ignoreAdditionalView = false;
    bool additionalViewWarned = false;
    for (int index = 0; index < lines.size(); ++index) {
        const QString line = lines[index].trimmed();
        if (line.startsWith(QStringLiteral("///---\\\\\\"))) {
            finished = true;
            continue;
        }
        if (line.startsWith(QStringLiteral("\\\\\\---///")) && viewSeen)
            ignoreAdditionalView = true;
        if (line.startsWith(QStringLiteral("*View"))) {
            if (viewSeen)
                ignoreAdditionalView = true;
            viewSeen = true;
        }
        if (ignoreAdditionalView && !additionalViewWarned) {
            addWarning(warnings, QStringLiteral("mdl-additional-view-ignored"),
                QStringLiteral("Only the first MDL sketch view is imported"));
            additionalViewWarned = true;
        }
        if (finished || ignoreAdditionalView || line.isEmpty() || line.startsWith(QStringLiteral("\\\\\\---///"))
            || line.startsWith(QStringLiteral("V300")) || line.startsWith('$') || line.startsWith(':')
            || line.startsWith(QStringLiteral("*View")))
            continue;
        LayoutDef layout;
        if (!parseSketchObject(line, layout))
            continue;
        if (layout.type == 12 && (layout.bits & 4) != 0 && index + 1 < lines.size())
            layout.text = lines[++index].trimmed();
        objects.insert(layout.objectId, layout);
    }

    for (LayoutDef &layout : objects) {
        if (layout.type == 10) {
            const EntityId id = ids.value(normalizeIdentifier(layout.name));
            layout.entityId = id;
            const DocumentElement *element = document.find(id);
            if (element && element->kind() != ElementKind::Flow)
                document.setPosition(id, corePoint(layout.position));
        } else if (layout.type == 12) {
            if ((layout.bits & 8) != 0) {
                addWarning(warnings, QStringLiteral("mdl-io-object-ignored"),
                    QStringLiteral("Sketch I/O objects are not imported"));
            } else if (layout.name != QStringLiteral("48")) {
                layout.entityId = document.createComment(layout.text, corePoint(layout.position));
            }
        } else if (layout.type != 1 && layout.type != 11) {
            addWarning(warnings, QStringLiteral("mdl-sketch-object-ignored"),
                QStringLiteral("Unsupported sketch object type: %1").arg(layout.type));
        }
    }

    for (LayoutDef &valve : objects) {
        if (valve.type != 11)
            continue;
        double nearest = std::numeric_limits<double>::max();
        for (const LayoutDef &label : objects) {
            if (label.type != 10 || label.entityId == 0)
                continue;
            const DocumentElement *element = document.find(label.entityId);
            if (!element || element->kind() != ElementKind::Flow)
                continue;
            const QPointF delta = label.position - valve.position;
            const double distance = delta.x() * delta.x() + delta.y() * delta.y();
            if (distance < nearest) {
                nearest = distance;
                valve.entityId = label.entityId;
            }
        }
    }
    for (const LayoutDef &arrow : objects) {
        if (arrow.type != 1)
            continue;
        LayoutDef &source = objects[arrow.fromId];
        LayoutDef &target = objects[arrow.toId];
        LayoutDef *valve = source.type == 11 ? &source : target.type == 11 ? &target : nullptr;
        LayoutDef *cloud = source.type == 12 && source.name == QStringLiteral("48") ? &source
            : target.type == 12 && target.name == QStringLiteral("48") ? &target : nullptr;
        if (!valve || !cloud || cloud->entityId != 0)
            continue;
        const auto *flow = dynamic_cast<const Flow *>(document.find(valve->entityId));
        if (!flow)
            continue;
        const EntityId endpoint = document.find(flow->endpoints().from)->kind() == ElementKind::Cloud
            ? flow->endpoints().from : flow->endpoints().to;
        if (document.find(endpoint)->kind() == ElementKind::Cloud) {
            cloud->entityId = endpoint;
            document.setPosition(endpoint, corePoint(cloud->position));
        }
    }
    for (LayoutDef &layout : objects) {
        if (layout.type == 12 && layout.name == QStringLiteral("48") && layout.entityId == 0)
            layout.entityId = document.createCloud(corePoint(layout.position));
        if (layout.type == 11 && layout.entityId != 0)
            document.setPosition(layout.entityId, corePoint(layout.position));
    }

    QSet<QString> connectors;
    for (const LayoutDef &arrow : objects) {
        if (arrow.type != 1)
            continue;
        const auto sourceObject = objects.constFind(arrow.fromId);
        const auto targetObject = objects.constFind(arrow.toId);
        if (sourceObject == objects.constEnd() || targetObject == objects.constEnd()) {
            addWarning(warnings, QStringLiteral("mdl-arrow-endpoint-missing"),
                QStringLiteral("Cannot resolve sketch arrow endpoint"));
            continue;
        }
        const EntityId from = sourceObject->entityId;
        const EntityId to = targetObject->entityId;
        if (from == 0 || to == 0) {
            addWarning(warnings, QStringLiteral("mdl-arrow-endpoint-missing"),
                QStringLiteral("Cannot resolve sketch arrow endpoint"));
            continue;
        }
        const DocumentElement *fromElement = document.find(from);
        const DocumentElement *toElement = document.find(to);
        if (!fromElement || !toElement)
            continue;
        const bool targetStock = toElement->kind() == ElementKind::Stock;
        const Flow *flow = dynamic_cast<const Flow *>(fromElement);
        if (flow && ((targetStock && (flow->endpoints().from == to || flow->endpoints().to == to))
                || fromElement->kind() == ElementKind::Cloud || toElement->kind() == ElementKind::Cloud))
            continue;
        const QString key = QString::number(from) + ':' + QString::number(to);
        if (connectors.contains(key))
            continue;
        ElementData connector;
        connector.kind = ElementKind::Connector;
        connector.from = from;
        connector.to = to;
        connector.route = makeRoute(sourceObject->position, targetObject->position, arrow.points);
        if (!document.restore(connector))
            return fail(errorMessage, QStringLiteral("Cannot restore sketch connector"));
        connectors.insert(key);
    }
    return true;
}

} // namespace

bool MdlFile::importText(const QString &text, Document &document, QString *errorMessage, QVector<ModelIssue> *warnings)
{
    if (warnings)
        warnings->clear();
    const QString sketchMarker = QStringLiteral("\\\\\\---///");
    const int sketchPosition = text.indexOf(sketchMarker);
    QString body = sketchPosition >= 0 ? text.left(sketchPosition) : text;
    if (body.startsWith(QStringLiteral("{UTF-8}")))
        body.remove(0, QStringLiteral("{UTF-8}").size());
    const QString sketch = sketchPosition >= 0 ? text.mid(sketchPosition) : QString();
    QStringList records = splitOutside(body, '|');
    Document parsed;
    QVector<VariableDef> definitions;
    QVector<MacroDefinitionData> macros;
    SimulationSettings settings;
    bool haveStart = false;
    bool haveStop = false;
    bool haveDt = false;
    bool haveSave = false;
    bool savePerTimeStep = false;
    QString timeUnit;
    bool hasModelEquation = false;

    for (int index = 0; index < records.size(); ++index) {
        const QString record = joinContinuations(records[index]).trimmed();
        if (record.isEmpty() || record == QStringLiteral("{UTF-8}") || record.startsWith(QStringLiteral("help link"))
            || record.startsWith('*') || record.startsWith(QStringLiteral(".Control")))
            continue;
        if (record.startsWith(QStringLiteral(":MACRO:"), Qt::CaseInsensitive)) {
            const int headerEnd = record.indexOf('\n');
            const QString header = record.left(headerEnd);
            const QString firstBodyRecord = headerEnd >= 0 ? record.mid(headerEnd + 1) : QString();
            const int open = header.indexOf('(');
            const int close = header.lastIndexOf(')');
            if (open < 0 || close < open)
                return fail(errorMessage, QStringLiteral("Invalid macro declaration: %1").arg(header.left(120)));
            const QString name = header.mid(8, open - 8).trimmed();
            const QString arguments = header.mid(open + 1, close - open - 1);
            if (arguments.contains(':'))
                return fail(errorMessage, QStringLiteral("Macro output arguments are not supported: %1").arg(name));
            MacroDefinitionData macro;
            macro.displayName = name;
            for (const QString &argument : splitOutside(arguments, ',')) {
                if (!argument.trimmed().isEmpty())
                    macro.parameters.push_back({unquote(argument), {}});
            }
            int mainEquations = 0;
            bool terminated = false;
            bool firstBody = true;
            for (; index <= records.size(); ++index) {
                if (!firstBody && index >= records.size())
                    break;
                const QString macroRecord = firstBody
                    ? joinContinuations(firstBodyRecord).trimmed() : joinContinuations(records[index]).trimmed();
                firstBody = false;
                if (macroRecord.startsWith(QStringLiteral(":END OF MACRO:"), Qt::CaseInsensitive)) {
                    const QString trailing = macroRecord.mid(QStringLiteral(":END OF MACRO:").size()).trimmed();
                    if (!trailing.isEmpty()) {
                        records[index] = trailing;
                        --index;
                    }
                    terminated = true;
                    break;
                }
                const Assignment assignment = parseAssignment(splitOutside(macroRecord, '~').value(0));
                if (assignment.left.isEmpty())
                    continue;
                if (normalizeIdentifier(unquote(assignment.left)) != normalizeIdentifier(name))
                    return fail(errorMessage, QStringLiteral("Macro internal variables are not supported: %1").arg(name));
                macro.equation = cleanExpression(assignment.right);
                ++mainEquations;
            }
            if (!terminated || mainEquations != 1 || macro.equation.isEmpty()) {
                return fail(errorMessage, QStringLiteral("Invalid macro definition: %1 (equations=%2, terminated=%3)")
                    .arg(name).arg(mainEquations).arg(terminated));
            }
            macros.push_back(std::move(macro));
            continue;
        }
        const Assignment assignment = parseAssignment(splitOutside(record, '~').value(0));
        if (assignment.left.isEmpty()) {
            if (record.contains(QStringLiteral(":IS:"), Qt::CaseInsensitive))
                return fail(errorMessage, QStringLiteral("String variables are not supported"));
            const int colon = findOutside(record, ':');
            if (colon > 0) {
                const QString name = unquote(record.left(colon));
                const QString declaration = splitOutside(record, '~').value(0);
                const int declarationColon = findOutside(declaration, ':');
                const QString elementsText = declaration.mid(declarationColon + 1);
                if (elementsText.contains(QStringLiteral("->")))
                    return fail(errorMessage, QStringLiteral("Subscript mapping is not supported: %1").arg(name));
                QStringList expanded;
                for (const QString &element : splitOutside(elementsText, ',')) {
                    if (!expandRangeElement(element, expanded))
                        return fail(errorMessage, QStringLiteral("Invalid subscript range: %1").arg(name));
                }
                if (expanded.isEmpty()
                    || !parsed.defineDimension(name, QVector<QString>(expanded.begin(), expanded.end())))
                    return fail(errorMessage, QStringLiteral("Invalid or duplicate subscript range: %1").arg(name));
                continue;
            }
            return fail(errorMessage, QStringLiteral("Unrecognized or unsupported MDL record: %1").arg(record.left(120)));
        }

        const QString controlName = normalizeIdentifier(unquote(assignment.left)).toUpper();
        const bool control = controlName == QStringLiteral("FINAL_TIME") || controlName == QStringLiteral("INITIAL_TIME")
            || controlName == QStringLiteral("TIME_STEP") || controlName == QStringLiteral("SAVEPER");
        if (control) {
            bool range = false;
            const QString unit = unitAndRange(splitOutside(record, '~').value(1), range);
            if (!unit.isEmpty())
                timeUnit = unit;
            bool numeric = false;
            const double number = cleanExpression(assignment.right).toDouble(&numeric);
            if (controlName == QStringLiteral("SAVEPER")
                && normalizeIdentifier(cleanExpression(assignment.right)) == QStringLiteral("time_step")) {
                savePerTimeStep = true;
            } else if (controlName == QStringLiteral("SAVEPER") && numeric) {
                settings = {settings.startTime(), settings.stopTime(), settings.dt(), number,
                    settings.integrationMethod(), settings.timeUnit()};
            } else if (!numeric) {
                return fail(errorMessage, QStringLiteral("Non-numeric simulation setting is not supported: %1").arg(assignment.left));
            } else if (controlName == QStringLiteral("FINAL_TIME")) {
                settings = {settings.startTime(), number, settings.dt(), settings.saveInterval(),
                    settings.integrationMethod(), settings.timeUnit()};
                haveStop = true;
            } else if (controlName == QStringLiteral("INITIAL_TIME")) {
                settings = {number, settings.stopTime(), settings.dt(), settings.saveInterval(),
                    settings.integrationMethod(), settings.timeUnit()};
                haveStart = true;
            } else if (controlName == QStringLiteral("TIME_STEP")) {
                settings = {settings.startTime(), settings.stopTime(), number, settings.saveInterval(),
                    settings.integrationMethod(), settings.timeUnit()};
                haveDt = true;
            }
            if (controlName == QStringLiteral("SAVEPER"))
                haveSave = true;
            continue;
        }

        VariableDef definition;
        bool stock = false;
        bool hasRange = false;
        if (!parseVariableRecord(record, definition, stock, hasRange, errorMessage))
            return false;
        if (hasRange)
            addWarning(warnings, QStringLiteral("mdl-range-not-preserved"),
                QStringLiteral("Ranges are not preserved: %1").arg(definition.baseName));
        hasModelEquation = true;
        definitions.push_back(std::move(definition));
    }
    if (!hasModelEquation)
        return fail(errorMessage, QStringLiteral("The MDL file contains no model equations"));
    for (const Dimension &dimension : parsed.dimensions()) {
        for (const DimensionElement &element : dimension.elements) {
            if (parsed.findDimension(element.displayName))
                return fail(errorMessage, QStringLiteral("Subranges are not supported: %1 in %2")
                    .arg(element.displayName, dimension.displayName));
        }
    }

    for (VariableDef &definition : definitions) {
        if (!mapSubscripts(parsed, definition, errorMessage))
            return false;
    }
    if (!mergeDefinitions(definitions, errorMessage))
        return false;

    QVector<Symbol> globalSymbols;
    for (const VariableDef &definition : definitions)
        globalSymbols.push_back({definition.baseName, normalizeIdentifier(definition.baseName)});
    for (const Dimension &dimension : parsed.dimensions()) {
        globalSymbols.push_back({dimension.displayName, dimension.nameKey});
        for (const DimensionElement &element : dimension.elements) {
            if (!element.nameKey.isEmpty())
                globalSymbols.push_back({element.displayName, element.nameKey});
        }
    }
    for (const MacroDefinitionData &macro : macros)
        globalSymbols.push_back({macro.displayName, normalizeIdentifier(macro.displayName)});
    globalSymbols = sortedSymbols(std::move(globalSymbols));

    for (VariableDef &definition : definitions) {
        if (!normalizeInPlace(definition.data.equation, globalSymbols, errorMessage)
            || !normalizeInPlace(definition.data.initialEquation, globalSymbols, errorMessage))
            return false;
        for (ArrayElementEquation &entry : definition.data.elementEquations) {
            if (!normalizeInPlace(entry.equation, globalSymbols, errorMessage))
                return false;
        }
        for (QString &net : definition.netExpressions) {
            if (!normalizeInPlace(net, globalSymbols, errorMessage))
                return false;
        }
    }
    for (MacroDefinitionData &macro : macros) {
        QVector<Symbol> localSymbols = globalSymbols;
        for (const MacroParameterData &parameter : macro.parameters)
            localSymbols.push_back({parameter.displayName, normalizeIdentifier(parameter.displayName)});
        if (!normalizeInPlace(macro.equation, sortedSymbols(std::move(localSymbols)), errorMessage))
            return false;
        if (macro.equation.isEmpty() || !parsed.defineMacro(std::move(macro)))
            return fail(errorMessage, QStringLiteral("Invalid or duplicate macro definition"));
    }

    QSet<QString> flowNames;
    for (const VariableDef &definition : definitions) {
        for (const QString &net : definition.netExpressions) {
            const QVector<NetTerm> terms = netTerms(net);
            if (terms.isEmpty())
                flowNames.insert(normalizeIdentifier(definition.baseName) + QStringLiteral("_net_flow"));
            for (const NetTerm &term : terms)
                flowNames.insert(term.nameKey);
        }
    }
    QHash<QString, EntityId> ids;
    for (const VariableDef &definition : definitions) {
        if (definition.data.kind == ElementKind::Stock
            && !restoreElement(parsed, definition.data, ids, errorMessage))
            return false;
    }
    for (const VariableDef &definition : definitions) {
        if (definition.data.kind == ElementKind::Stock
            || flowNames.contains(normalizeIdentifier(definition.baseName)))
            continue;
        if (!restoreElement(parsed, definition.data, ids, errorMessage))
            return false;
    }

    QHash<QString, FlowConnection> connections;
    for (const VariableDef &definition : definitions) {
        if (definition.data.kind != ElementKind::Stock)
            continue;
        const EntityId stock = ids.value(normalizeIdentifier(definition.baseName));
        for (const QString &net : definition.netExpressions) {
            for (const NetTerm &term : netTerms(net)) {
                if (connections.contains(term.nameKey))
                    return fail(errorMessage, QStringLiteral("Flow is used by multiple INTEG terms: %1").arg(term.nameKey));
                connections.insert(term.nameKey, {stock, term.sign});
            }
        }
    }
    for (const VariableDef &definition : definitions) {
        const QString nameKey = normalizeIdentifier(definition.baseName);
        if (definition.data.kind == ElementKind::Stock || !flowNames.contains(nameKey))
            continue;
        ElementData data = definition.data;
        data.kind = ElementKind::Flow;
        const FlowConnection connection = connections.value(nameKey);
        const EntityId open = parsed.createCloud();
        if (open == 0)
            return fail(errorMessage, QStringLiteral("Cannot create open flow endpoint"));
        if (connection.stock != 0) {
            data.flowEndpoints = connection.sign > 0 ? FlowEndpoints {open, connection.stock}
                : FlowEndpoints {connection.stock, open};
        } else {
            const EntityId otherOpen = parsed.createCloud();
            if (otherOpen == 0)
                return fail(errorMessage, QStringLiteral("Cannot create second open flow endpoint"));
            data.flowEndpoints = {open, otherOpen};
        }
        if (!restoreElement(parsed, data, ids, errorMessage))
            return false;
    }
    for (auto connection = connections.constBegin(); connection != connections.constEnd(); ++connection) {
        const EntityId flowId = ids.value(connection.key());
        if (!dynamic_cast<const Flow *>(parsed.find(flowId)))
            return fail(errorMessage, QStringLiteral("Unknown or non-flow rate in INTEG: %1").arg(connection.key()));
    }
    for (const VariableDef &definition : definitions) {
        if (definition.data.kind != ElementKind::Stock)
            continue;
        const EntityId stock = ids.value(normalizeIdentifier(definition.baseName));
        for (const QString &net : definition.netExpressions) {
            if (!netTerms(net).isEmpty())
                continue;
            ElementData flow;
            flow.kind = ElementKind::Flow;
            flow.displayName = uniqueFlowName(parsed, definition.baseName);
            flow.equation = net;
            flow.flowEndpoints = {parsed.createCloud(), stock};
            if (!parsed.restore(flow))
                return fail(errorMessage, QStringLiteral("Cannot create net flow for %1").arg(definition.baseName));
        }
    }

    if (savePerTimeStep)
        settings = {settings.startTime(), settings.stopTime(), settings.dt(), settings.dt(),
            settings.integrationMethod(), settings.timeUnit()};
    settings = {settings.startTime(), settings.stopTime(), settings.dt(), settings.saveInterval(),
        settings.integrationMethod(), timeUnit};
    if ((haveStart || haveStop || haveDt || haveSave) && !parsed.setSimulationSettings(settings))
        return fail(errorMessage, QStringLiteral("Invalid simulation settings in MDL file"));

    QVector<const DocumentElement *> orderedElements;
    for (const DocumentElement &element : parsed.elements())
        orderedElements.push_back(&element);
    std::sort(orderedElements.begin(), orderedElements.end(), [](const DocumentElement *left, const DocumentElement *right) {
        return left->id() < right->id();
    });
    for (int index = 0; index < orderedElements.size(); ++index)
        parsed.setPosition(orderedElements[index]->id(), {(index % 5) * 220.0, (index / 5) * 160.0});
    if (sketchPosition >= 0 && !importSketch(sketch, parsed, ids, warnings, errorMessage))
        return false;

    document = std::move(parsed);
    return true;
}

bool MdlFile::importFile(const QString &fileName, Document &document, QString *errorMessage, QVector<ModelIssue> *warnings)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly))
        return fail(errorMessage, QStringLiteral("Cannot open MDL file: %1").arg(fileName));
    const QByteArray bytes = file.readAll();
    if (file.error() != QFileDevice::NoError)
        return fail(errorMessage, QStringLiteral("Cannot read MDL file: %1").arg(fileName));
    return importText(QString::fromUtf8(bytes), document, errorMessage, warnings);
}

} // namespace qds::io
