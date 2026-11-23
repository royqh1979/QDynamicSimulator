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
    connect(this, &QGraphicsScene::selectionChanged, this, &DiagramScene::syncControlPoints);
    ChangeSet reset;
    reset.documentReset = true;
    applyChangeSet(reset);
}

DiagramScene::~DiagramScene()
{
    disconnect(this, &QGraphicsScene::selectionChanged, this, &DiagramScene::syncControlPoints);
    m_controlHandles.clear();
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

std::vector<EntityId> DiagramScene::selectedVariableIds() const
{
    std::vector<EntityId> ids;
    for (QGraphicsItem *item : selectedItems()) {
        if (VariableItem *variable = qgraphicsitem_cast<VariableItem *>(item))
            ids.push_back(variable->id());
    }
    return ids;
}

std::vector<EntityId> DiagramScene::findVariables(const QString &text) const
{
    std::vector<EntityId> matches;
    for (const qds::core::Variable &variable : m_controller->document().model().variables()) {
        if (QString::fromStdString(variable.name).contains(text, Qt::CaseInsensitive))
            matches.push_back(variable.id);
    }
    return matches;
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

void DiagramScene::setSelectedRouteMode(qds::core::RouteMode mode)
{
    for (QGraphicsItem *item : selectedItems()) {
        ConnectionItem *connection = qgraphicsitem_cast<ConnectionItem *>(item);
        if (!connection)
            continue;
        qds::core::Route route = m_controller->document().diagram().route(connection->id());
        route.mode = mode;
        route.manual = true;
        if (mode == qds::core::RouteMode::Straight)
            route.controlPoints.clear();
        else if (mode == qds::core::RouteMode::Bezier && route.controlPoints.size() < 2) {
            const QPointF start = connection->from()->pos();
            const QPointF end = connection->to()->pos();
            route.controlPoints = {
                {start.x() + (end.x() - start.x()) / 3.0, start.y() + (end.y() - start.y()) / 3.0 - 50.0},
                {start.x() + (end.x() - start.x()) * 2.0 / 3.0, start.y() + (end.y() - start.y()) * 2.0 / 3.0 + 50.0}};
        }
        m_controller->setRoute(connection->id(), route);
    }
}

void DiagramScene::resetSelectedRoute()
{
    for (QGraphicsItem *item : selectedItems()) {
        if (ConnectionItem *connection = qgraphicsitem_cast<ConnectionItem *>(item))
            m_controller->setRoute(connection->id(), qds::core::Route());
    }
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

void DiagramScene::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event)
{
    ConnectionItem *connection = qgraphicsitem_cast<ConnectionItem *>(itemAt(event->scenePos(), QTransform()));
    if (connection) {
        qds::core::Route route = m_controller->document().diagram().route(connection->id());
        const QPointF point = snapPoint(event->scenePos());
        route.controlPoints.push_back({point.x(), point.y()});
        route.manual = true;
        m_controller->setRoute(connection->id(), route);
        event->accept();
        return;
    }
    QGraphicsScene::mouseDoubleClickEvent(event);
}

void DiagramScene::applyChangeSet(const ChangeSet &changeSet)
{
    if (changeSet.documentReset) {
        m_controlHandles.clear();
        m_obstacles.clear();
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
        m_obstacles.remove(id);
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
        m_obstacles.upsert(id, item->sceneBoundingRect().adjusted(-12.0, -12.0, 12.0, 12.0));
    }
    for (ConnectionId id : changeSet.updatedConnections) {
        ConnectionItem *item = m_connectionItems.value(id);
        const qds::core::Connection *connection = m_controller->document().connection(id);
        if (!item || !connection)
            continue;
        VariableItem *from = m_entityItems.value(connection->from);
        VariableItem *to = m_entityItems.value(connection->to);
        if (from && to)
            item->rebind(from, to);
        item->setRoute(m_controller->document().diagram().route(id));
        updateAutomaticRoute(item);
    }
    syncControlPoints();
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
    m_obstacles.upsert(id, item->sceneBoundingRect().adjusted(-12.0, -12.0, 12.0, 12.0));
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
    ConnectionItem *item = new ConnectionItem(id, connection->type, from, to, m_controller->document().diagram().route(id));
    addItem(item);
    m_connectionItems.insert(id, item);
    updateAutomaticRoute(item);
    return item;
}

void DiagramScene::updateConnections(EntityId id)
{
    for (ConnectionItem *connection : m_connectionItems) {
        if (connection->connects(id)) {
            connection->updatePosition();
            updateAutomaticRoute(connection);
        }
    }
    for (ControlPointHandle *handle : m_controlHandles) {
        ConnectionItem *connection = m_connectionItems.value(handle->connectionId());
        if (!connection)
            continue;
        if (handle->index() == -1 && connection->from()->id() == id)
            handle->setPos(connection->from()->pos());
        else if (handle->index() == -2 && connection->to()->id() == id)
            handle->setPos(connection->to()->pos());
    }
}

void DiagramScene::syncControlPoints()
{
    ConnectionItem *selected = qgraphicsitem_cast<ConnectionItem *>(selectedItems().value(0));
    const qds::core::Route route = selected
        ? m_controller->document().diagram().route(selected->id())
        : qds::core::Route();
    int required = 0;
    if (selected)
        required = 2 + (route.mode == qds::core::RouteMode::Straight ? 0 : static_cast<int>(route.controlPoints.size()));

    auto updateHandle = [this](int handleIndex, qds::core::ConnectionId id, int pointIndex,
                              ControlPointHandle::Role role, const QPointF &position) {
        if (handleIndex == m_controlHandles.size()) {
            ControlPointHandle *handle = new ControlPointHandle(id, pointIndex, role, position);
            addItem(handle);
            m_controlHandles.append(handle);
            connect(handle, &ControlPointHandle::moved, this, &DiagramScene::previewControlPoint);
            connect(handle, &ControlPointHandle::finished, this, &DiagramScene::finishControlPoint);
            connect(handle, &ControlPointHandle::removeRequested, this, &DiagramScene::removeControlPoint);
        }
        m_controlHandles[handleIndex]->assign(id, pointIndex, role, position);
    };

    if (selected) {
        updateHandle(0, selected->id(), -1, ControlPointHandle::Role::Source, selected->from()->pos());
        updateHandle(1, selected->id(), -2, ControlPointHandle::Role::Target, selected->to()->pos());
        if (route.mode != qds::core::RouteMode::Straight) {
            for (std::size_t index = 0; index < route.controlPoints.size(); ++index) {
                const qds::core::Vec2 point = route.controlPoints[index];
                updateHandle(
                    static_cast<int>(index) + 2,
                    selected->id(),
                    static_cast<int>(index),
                    ControlPointHandle::Role::ControlPoint,
                    QPointF(point.x, point.y));
            }
        }
    }
    for (int index = required; index < m_controlHandles.size(); ++index)
        m_controlHandles[index]->hide();
}

void DiagramScene::previewControlPoint(ConnectionId id, int index, const QPointF &position)
{
    if (index < 0)
        return;
    ConnectionItem *item = m_connectionItems.value(id);
    if (!item)
        return;
    qds::core::Route route = m_controller->document().diagram().route(id);
    if (index >= static_cast<int>(route.controlPoints.size()))
        return;
    route.controlPoints[static_cast<std::size_t>(index)] = {position.x(), position.y()};
    route.manual = true;
    item->setRoute(route);
}

void DiagramScene::finishControlPoint(ConnectionId id, int index, const QPointF &position)
{
    ConnectionItem *item = m_connectionItems.value(id);
    const qds::core::Connection *connection = m_controller->document().connection(id);
    if (!item || !connection)
        return;
    if (index >= 0) {
        qds::core::Route route = m_controller->document().diagram().route(id);
        if (index >= static_cast<int>(route.controlPoints.size()))
            return;
        const QPointF snapped = snapPoint(position);
        route.controlPoints[static_cast<std::size_t>(index)] = {snapped.x(), snapped.y()};
        route.manual = true;
        m_controller->setRoute(id, route);
        return;
    }

    EntityId target = 0;
    const QList<QGraphicsItem *> candidates = items(position);
    for (QGraphicsItem *candidate : candidates) {
        if (VariableItem *variable = qgraphicsitem_cast<VariableItem *>(candidate)) {
            target = variable->id();
            break;
        }
    }
    if (target == 0) {
        syncControlPoints();
        return;
    }
    const EntityId from = index == -1 ? target : connection->from;
    const EntityId to = index == -2 ? target : connection->to;
    if (!m_controller->reconnectConnection(id, from, to))
        syncControlPoints();
}

void DiagramScene::removeControlPoint(ConnectionId id, int index)
{
    qds::core::Route route = m_controller->document().diagram().route(id);
    if (index < 0 || index >= static_cast<int>(route.controlPoints.size()))
        return;
    route.controlPoints.erase(route.controlPoints.begin() + index);
    route.manual = true;
    m_controller->setRoute(id, route);
}

void DiagramScene::updateAutomaticRoute(ConnectionItem *item)
{
    if (!item)
        return;
    const qds::core::Route route = m_controller->document().diagram().route(item->id());
    if (route.manual || route.locked || route.mode != qds::core::RouteMode::Orthogonal)
        return;
    item->setRoute(m_router.route(
        item->from()->pos(), item->to()->pos(), item->from()->id(), item->to()->id(), m_obstacles));
}

void DiagramScene::rerouteAll()
{
    for (ConnectionItem *item : m_connectionItems)
        updateAutomaticRoute(item);
}

void DiagramScene::toggleSelectedRouteLock()
{
    for (QGraphicsItem *item : selectedItems()) {
        if (ConnectionItem *connection = qgraphicsitem_cast<ConnectionItem *>(item)) {
            qds::core::Route route = m_controller->document().diagram().route(connection->id());
            route.locked = !route.locked;
            m_controller->setRoute(connection->id(), route);
        }
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
