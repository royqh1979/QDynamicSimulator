#include "DiagramItems.h"

#include <QFontMetrics>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSignalBlocker>
#include <QStyleOptionGraphicsItem>

#include <algorithm>
#include <cmath>

VariableItem::VariableItem(const qds::core::Variable &variable, const QPointF &position, QGraphicsItem *parent)
    : QGraphicsObject(parent)
    , m_id(variable.id)
    , m_name(QString::fromStdString(variable.name))
    , m_variableType(variable.type)
{
    setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
    setCursor(Qt::OpenHandCursor);
    setPos(position);
}

int VariableItem::type() const
{
    return Type;
}

QRectF VariableItem::boundingRect() const
{
    return QRectF(-65.0, -32.0, 130.0, 72.0);
}

void VariableItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *)
{
    Q_UNUSED(option)
    painter->setRenderHint(QPainter::Antialiasing);
    QPen pen(isSelected() ? QColor(35, 120, 210) : QColor(45, 55, 65), isSelected() ? 2.0 : 1.4);
    painter->setPen(pen);
    painter->setBrush(Qt::white);

    QRectF textRect;
    if (m_variableType == qds::core::VariableType::Stock) {
        const QRectF stockRect(-60.0, -25.0, 120.0, 50.0);
        painter->drawRect(stockRect);
        textRect = stockRect.adjusted(5.0, 0.0, -5.0, 0.0);
    } else if (m_variableType == qds::core::VariableType::Flow) {
        painter->drawLine(QPointF(-60.0, 0.0), QPointF(60.0, 0.0));
        painter->drawLine(QPointF(50.0, -6.0), QPointF(60.0, 0.0));
        painter->drawLine(QPointF(50.0, 6.0), QPointF(60.0, 0.0));
        const QPolygonF leftValve({QPointF(-12.0, -10.0), QPointF(0.0, 0.0), QPointF(-12.0, 10.0)});
        const QPolygonF rightValve({QPointF(12.0, -10.0), QPointF(0.0, 0.0), QPointF(12.0, 10.0)});
        painter->drawPolygon(leftValve);
        painter->drawPolygon(rightValve);
        textRect = QRectF(-60.0, 12.0, 120.0, 24.0);
    } else if (m_variableType == qds::core::VariableType::Auxiliary) {
        const QRectF auxiliaryRect(-45.0, -25.0, 90.0, 50.0);
        painter->drawEllipse(auxiliaryRect);
        textRect = auxiliaryRect.adjusted(5.0, 0.0, -5.0, 0.0);
    } else if (m_variableType == qds::core::VariableType::Constant) {
        const QPolygonF diamond({QPointF(0.0, -28.0), QPointF(50.0, 0.0), QPointF(0.0, 28.0), QPointF(-50.0, 0.0)});
        painter->drawPolygon(diamond);
        textRect = QRectF(-40.0, -15.0, 80.0, 30.0);
    } else if (m_variableType == qds::core::VariableType::Exogenous) {
        const QRectF rect(-50.0, -25.0, 100.0, 50.0);
        painter->drawRoundedRect(rect, 8.0, 8.0);
        textRect = rect.adjusted(5.0, 0.0, -5.0, 0.0);
    } else if (m_variableType == qds::core::VariableType::Cloud) {
        const QRectF cloudRect(-45.0, -25.0, 90.0, 50.0);
        painter->setPen(QPen(pen.color(), pen.width(), Qt::DashLine));
        painter->drawEllipse(cloudRect);
        textRect = QRectF(-60.0, 28.0, 120.0, 24.0);
    } else {
        const QRectF noteRect(-60.0, -25.0, 120.0, 50.0);
        painter->setBrush(QColor(255, 250, 205));
        painter->drawRect(noteRect);
        textRect = noteRect.adjusted(5.0, 0.0, -5.0, 0.0);
    }

    painter->setPen(QColor(25, 30, 35));
    const QString label = painter->fontMetrics().elidedText(m_name, Qt::ElideRight, static_cast<int>(textRect.width()));
    painter->drawText(textRect, Qt::AlignCenter, label);
}

