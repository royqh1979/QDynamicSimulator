#pragma once

#include <qds/core/Model.h>

#include <unordered_map>
#include <vector>

namespace qds::core {

struct Vec2 {
    double x = 0.0;
    double y = 0.0;

    bool operator==(const Vec2 &other) const;
    bool operator!=(const Vec2 &other) const;
};

enum class RouteMode {
    Straight,
    Orthogonal,
    Bezier
};

struct Route {
    RouteMode mode = RouteMode::Orthogonal;
    std::vector<Vec2> controlPoints;
    bool manual = false;
    bool locked = false;

    bool operator==(const Route &other) const;
};

class DiagramDefinition
{
public:
    Vec2 position(EntityId id) const;
    Route route(ConnectionId id) const;
    const std::unordered_map<EntityId, Vec2> &positions() const;
    const std::unordered_map<ConnectionId, Route> &routes() const;

private:
    friend class Document;

    std::unordered_map<EntityId, Vec2> m_positions;
    std::unordered_map<ConnectionId, Route> m_routes;
};

class Document
{
public:
    EntityId createVariable(VariableType type, Vec2 position = {});
    EntityId addVariable(const Variable &variable, Vec2 position = {});
    bool removeVariable(EntityId id);
    bool renameVariable(EntityId id, const std::string &newName);
    bool setEquation(EntityId id, const std::string &equation);
    bool setUnit(EntityId id, const std::string &unit);
    bool setComment(EntityId id, const std::string &comment);
    bool setPosition(EntityId id, Vec2 position);

    ConnectionId createConnection(EntityId from, EntityId to, ConnectionType type = ConnectionType::Information);
    ConnectionId addConnection(const Connection &connection);
    bool removeConnection(ConnectionId id);
    bool reconnectConnection(ConnectionId id, EntityId from, EntityId to);
    bool setRoute(ConnectionId id, const Route &route);

    const Model &model() const;
    const DiagramDefinition &diagram() const;
    const Variable *variable(EntityId id) const;
    const Connection *connection(ConnectionId id) const;
    void clear();

private:
    Model m_model;
    DiagramDefinition m_diagram;
};

} // namespace qds::core
