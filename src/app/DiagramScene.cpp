#include "DiagramScene.h"

#include "DiagramItems.h"
#include "DocumentController.h"

#include <QGraphicsSceneMouseEvent>

#include <vector>

using qds::core::ChangeSet;
using qds::core::ConnectionId;
using qds::core::EntityId;

namespace {

bool isConnectionTool(DiagramScene::Tool tool)
{
    return tool == DiagramScene::Tool::Connect || tool == DiagramScene::Tool::ConnectMaterial
        || tool == DiagramScene::Tool::ConnectAnnotation;
}

} // namespace

DiagramScene::DiagramScene(DocumentController *controller, QObject *parent)
    : QGraphicsScene(parent)
    , m_controller(controller)
{
    setSceneRect(-3000.0, -3000.0, 6000.0, 6000.0);
    connect(m_controller, &DocumentController::documentChanged, this, &DiagramScene::applyChangeSet);
    ChangeSet reset;
    reset.documentReset = true;
    applyChangeSet(reset);
}

void DiagramScene::setTool(Tool tool)
{
    m_tool = tool;
    m_connectionSource = 0;
}

DiagramScene::Tool DiagramScene::tool() const
{
    return m_tool;
}

void DiagramScene::removeSelected()
{
    std::vector<EntityId> ids;
    const QList<QGraphicsItem *> selection = selectedItems();
    for (QGraphicsItem *item : selection) {
        if (VariableItem *variable = qgraphicsitem_cast<VariableItem *>(item))
            ids.push_back(variable->id());
    }
    m_controller->removeVariables(ids);
}

bool DiagramScene::renameSelected(const QString &newName)
{
    VariableItem *item = qgraphicsitem_cast<VariableItem *>(selectedItems().value(0));
    return item ? m_controller->renameVariable(item->id(), newName) : false;
}

bool DiagramScene::setSelectedEquation(const QString &equation)
{
    VariableItem *item = qgraphicsitem_cast<VariableItem *>(selectedItems().value(0));
    if (!item)
        return false;
    m_controller->setEquation(item->id(), equation);
    return true;
}

const qds::core::Variable *DiagramScene::selectedVariable() const
{
    VariableItem *item = qgraphicsitem_cast<VariableItem *>(selectedItems().value(0));
    return item ? m_controller->document().variable(item->id()) : nullptr;
}

VariableItem *DiagramScene::variableItem(EntityId id) const
{
    return m_entityItems.value(id);
}

ConnectionItem *DiagramScene::connectionItem(ConnectionId id) const
{
    return m_connectionItems.value(id);
}

void DiagramScene::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    if (event->button() == Qt::RightButton && isConnectionTool(m_tool)) {
        m_connectionSource = 0;
        clearSelection();
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && m_tool != Tool::Select && !isConnectionTool(m_tool)) {
        qds::core::VariableType type = qds::core::VariableType::Stock;
        if (m_tool == Tool::AddFlow)
            type = qds::core::VariableType::Flow;
        else if (m_tool == Tool::AddAuxiliary)
            type = qds::core::VariableType::Auxiliary;
        else if (m_tool == Tool::AddConstant)
            type = qds::core::VariableType::Constant;
        else if (m_tool == Tool::AddExogenous)
            type = qds::core::VariableType::Exogenous;
        else if (m_tool == Tool::AddCloud)
            type = qds::core::VariableType::Cloud;
        else if (m_tool == Tool::AddAnnotation)
            type = qds::core::VariableType::Annotation;

        const EntityId id = m_controller->createVariable(type, event->scenePos());
        clearSelection();
        if (VariableItem *item = m_entityItems.value(id))
            item->setSelected(true);
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && isConnectionTool(m_tool)) {
        VariableItem *item = qgraphicsitem_cast<VariableItem *>(itemAt(event->scenePos(), QTransform()));
        if (!item) {
            m_connectionSource = 0;
            clearSelection();
            event->accept();
            return;
        }
        if (m_connectionSource == 0) {
            m_connectionSource = item->id();
            clearSelection();
            item->setSelected(true);
        } else if (m_connectionSource != item->id()) {
            qds::core::ConnectionType type = qds::core::ConnectionType::Information;
            if (m_tool == Tool::ConnectMaterial)
                type = qds::core::ConnectionType::MaterialFlow;
            else if (m_tool == Tool::ConnectAnnotation)
                type = qds::core::ConnectionType::Annotation;
            m_controller->createConnection(m_connectionSource, item->id(), type);
            m_connectionSource = 0;
            clearSelection();
            item->setSelected(true);
        }
        event->accept();
        return;
    }

    QGraphicsScene::mousePressEvent(event);
}