qds::core::EntityId VariableItem::id() const
{
    return m_id;
}

QString VariableItem::name() const
{
    return m_name;
}

qds::core::VariableType VariableItem::variableType() const
{
    return m_variableType;
}

QPointF VariableItem::boundaryPointToward(const QPointF &target) const
{
    const QPointF delta = target - pos();
    if (delta.isNull())
        return pos();

    double halfWidth = 60.0;
    double halfHeight = 25.0;
    if (m_variableType == qds::core::VariableType::Auxiliary || m_variableType == qds::core::VariableType::Cloud)
        halfWidth = 45.0;
    else if (m_variableType == qds::core::VariableType::Constant)
        halfWidth = 50.0;

    double scale = 0.0;
    if (m_variableType == qds::core::VariableType::Auxiliary || m_variableType == qds::core::VariableType::Cloud) {
        scale = 1.0 / std::sqrt(
            delta.x() * delta.x() / (halfWidth * halfWidth)
            + delta.y() * delta.y() / (halfHeight * halfHeight));
    } else if (m_variableType == qds::core::VariableType::Constant) {
        scale = 1.0 / (std::abs(delta.x()) / halfWidth + std::abs(delta.y()) / halfHeight);
    } else {
        const double xScale = std::abs(delta.x()) > 1e-12 ? halfWidth / std::abs(delta.x()) : 1e12;
        const double yScale = std::abs(delta.y()) > 1e-12 ? halfHeight / std::abs(delta.y()) : 1e12;
        scale = std::min(xScale, yScale);
    }
    return pos() + delta * scale;
}

void VariableItem::refresh(const qds::core::Variable &variable, const QPointF &position)
{
    m_name = QString::fromStdString(variable.name);
    m_variableType = variable.type;
    if (pos() != position)
        setPos(position);
    update();
}

QVariant VariableItem::itemChange(GraphicsItemChange change, const QVariant &value)
{
    if (change == ItemPositionHasChanged && scene())
        emit positionChanged(m_id, value.toPointF());
    return QGraphicsObject::itemChange(change, value);
}

void VariableItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    QGraphicsObject::mouseReleaseEvent(event);
    emit moveFinished(m_id, pos());
}

ConnectionItem::ConnectionItem(
    qds::core::ConnectionId id,
    qds::core::ConnectionType connectionType,
    VariableItem *from,
    VariableItem *to,
    const qds::core::Route &route,
    QGraphicsItem *parent)
    : QGraphicsItem(parent)
    , m_id(id)
    , m_connectionType(connectionType)
    , m_from(from)
    , m_to(to)
    , m_route(route)
{
    setFlag(ItemIsSelectable);
    setZValue(1.0);
}

int ConnectionItem::type() const
{
    return Type;
}

QRectF ConnectionItem::boundingRect() const
{
    return geometryPath().boundingRect().adjusted(-12.0, -12.0, 12.0, 12.0);
}

QPainterPath ConnectionItem::shape() const
{
    QPainterPathStroker stroker;
    stroker.setWidth(12.0);
    return stroker.createStroke(geometryPath());
}

void ConnectionItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    painter->setRenderHint(QPainter::Antialiasing);
    const QVector<QLineF> guides = controlGuides();
    if (!guides.isEmpty()) {
        painter->save();
        QPen guidePen(QColor(70, 95, 120), 2.0, Qt::DashLine);
        guidePen.setCosmetic(true);
        painter->setPen(guidePen);
        for (const QLineF &guide : guides)
            painter->drawLine(guide);
        painter->restore();
    }
    QColor color(75, 85, 95);
    qreal width = 1.4;
    Qt::PenStyle style = Qt::DashLine;
    if (m_connectionType == qds::core::ConnectionType::MaterialFlow) {
        color = QColor(45, 100, 160);
        width = 2.4;
        style = Qt::SolidLine;
    } else if (m_connectionType == qds::core::ConnectionType::Annotation) {
        color = QColor(150, 120, 40);
        style = Qt::DotLine;
    }
    if (isSelected()) {
        color = QColor(35, 120, 210);
        width += 1.0;
        style = Qt::SolidLine;
    }
    painter->setPen(QPen(color, width, style));
    const QPainterPath path = geometryPath();
    painter->drawPath(path);

    painter->setBrush(color);
    const QPolygonF arrow = arrowHead();
    if (!arrow.isEmpty())
        painter->drawPolygon(arrow);
}

QPolygonF ConnectionItem::arrowHead() const
{
    const QVector<QPointF> pathPoints = points();
    if (pathPoints.size() < 2)
        return {};
    const QPointF end = pathPoints.back();
    const QPointF before = m_route.mode == qds::core::RouteMode::Bezier && pathPoints.size() >= 4
        ? pathPoints[2]
        : pathPoints[pathPoints.size() - 2];
    const QLineF tangent(before, end);
    if (tangent.length() < 1e-9)
        return {};
    const qreal angle = std::atan2(-tangent.dy(), tangent.dx());
    const qreal arrowSize = 10.0;
    constexpr qreal pi = 3.14159265358979323846;
    const QPointF arrow1 = end - QPointF(std::cos(angle + pi / 6.0) * arrowSize, -std::sin(angle + pi / 6.0) * arrowSize);
    const QPointF arrow2 = end - QPointF(std::cos(angle - pi / 6.0) * arrowSize, -std::sin(angle - pi / 6.0) * arrowSize);
    return QPolygonF({end, arrow1, arrow2});
}

QVector<QLineF> ConnectionItem::controlGuides() const
{
    if (!m_controlGuidesVisible || m_route.mode != qds::core::RouteMode::Bezier)
        return {};
    const QVector<QPointF> pathPoints = points();
    if (pathPoints.size() < 3)
        return {};
    QVector<QLineF> guides {QLineF(pathPoints.front(), pathPoints[1])};
    const QPointF finalControl = pathPoints.size() >= 4 ? pathPoints[2] : pathPoints[1];
    guides.append(QLineF(finalControl, pathPoints.back()));
    return guides;
}

void ConnectionItem::setControlGuidesVisible(bool visible)
{
    if (m_controlGuidesVisible == visible)
        return;
    m_controlGuidesVisible = visible;
    update();
}

qds::core::ConnectionId ConnectionItem::id() const
{
    return m_id;
}

bool ConnectionItem::connects(qds::core::EntityId id) const
{
    return m_from->id() == id || m_to->id() == id;
}

VariableItem *ConnectionItem::from() const
{
    return m_from;
}

VariableItem *ConnectionItem::to() const
{
    return m_to;
}

QPainterPath ConnectionItem::geometryPath() const
{
    const QVector<QPointF> pathPoints = points();
    QPainterPath path;
    if (pathPoints.size() < 2)
        return path;
    path.moveTo(pathPoints.front());
    if (m_route.mode == qds::core::RouteMode::Bezier && pathPoints.size() >= 4)
        path.cubicTo(pathPoints[1], pathPoints[2], pathPoints.back());
    else if (m_route.mode == qds::core::RouteMode::Bezier && pathPoints.size() == 3)
        path.quadTo(pathPoints[1], pathPoints.back());
    else {
        for (int index = 1; index < pathPoints.size(); ++index)
            path.lineTo(pathPoints[index]);
    }
    return path;
}

void ConnectionItem::setRoute(const qds::core::Route &route)
{
    prepareGeometryChange();
    m_route = route;
    update();
}

void ConnectionItem::rebind(VariableItem *from, VariableItem *to)
{
    prepareGeometryChange();
    m_from = from;
    m_to = to;
    update();
}

