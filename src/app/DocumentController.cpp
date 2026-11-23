#include "DocumentController.h"

#include <QUndoCommand>

#include <memory>
#include <utility>

using qds::core::ChangeSet;
using qds::core::Connection;
using qds::core::ConnectionId;
using qds::core::Document;
using qds::core::EntityId;
using qds::core::Route;
using qds::core::Variable;
using qds::core::VariableType;

namespace {

class FunctionalCommand : public QUndoCommand
{
public:
    FunctionalCommand(const QString &text, DocumentController::Callback undo, DocumentController::Callback redo)
        : QUndoCommand(text)
        , m_undo(std::move(undo))
        , m_redo(std::move(redo))
    {
    }

    void undo() override
    {
        m_undo();
    }

    void redo() override
    {
        m_redo();
    }

private:
    DocumentController::Callback m_undo;
    DocumentController::Callback m_redo;
};

ChangeSet entityChange(EntityId id, bool added, bool removed = false)
{
    ChangeSet change;
    if (added)
        change.addedEntities.push_back(id);
    else if (removed)
        change.removedEntities.push_back(id);
    else
        change.updatedEntities.push_back(id);
    return change;
}

ChangeSet connectionChange(ConnectionId id, bool added, bool removed = false)
{
    ChangeSet change;
    if (added)
        change.addedConnections.push_back(id);
    else if (removed)
        change.removedConnections.push_back(id);
    else
        change.updatedConnections.push_back(id);
    return change;
}

qds::core::Vec2 toVec(const QPointF &value)
{
    return {value.x(), value.y()};
}

} // namespace

DocumentController::DocumentController(QObject *parent)
    : QObject(parent)
{
}

const Document &DocumentController::document() const
{
    return m_document;
}

QUndoStack *DocumentController::undoStack()
{
    return &m_undoStack;
}

EntityId DocumentController::createVariable(VariableType type, const QPointF &position)
{
    struct State {
        EntityId id = 0;
        Variable variable;
    };
    auto state = std::make_shared<State>();
    pushCommand(
        tr("Create variable"),
        [this, state, position]() {
            m_document.removeVariable(state->id);
            ChangeSet change;
            change.removedEntities.push_back(state->id);
            notify(change);
        },
        [this, state, type, position]() {
            if (state->id == 0) {
                state->id = m_document.createVariable(type, toVec(position));
                state->variable = *m_document.variable(state->id);
            } else {
                m_document.addVariable(state->variable, toVec(position));
            }
            notify(entityChange(state->id, true));
        });
    return state->id;
}

void DocumentController::removeVariables(const std::vector<EntityId> &ids)
{
    std::vector<EntityId> existing;
    for (EntityId id : ids) {
        if (m_document.variable(id))
            existing.push_back(id);
    }
    if (existing.empty())
        return;

    m_undoStack.beginMacro(tr("Delete variables"));
    for (EntityId id : existing) {
        struct State {
            Variable variable;
            qds::core::Vec2 position;
            std::vector<std::pair<Connection, Route>> connections;
        };
        auto state = std::make_shared<State>();
        state->variable = *m_document.variable(id);
        state->position = m_document.diagram().position(id);
        for (const Connection &connection : m_document.model().connections()) {
            if (connection.from == id || connection.to == id)
                state->connections.emplace_back(connection, m_document.diagram().route(connection.id));
        }

        pushCommand(
            tr("Delete variable"),
            [this, state]() {
                ChangeSet change;
                const EntityId restored = m_document.addVariable(state->variable, state->position);
                change.addedEntities.push_back(restored);
                for (const auto &entry : state->connections) {
                    const ConnectionId id = m_document.addConnection(entry.first);
                    m_document.setRoute(id, entry.second);
                    change.addedConnections.push_back(id);
                }
                notify(change);
            },
            [this, state, id]() {
                ChangeSet change;
                change.removedEntities.push_back(id);
                for (const auto &entry : state->connections)
                    change.removedConnections.push_back(entry.first.id);
                m_document.removeVariable(id);
                notify(change);
            });
    }
    m_undoStack.endMacro();
}

