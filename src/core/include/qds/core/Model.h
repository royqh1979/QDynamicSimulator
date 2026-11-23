#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace qds::core {

using EntityId = std::uint64_t;
using ConnectionId = std::uint64_t;

enum class VariableType {
    Stock,
    Flow,
    Auxiliary,
    Constant,
    Exogenous,
    Cloud,
    Annotation
};

enum class ConnectionType {
    MaterialFlow,
    Information,
    Annotation
};

struct Variable {
    EntityId id = 0;
    std::string name;
    VariableType type = VariableType::Stock;
    std::string equation;
    std::string unit;
    std::string comment;
};

struct Connection {
    ConnectionId id = 0;
    EntityId from = 0;
    EntityId to = 0;
    ConnectionType type = ConnectionType::Information;
};

class Model
{
public:
    EntityId createVariable(VariableType type);
    EntityId addVariable(Variable variable);
    bool removeVariable(EntityId id);
    bool renameVariable(EntityId id, const std::string &newName);
    bool setEquation(EntityId id, const std::string &equation);
    bool setUnit(EntityId id, const std::string &unit);
    bool setComment(EntityId id, const std::string &comment);

    ConnectionId addConnection(Connection connection);
    bool canConnect(EntityId from, EntityId to, ConnectionType type) const;
    ConnectionId createConnection(EntityId from, EntityId to, ConnectionType type = ConnectionType::Information);
    bool removeConnection(ConnectionId id);
    bool reconnectConnection(ConnectionId id, EntityId from, EntityId to);

    const Variable *variable(EntityId id) const;
    const Variable *variableByName(const std::string &name) const;
    const Connection *connection(ConnectionId id) const;
    const std::vector<Variable> &variables() const;
    const std::vector<Connection> &connections() const;
    void clear();

private:
    std::string uniqueName(const std::string &base) const;
    bool contains(EntityId id) const;
    bool nameExists(const std::string &name) const;

    std::vector<Variable> m_variables;
    std::vector<Connection> m_connections;
    std::unordered_map<EntityId, std::size_t> m_variableIndex;
    std::unordered_map<ConnectionId, std::size_t> m_connectionIndex;
    std::unordered_map<std::string, EntityId> m_nameIndex;
    EntityId m_nextEntityId = 1;
    ConnectionId m_nextConnectionId = 1;
};

} // namespace qds::core