void ConnectionItem::updatePosition()
{
    prepareGeometryChange();
    update();
}

QVector<QPointF> ConnectionItem::points() const
{
    const auto samePoint = [](const QPointF &left, const QPointF &right) {
        return QLineF(left, right).length() < 1e-9;
    };
    QVector<QPointF> controls;
    for (const qds::core::Vec2 &point : m_route.controlPoints) {
        const QPointF candidate(point.x, point.y);
        if (samePoint(candidate, m_from->pos()) || samePoint(candidate, m_to->pos()))
            continue;
        if (controls.isEmpty() || !samePoint(candidate, controls.back()))
            controls.append(candidate);
    }

    const QPointF fromTarget = controls.isEmpty() ? m_to->pos() : controls.front();
    const QPointF toTarget = controls.isEmpty() ? m_from->pos() : controls.back();
    const QPointF start = m_from->boundaryPointToward(fromTarget);
    const QPointF end = m_to->boundaryPointToward(toTarget);
    QVector<QPointF> result;
    if (m_route.mode == qds::core::RouteMode::Straight || m_route.mode == qds::core::RouteMode::Bezier
        || !controls.isEmpty()) {
        result = {start};
        result += controls;
        result.append(end);
    } else {
        const double middleX = (start.x() + end.x()) / 2.0;
        result = {start, QPointF(middleX, start.y()), QPointF(middleX, end.y()), end};
    }

    QVector<QPointF> normalized;
    for (const QPointF &point : result) {
        if (normalized.isEmpty() || !samePoint(point, normalized.back()))
            normalized.append(point);
    }
    return normalized;
}

ControlPointHandle::ControlPointHandle(qds::core::ConnectionId id, int index, Role role, const QPointF &position)
    : m_connectionId(id)
    , m_index(index)
    , m_role(role)
{
    setFlags(ItemIsMovable | ItemSendsGeometryChanges);
    setFlag(ItemIgnoresTransformations);
    setZValue(10.0);
    setPos(position);
}

int ControlPointHandle::type() const
{
    return Type;
}

QRectF ControlPointHandle::boundingRect() const
{
    return QRectF(-7.0, -7.0, 14.0, 14.0);
}

void ControlPointHandle::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    painter->setRenderHint(QPainter::Antialiasing);
    QColor color = m_role == Role::ControlPoint ? QColor(35, 120, 210)
        : m_role == Role::Source ? QColor(50, 160, 90) : QColor(210, 100, 45);
    painter->setPen(QPen(Qt::white, 1.5));
    painter->setBrush(color);
    painter->drawEllipse(QRectF(-5.0, -5.0, 10.0, 10.0));
}

qds::core::ConnectionId ControlPointHandle::connectionId() const
{
    return m_connectionId;
}

int ControlPointHandle::index() const
{
    return m_index;
}

ControlPointHandle::Role ControlPointHandle::role() const
{
    return m_role;
}

void ControlPointHandle::assign(qds::core::ConnectionId id, int index, Role role, const QPointF &position)
{
    const QSignalBlocker blocker(this);
    m_connectionId = id;
    m_index = index;
    m_role = role;
    setPos(position);
    setEnabled(true);
    show();
}

QVariant ControlPointHandle::itemChange(GraphicsItemChange change, const QVariant &value)
{
    if (change == ItemPositionHasChanged && scene())
        emit moved(m_connectionId, m_index, value.toPointF());
    return QGraphicsObject::itemChange(change, value);
}

void ControlPointHandle::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    QGraphicsObject::mouseReleaseEvent(event);
    emit finished(m_connectionId, m_index, pos());
}

void ControlPointHandle::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event)
{
    if (m_index >= 0) {
        emit removeRequested(m_connectionId, m_index);
        event->accept();
        return;
    }
    QGraphicsObject::mouseDoubleClickEvent(event);
}