void DiagramScene::applyChangeSet(const ChangeSet &changeSet)
{
    if (changeSet.documentReset) {
        clear();
        m_entityItems.clear();
        m_connectionItems.clear();
        m_connectionSource = 0;
        for (const qds::core::Variable &variable : m_controller->document().model().variables())
            createVariableItem(variable.id);
        for (const qds::core::Connection &connection : m_controller->document().model().connections())
            createConnectionItem(connection.id);
        return;
    }

    for (ConnectionId id : changeSet.removedConnections) {
        ConnectionItem *item = m_connectionItems.take(id);
        delete item;
    }
    for (EntityId id : changeSet.removedEntities) {
        if (m_connectionSource == id)
            m_connectionSource = 0;
        VariableItem *item = m_entityItems.take(id);
        delete item;
    }
    for (EntityId id : changeSet.addedEntities)
        createVariableItem(id);
    for (ConnectionId id : changeSet.addedConnections)
        createConnectionItem(id);

    for (EntityId id : changeSet.updatedEntities) {
        VariableItem *item = m_entityItems.value(id);
        const qds::core::Variable *variable = m_controller->document().variable(id);
        if (!item || !variable)
            continue;
        const qds::core::Vec2 position = m_controller->document().diagram().position(id);
        item->refresh(*variable, QPointF(position.x, position.y));
    }
    for (ConnectionId id : changeSet.updatedConnections) {
        if (ConnectionItem *item = m_connectionItems.value(id))
            item->updatePosition();
    }
}

VariableItem *DiagramScene::createVariableItem(EntityId id)
{
    const qds::core::Variable *variable = m_controller->document().variable(id);
    if (!variable)
        return nullptr;
    const qds::core::Vec2 position = m_controller->document().diagram().position(id);
    VariableItem *item = new VariableItem(*variable, QPointF(position.x, position.y));
    addItem(item);
    m_entityItems.insert(id, item);
    connect(item, &VariableItem::positionChanged, this, [this](EntityId id, const QPointF &) {
        updateConnections(id);
    });
    connect(item, &VariableItem::moveFinished, this, &DiagramScene::finishMove);
    return item;
}

ConnectionItem *DiagramScene::createConnectionItem(ConnectionId id)
{
    const qds::core::Connection *connection = m_controller->document().connection(id);
    if (!connection)
        return nullptr;
    VariableItem *from = m_entityItems.value(connection->from);
    VariableItem *to = m_entityItems.value(connection->to);
    if (!from || !to)
        return nullptr;
    ConnectionItem *item = new ConnectionItem(id, connection->type, from, to);
    addItem(item);
    m_connectionItems.insert(id, item);
    return item;
}

void DiagramScene::updateConnections(EntityId id)
{
    for (ConnectionItem *connection : m_connectionItems) {
        if (connection->connects(id))
            connection->updatePosition();
    }
}

void DiagramScene::finishMove()
{
    std::vector<std::pair<EntityId, QPointF>> positions;
    const QList<QGraphicsItem *> selection = selectedItems();
    for (QGraphicsItem *item : selection) {
        if (VariableItem *variable = qgraphicsitem_cast<VariableItem *>(item))
            positions.emplace_back(variable->id(), variable->pos());
    }
    m_controller->moveVariables(positions);
}
