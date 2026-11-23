#include "DiagramScene.h"

#include "DiagramItems.h"
#include "DocumentController.h"

#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QtMath>

#include <algorithm>
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
    std::vector<ConnectionId> connections;
    const QList<QGraphicsItem *> selection = selectedItems();
    for (QGraphicsItem *item : selection) {
        if (VariableItem *variable = qgraphicsitem_cast<VariableItem *>(item))
            ids.push_back(variable->id());
        else if (ConnectionItem *connection = qgraphicsitem_cast<ConnectionItem *>(item))
            connections.push_back(connection->id());
    }
    m_controller->removeConnections(connections);
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

void DiagramScene::selectAllItems()
{
    const QList<QGraphicsItem *> allItems = items();
    for (QGraphicsItem *item : allItems) {
        if (item->flags() & QGraphicsItem::ItemIsSelectable)
            item->setSelected(true);
    }
}

void DiagramScene::alignSelected(Qt::Alignment alignment)
{
    QVector<VariableItem *> selected;
    for (QGraphicsItem *item : selectedItems()) {
        if (VariableItem *variable = qgraphicsitem_cast<VariableItem *>(item))
            selected.append(variable);
    }
    if (selected.size() < 2)
        return;

    std::vector<std::pair<EntityId, QPointF>> positions;
    for (VariableItem *item : selected) {
        QPointF position = item->pos();
        if (alignment & Qt::AlignLeft)
            position.setX(std::min(position.x(), selected.front()->pos().x()));
        for (VariableItem *other : selected) {
            if (alignment & Qt::AlignLeft)
                position.setX(std::min(position.x(), other->pos().x()));
            else if (alignment & Qt::AlignRight)
                position.setX(std::max(position.x(), other->pos().x()));
            else if (alignment & Qt::AlignTop)
                position.setY(std::min(position.y(), other->pos().y()));
            else if (alignment & Qt::AlignBottom)
                position.setY(std::max(position.y(), other->pos().y()));
        }
        if (alignment & Qt::AlignHCenter) {
            double center = 0.0;
            for (VariableItem *other : selected)
                center += other->pos().x();
            position.setX(center / selected.size());
        } else if (alignment & Qt::AlignVCenter) {
            double center = 0.0;
            for (VariableItem *other : selected)
                center += other->pos().y();
            position.setY(center / selected.size());
        }
        positions.emplace_back(item->id(), position);
    }
    m_controller->moveVariables(positions);
}

void DiagramScene::distributeSelected(bool horizontal)
{
    QVector<VariableItem *> selected;
    for (QGraphicsItem *item : selectedItems()) {
        if (VariableItem *variable = qgraphicsitem_cast<VariableItem *>(item))
            selected.append(variable);
    }
    if (selected.size() < 3)
        return;
    std::sort(selected.begin(), selected.end(), [horizontal](VariableItem *left, VariableItem *right) {
        return horizontal ? left->pos().x() < right->pos().x() : left->pos().y() < right->pos().y();
    });
    const double start = horizontal ? selected.front()->pos().x() : selected.front()->pos().y();
    const double end = horizontal ? selected.back()->pos().x() : selected.back()->pos().y();
    const double spacing = (end - start) / (selected.size() - 1);
    std::vector<std::pair<EntityId, QPointF>> positions;
    for (int index = 1; index < selected.size() - 1; ++index) {
        QPointF position = selected[index]->pos();
        if (horizontal)
            position.setX(start + spacing * index);
        else
            position.setY(start + spacing * index);
        positions.emplace_back(selected[index]->id(), position);
    }
    m_controller->moveVariables(positions);
}

void DiagramScene::setGridVisible(bool visible)
{
    m_gridVisible = visible;
    update();
}

void DiagramScene::setSnapEnabled(bool enabled)
{
    m_snapEnabled = enabled;
}

QPointF DiagramScene::snapPoint(const QPointF &position) const
{
    if (!m_snapEnabled)
        return position;
    return QPointF(qRound64(position.x() / m_gridSize) * m_gridSize, qRound64(position.y() / m_gridSize) * m_gridSize);
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

        const EntityId id = m_controller->createVariable(type, snapPoint(event->scenePos()));
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
        updateSceneRect();
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
    updateSceneRect();
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

void DiagramScene::updateSceneRect()
{
    QRectF bounds = itemsBoundingRect().adjusted(-1000.0, -1000.0, 1000.0, 1000.0);
    if (bounds.isEmpty())
        bounds = QRectF(-1000.0, -1000.0, 2000.0, 2000.0);
    setSceneRect(bounds);
}

void DiagramScene::finishMove()
{
    std::vector<std::pair<EntityId, QPointF>> positions;
    const QList<QGraphicsItem *> selection = selectedItems();
    for (QGraphicsItem *item : selection) {
        if (VariableItem *variable = qgraphicsitem_cast<VariableItem *>(item))
            positions.emplace_back(variable->id(), snapPoint(variable->pos()));
    }
    m_controller->moveVariables(positions);
}

void DiagramScene::drawBackground(QPainter *painter, const QRectF &rect)
{
    painter->fillRect(rect, QColor(250, 250, 250));
    if (!m_gridVisible)
        return;
    painter->setPen(QPen(QColor(225, 225, 225), 0.0));
    const double left = qFloor(rect.left() / m_gridSize) * m_gridSize;
    const double top = qFloor(rect.top() / m_gridSize) * m_gridSize;
    for (double x = left; x < rect.right(); x += m_gridSize)
        painter->drawLine(QPointF(x, rect.top()), QPointF(x, rect.bottom()));
    for (double y = top; y < rect.bottom(); y += m_gridSize)
        painter->drawLine(QPointF(rect.left(), y), QPointF(rect.right(), y));
}
