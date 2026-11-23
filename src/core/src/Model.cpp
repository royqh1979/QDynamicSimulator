#include <qds/core/Model.h>

#include <algorithm>
#include <utility>

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
    case VariableType::Constant:
        return "Constant";
    case VariableType::Exogenous:
        return "Exogenous";
    case VariableType::Cloud:
        return "Cloud";
    case VariableType::Annotation:
        return "Note";
    }
    return "Variable";
}

bool validConnection(const Variable &from, const Variable &to, ConnectionType type)
{
    if (type == ConnectionType::Annotation)
        return from.type == VariableType::Annotation || to.type == VariableType::Annotation;
    if (from.type == VariableType::Annotation || to.type == VariableType::Annotation)
        return false;
    if (type == ConnectionType::Information)
        return from.type != VariableType::Cloud && to.type != VariableType::Cloud;

    const Variable *other = nullptr;
    if (from.type == VariableType::Flow && to.type != VariableType::Flow)
        other = &to;
    else if (to.type == VariableType::Flow && from.type != VariableType::Flow)
        other = &from;
    return other && (other->type == VariableType::Stock || other->type == VariableType::Cloud);
}

} // namespace

EntityId Model::createVariable(VariableType type)
{
    Variable variable;
    variable.name = uniqueName(defaultName(type));
    variable.type = type;
    return addVariable(std::move(variable));
}

EntityId Model::addVariable(Variable variable)
{
    variable.name = trimmed(variable.name);
    if (variable.name.empty() || nameExists(variable.name) || (variable.id != 0 && contains(variable.id)))
        return 0;
    if (variable.id == 0)
        variable.id = m_nextEntityId;
    m_nextEntityId = std::max(m_nextEntityId, variable.id + 1);
    m_nameIndex[variable.name] = variable.id;
    m_variableIndex[variable.id] = m_variables.size();
    m_variables.push_back(std::move(variable));
    return m_variables.back().id;
}

bool Model::removeVariable(EntityId id)
{
    const auto index = m_variableIndex.find(id);
    if (index == m_variableIndex.end())
        return false;

    const std::size_t position = index->second;
    m_nameIndex.erase(m_variables[position].name);
    m_variableIndex.erase(index);
    m_variables.erase(m_variables.begin() + static_cast<std::ptrdiff_t>(position));
    for (std::size_t current = position; current < m_variables.size(); ++current)
        m_variableIndex[m_variables[current].id] = current;

    m_connections.erase(
        std::remove_if(m_connections.begin(), m_connections.end(), [id](const Connection &connection) {
            return connection.from == id || connection.to == id;
        }),
        m_connections.end());
    m_connectionIndex.clear();
    for (std::size_t current = 0; current < m_connections.size(); ++current)
        m_connectionIndex[m_connections[current].id] = current;
    return true;
}

bool Model::renameVariable(EntityId id, const std::string &newName)
{
    const std::string name = trimmed(newName);
    if (name.empty())
        return false;
    for (Variable &variable : m_variables) {
        if (variable.id == id) {
            if (variable.name != name && nameExists(name))
                return false;
            m_nameIndex.erase(variable.name);
            variable.name = name;
            m_nameIndex[name] = id;
            return true;
        }
    }
    return false;
}

bool Model::setEquation(EntityId id, const std::string &equation)
{
    for (Variable &variable : m_variables) {
        if (variable.id == id) {
            variable.equation = equation;
            return true;
        }
    }
    return false;
}

ConnectionId Model::addConnection(Connection connection)
{
    if ((connection.id != 0 && this->connection(connection.id))
        || !canConnect(connection.from, connection.to, connection.type))
        return 0;

    if (connection.id == 0)
        connection.id = m_nextConnectionId;
    m_nextConnectionId = std::max(m_nextConnectionId, connection.id + 1);
    m_connectionIndex[connection.id] = m_connections.size();
    m_connections.push_back(connection);
    return connection.id;
}

bool Model::canConnect(EntityId from, EntityId to, ConnectionType type) const
{
    if (from == to || !contains(from) || !contains(to)
        || !validConnection(*variable(from), *variable(to), type))
        return false;
    return std::none_of(m_connections.begin(), m_connections.end(), [from, to, type](const Connection &connection) {
        return connection.from == from && connection.to == to && connection.type == type;
    });
}

ConnectionId Model::createConnection(EntityId from, EntityId to, ConnectionType type)
{
    return addConnection({0, from, to, type});
}

bool Model::removeConnection(ConnectionId id)
{
    const auto index = m_connectionIndex.find(id);
    if (index == m_connectionIndex.end())
        return false;
    const std::size_t position = index->second;
    m_connectionIndex.erase(index);
    m_connections.erase(m_connections.begin() + static_cast<std::ptrdiff_t>(position));
    for (std::size_t current = position; current < m_connections.size(); ++current)
        m_connectionIndex[m_connections[current].id] = current;
    return true;
}

const Variable *Model::variable(EntityId id) const
{
    const auto it = m_variableIndex.find(id);
    return it == m_variableIndex.end() ? nullptr : &m_variables[it->second];
}

const Variable *Model::variableByName(const std::string &name) const
{
    const auto it = m_nameIndex.find(name);
    return it == m_nameIndex.end() ? nullptr : variable(it->second);
}

const Connection *Model::connection(ConnectionId id) const
{
    const auto it = m_connectionIndex.find(id);
    return it == m_connectionIndex.end() ? nullptr : &m_connections[it->second];
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
    m_variableIndex.clear();
    m_connectionIndex.clear();
    m_nameIndex.clear();
    m_nextEntityId = 1;
    m_nextConnectionId = 1;
}

std::string Model::uniqueName(const std::string &base) const
{
    if (!nameExists(base))
        return base;
    for (int suffix = 2;; ++suffix) {
        const std::string candidate = base + " " + std::to_string(suffix);
        if (!nameExists(candidate))
            return candidate;
    }
}

bool Model::contains(EntityId id) const
{
    return variable(id) != nullptr;
}

bool Model::nameExists(const std::string &name) const
{
    return variableByName(name) != nullptr;
}

} // namespace qds::core
