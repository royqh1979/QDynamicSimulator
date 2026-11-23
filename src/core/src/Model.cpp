#include <qds/core/Model.h>

#include <algorithm>

namespace qds::core {
namespace {

std::string trimmed(std::string value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string defaultName(VariableType type)
{
    switch (type) {
    case VariableType::Stock:
        return "Stock";
    case VariableType::Flow:
        return "Flow";
    case VariableType::Auxiliary:
        return "Auxiliary";
    }
    return "Variable";
}

} // namespace

std::string Model::createVariable(VariableType type, double x, double y)
{
    Variable variable;
    variable.name = uniqueName(defaultName(type));
    variable.type = type;
    variable.x = x;
    variable.y = y;
    m_variables.push_back(variable);
    return variable.name;
}

bool Model::addVariable(Variable variable)
{
    variable.name = trimmed(variable.name);
    if (variable.name.empty() || contains(variable.name))
        return false;
    m_variables.push_back(std::move(variable));
    return true;
}

bool Model::removeVariable(const std::string &name)
{
    const auto it = std::find_if(m_variables.begin(), m_variables.end(), [&name](const Variable &variable) {
        return variable.name == name;
    });
    if (it == m_variables.end())
        return false;

    m_variables.erase(it);
    m_connections.erase(
        std::remove_if(m_connections.begin(), m_connections.end(), [&name](const Connection &connection) {
            return connection.from == name || connection.to == name;
        }),
        m_connections.end());
    return true;
}

bool Model::renameVariable(const std::string &oldName, const std::string &newName)
{
    const std::string name = trimmed(newName);
    if (name.empty() || (name != oldName && contains(name)))
        return false;

    for (Variable &variable : m_variables) {
        if (variable.name == oldName) {
            variable.name = name;
            for (Connection &connection : m_connections) {
                if (connection.from == oldName)
                    connection.from = name;
                if (connection.to == oldName)
                    connection.to = name;
            }
            return true;
        }
    }
    return false;
}

bool Model::setEquation(const std::string &name, const std::string &equation)
{
    for (Variable &variable : m_variables) {
        if (variable.name == name) {
            variable.equation = equation;
            return true;
        }
    }
    return false;
}

bool Model::setPosition(const std::string &name, double x, double y)
{
    for (Variable &variable : m_variables) {
        if (variable.name == name) {
            variable.x = x;
            variable.y = y;
            return true;
        }
    }
    return false;
}

bool Model::addConnection(const std::string &from, const std::string &to)
{
    if (from == to || !contains(from) || !contains(to))
        return false;

    const auto duplicate = std::find_if(m_connections.begin(), m_connections.end(), [&from, &to](const Connection &connection) {
        return connection.from == from && connection.to == to;
    });
    if (duplicate != m_connections.end())
        return false;

    m_connections.push_back({from, to});
    return true;
}

bool Model::removeConnection(const std::string &from, const std::string &to)
{
    const auto it = std::find_if(m_connections.begin(), m_connections.end(), [&from, &to](const Connection &connection) {
        return connection.from == from && connection.to == to;
    });
    if (it == m_connections.end())
        return false;
    m_connections.erase(it);
    return true;
}

const Variable *Model::variable(const std::string &name) const
{
    const auto it = std::find_if(m_variables.begin(), m_variables.end(), [&name](const Variable &variable) {
        return variable.name == name;
    });
    return it == m_variables.end() ? nullptr : &*it;
}

const std::vector<Variable> &Model::variables() const
{
    return m_variables;
}

const std::vector<Connection> &Model::connections() const
{
    return m_connections;
}

void Model::clear()
{
    m_variables.clear();
    m_connections.clear();
}

std::string Model::uniqueName(const std::string &base) const
{
    if (!contains(base))
        return base;
    for (int suffix = 2;; ++suffix) {
        const std::string candidate = base + " " + std::to_string(suffix);
        if (!contains(candidate))
            return candidate;
    }
}

bool Model::contains(const std::string &name) const
{
    return variable(name) != nullptr;
}

} // namespace qds::core