bool DocumentController::renameVariable(EntityId id, const QString &name)
{
    const Variable *variable = m_document.variable(id);
    const QString trimmed = name.trimmed();
    if (!variable || trimmed.isEmpty())
        return false;
    const std::string after = trimmed.toStdString();
    const Variable *duplicate = m_document.model().variableByName(after);
    if (duplicate && duplicate->id != id)
        return false;
    const std::string before = variable->name;
    if (before == after)
        return true;

    pushCommand(
        tr("Rename variable"),
        [this, id, before]() {
            m_document.renameVariable(id, before);
            notify(entityChange(id, false));
        },
        [this, id, after]() {
            m_document.renameVariable(id, after);
            notify(entityChange(id, false));
        });
    return true;
}

void DocumentController::setEquation(EntityId id, const QString &equation)
{
    const Variable *variable = m_document.variable(id);
    if (!variable)
        return;
    const std::string before = variable->equation;
    const std::string after = equation.toStdString();
    if (before == after)
        return;
    pushCommand(
        tr("Edit equation"),
        [this, id, before]() {
            m_document.setEquation(id, before);
            notify(entityChange(id, false));
        },
        [this, id, after]() {
            m_document.setEquation(id, after);
            notify(entityChange(id, false));
        });
}

void DocumentController::moveVariables(const std::vector<std::pair<EntityId, QPointF>> &positions)
{
    m_undoStack.beginMacro(tr("Move variables"));
    for (const auto &entry : positions) {
        const EntityId id = entry.first;
        if (!m_document.variable(id))
            continue;
        const qds::core::Vec2 before = m_document.diagram().position(id);
        const qds::core::Vec2 after = toVec(entry.second);
        if (before == after)
            continue;
        pushCommand(
            tr("Move variable"),
            [this, id, before]() {
                m_document.setPosition(id, before);
                notify(entityChange(id, false));
            },
            [this, id, after]() {
                m_document.setPosition(id, after);
                notify(entityChange(id, false));
            });
    }
    m_undoStack.endMacro();
}

ConnectionId DocumentController::createConnection(EntityId from, EntityId to)
{
    if (from == to || !m_document.variable(from) || !m_document.variable(to))
        return 0;
    for (const Connection &connection : m_document.model().connections()) {
        if (connection.from == from && connection.to == to)
            return 0;
    }

    struct State {
        Connection connection;
    };
    auto state = std::make_shared<State>();
    state->connection.from = from;
    state->connection.to = to;
    pushCommand(
        tr("Create connection"),
        [this, state]() {
            m_document.removeConnection(state->connection.id);
            notify(connectionChange(state->connection.id, false, true));
        },
        [this, state]() {
            if (state->connection.id == 0)
                state->connection.id = m_document.createConnection(state->connection.from, state->connection.to);
            else
                m_document.addConnection(state->connection);
            notify(connectionChange(state->connection.id, true));
        });
    return state->connection.id;
}

void DocumentController::removeConnections(const std::vector<ConnectionId> &ids)
{
    m_undoStack.beginMacro(tr("Delete connections"));
    for (ConnectionId id : ids) {
        const Connection *connection = m_document.connection(id);
        if (!connection)
            continue;
        const Connection before = *connection;
        const Route route = m_document.diagram().route(id);
        pushCommand(
            tr("Delete connection"),
            [this, before, route]() {
                const ConnectionId id = m_document.addConnection(before);
                m_document.setRoute(id, route);
                notify(connectionChange(id, true));
            },
            [this, id]() {
                m_document.removeConnection(id);
                notify(connectionChange(id, false, true));
            });
    }
    m_undoStack.endMacro();
}

void DocumentController::setRoute(ConnectionId id, const Route &route)
{
    if (!m_document.connection(id))
        return;
    const Route before = m_document.diagram().route(id);
    if (before == route)
        return;
    pushCommand(
        tr("Edit route"),
        [this, id, before]() {
            m_document.setRoute(id, before);
            notify(connectionChange(id, false));
        },
        [this, id, route]() {
            m_document.setRoute(id, route);
            notify(connectionChange(id, false));
        });
}

void DocumentController::replaceDocument(Document document)
{
    m_document = std::move(document);
    m_undoStack.clear();
    ChangeSet change;
    change.documentReset = true;
    notify(change);
}

void DocumentController::pushCommand(const QString &text, const Callback &undo, const Callback &redo)
{
    m_undoStack.push(new FunctionalCommand(text, undo, redo));
}

void DocumentController::notify(const ChangeSet &changeSet)
{
    emit documentChanged(changeSet);
}
