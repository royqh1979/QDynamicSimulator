#include <qds/core/Validation.h>

#include <algorithm>
#include <cctype>
#include <functional>
#include <unordered_map>
#include <unordered_set>

namespace qds::core {
namespace {

bool identifierStart(unsigned char c)
{
    return std::isalpha(c) || c == '_' || c >= 128;
}

bool identifierPart(unsigned char c)
{
    return std::isalnum(c) || c == '_' || c >= 128;
}

std::string upper(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return value;
}

void validateEquation(const Document &document, const ModelElement &variable, std::vector<ModelIssue> &issues)
{
    const std::string &equation = variable.equation;
    int parentheses = 0;
    for (char c : equation) {
        if (c == '(')
            ++parentheses;
        else if (c == ')')
            --parentheses;
        if (parentheses < 0)
            break;
    }
    if (parentheses != 0)
        issues.push_back({variable.id(), IssueSeverity::Error, "Unbalanced parentheses"});

    const std::unordered_set<std::string> functions {
        "EXP", "LN", "LOG", "ABS", "MIN", "MAX", "SQRT", "SIN", "COS", "TAN",
        "INTEG", "DELAY", "SMOOTH", "STEP", "PULSE", "RAMP", "LOOKUP", "RANDOM"};
    for (std::size_t i = 0; i < equation.size();) {
        const unsigned char c = static_cast<unsigned char>(equation[i]);
        if (c == '\'' || c == '"') {
            const char quote = static_cast<char>(c);
            const std::size_t end = equation.find(quote, ++i);
            const std::string name = equation.substr(i, end == std::string::npos ? end : end - i);
            if (!name.empty() && !document.model().findByName(name))
                issues.push_back({variable.id(), IssueSeverity::Error, "Undefined variable: " + name});
            i = end == std::string::npos ? equation.size() : end + 1;
            continue;
        }
        if (!identifierStart(c)) {
            ++i;
            continue;
        }
        const std::size_t start = i++;
        while (i < equation.size() && identifierPart(static_cast<unsigned char>(equation[i])))
            ++i;
        const std::string token = equation.substr(start, i - start);
        std::size_t next = i;
        while (next < equation.size() && std::isspace(static_cast<unsigned char>(equation[next])))
            ++next;
        if (next < equation.size() && equation[next] == '(' && functions.count(upper(token)))
            continue;
        if (!document.model().findByName(token))
            issues.push_back({variable.id(), IssueSeverity::Error, "Undefined variable: " + token});
    }
}

} // namespace

std::vector<ModelIssue> validateDocument(const Document &document)
{
    std::vector<ModelIssue> issues;
    std::unordered_set<EntityId> used;
    std::unordered_map<EntityId, std::vector<EntityId>> dependencies;
    for (const ViewElement &element : document.diagram().elements()) {
        if (element.kind() != ElementKind::Connector)
            continue;
        const Connector &connector = static_cast<const Connector &>(element);
        used.insert(connector.from);
        used.insert(connector.to);
        const DocumentElement *from = document.find(connector.from);
        const DocumentElement *to = document.find(connector.to);
        if (from && to && from->kind() <= ElementKind::Data && to->kind() <= ElementKind::Data)
            dependencies[connector.from].push_back(connector.to);
    }
    for (const ModelElement &variable : document.model().elements()) {
        if (variable.kind() == ElementKind::Flow) {
            const Flow &flow = static_cast<const Flow &>(variable);
            used.insert(flow.endpoints.from);
            used.insert(flow.endpoints.to);
            used.insert(flow.id());
        }
        validateEquation(document, variable, issues);
        if (!used.count(variable.id()))
            issues.push_back({variable.id(), IssueSeverity::Warning, "Variable is not connected or referenced"});
    }

    std::unordered_map<EntityId, int> state;
    std::unordered_set<EntityId> reported;
    std::function<void(EntityId)> visit = [&](EntityId id) {
        state[id] = 1;
        for (EntityId next : dependencies[id]) {
            if (state[next] == 1) {
                if (reported.insert(next).second)
                    issues.push_back({next, IssueSeverity::Error, "Algebraic dependency cycle"});
            } else if (state[next] == 0) {
                visit(next);
            }
        }
        state[id] = 2;
    };
    for (const ModelElement &variable : document.model().elements()) {
        if (state[variable.id()] == 0)
            visit(variable.id());
    }
    return issues;
}

} // namespace qds::core
