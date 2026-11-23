#include <qds/core/Document.h>

#include <algorithm>

namespace qds::core {

bool Vec2::operator==(const Vec2 &other) const
{
    return x == other.x && y == other.y;
}

bool Vec2::operator!=(const Vec2 &other) const
{
    return !(*this == other);
}

bool Route::operator==(const Route &other) const
{
    return mode == other.mode && controlPoints == other.controlPoints && manual == other.manual && locked == other.locked;
}

Vec2 DiagramDefinition::position(EntityId id) const
{
    const auto it = m_positions.find(id);
    return it == m_positions.end() ? Vec2 {} : it->second;
}

Route DiagramDefinition::route(ConnectionId id) const
{
    const auto it = m_routes.find(id);
    return it == m_routes.end() ? Route {} : it->second;
}

const std::unordered_map<EntityId, Vec2> &DiagramDefinition::positions() const
{
    return m_positions;
}

const std::unordered_map<ConnectionId, Route> &DiagramDefinition::routes() const
{
    return m_routes;
}

EntityId Document::createVariable(VariableType type, Vec2 position)
{
    const EntityId id = m_model.createVariable(type);
    if (id != 0)
        m_diagram.m_positions[id] = position;
    return id;
}

EntityId Document::addVariable(const Variable &variable, Vec2 position)
{
    const EntityId id = m_model.addVariable(variable);
    if (id != 0)
        m_diagram.m_positions[id] = position;
    return id;
}

bool Document::removeVariable(EntityId id)
{
    if (!m_model.variable(id))
        return false;

    std::vector<ConnectionId> removedConnections;
    for (const Connection &connection : m_model.connections()) {
        if (connection.from == id || connection.to == id)
            removedConnections.push_back(connection.id);
    }
    if (!m_model.removeVariable(id))
        return false;
    for (ConnectionId connectionId : removedConnections)
        m_diagram.m_routes.erase(connectionId);
    m_diagram.m_positions.erase(id);
    return true;
}

bool Document::renameVariable(EntityId id, const std::string &newName)
{
    return m_model.renameVariable(id, newName);
}

bool Document::setEquation(EntityId id, const std::string &equation)
{
    return m_model.setEquation(id, equation);
}

bool Document::setPosition(EntityId id, Vec2 position)
{
    if (!m_model.variable(id))
        return false;
    m_diagram.m_positions[id] = position;
    return true;
}

ConnectionId Document::createConnection(EntityId from, EntityId to, ConnectionType type)
{
    const ConnectionId id = m_model.createConnection(from, to, type);
    if (id != 0)
        m_diagram.m_routes[id] = Route {};
    return id;
}

ConnectionId Document::addConnection(const Connection &connection)
{
    const ConnectionId id = m_model.addConnection(connection);
    if (id != 0)
        m_diagram.m_routes[id] = Route {};
    return id;
}

bool Document::removeConnection(ConnectionId id)
{
    if (!m_model.removeConnection(id))
        return false;
    m_diagram.m_routes.erase(id);
    return true;
}

bool Document::setRoute(ConnectionId id, const Route &route)
{
    if (!m_model.connection(id))
        return false;
    m_diagram.m_routes[id] = route;
    return true;
}

const Model &Document::model() const
{
    return m_model;
}

const DiagramDefinition &Document::diagram() const
{
    return m_diagram;
}

const Variable *Document::variable(EntityId id) const
{
    return m_model.variable(id);
}

const Connection *Document::connection(ConnectionId id) const
{
    return m_model.connection(id);
}

void Document::clear()
{
    m_model.clear();
    m_diagram.m_positions.clear();
    m_diagram.m_routes.clear();
}

} // namespace qds::core
