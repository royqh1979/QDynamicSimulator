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

namespace {

QString viewObjectText(const qds::core::ViewElement &object)
{
    if (const auto *comment = dynamic_cast<const qds::core::Comment *>(&object))
        return QString::fromStdString(comment->content);
    return {};
}

QPolygonF offsetPolygon(const QPolygonF &points, double offset)
{
    QPolygonF result;
    if (points.size() < 2)
        return result;
    result.reserve(points.size());
    for (int index = 0; index < points.size(); ++index) {
        const QPointF before = index == 0 ? points[0] : points[index - 1];
        const QPointF after = index == points.size() - 1 ? points[index] : points[index + 1];
        const QLineF tangent(before, after);
        if (tangent.length() < 1e-9)
            return {};
        const QPointF normal(-tangent.dy() / tangent.length(), tangent.dx() / tangent.length());
        result.append(points[index] + normal * offset);
    }
    return result;
}

QList<QPolygonF> trimmedPolygons(const QPainterPath &path, double trailingLength)
{
    QList<QPolygonF> result;
    const QList<QPolygonF> polygons = path.toSubpathPolygons();
    for (const QPolygonF &polygon : polygons) {
        if (polygon.size() < 2)
            continue;
        QPolygonF trimmed;
        trimmed.append(polygon.front());
        double remaining = trailingLength;
        for (int index = 1; index < polygon.size(); ++index) {
            const QLineF segment(polygon[index - 1], polygon[index]);
            const double length = segment.length();
            if (length < 1e-9)
                continue;
            if (remaining >= length) {
                trimmed.append(polygon[index]);
                remaining -= length;
                if (remaining <= 1e-9)
                    break;
                continue;
            }
            trimmed.append(segment.pointAt((length - remaining) / length));
            remaining = 0.0;
            break;
        }
        if (trimmed.size() >= 2 && remaining <= 1e-9)
            result.append(trimmed);
    }
    return result;
}

} // namespace

QPointF DiagramItem::boundaryPointToward(const QPointF &target) const
{
    const QPointF delta = target - pos();
    if (delta.isNull())
        return pos();
    const QRectF bounds(-60.0, -25.0, 120.0, 50.0);
    const double xScale = std::abs(delta.x()) > 1e-12 ? bounds.width() / 2.0 / std::abs(delta.x()) : 1e12;
    const double yScale = std::abs(delta.y()) > 1e-12 ? bounds.height() / 2.0 / std::abs(delta.y()) : 1e12;
    return pos() + delta * std::min(xScale, yScale);
}

QVariant DiagramItem::itemChange(GraphicsItemChange change, const QVariant &value)
{
    if (change == ItemPositionHasChanged && scene() && (flags() & ItemIsMovable))
        emit positionChanged(id(), value.toPointF());
    return QGraphicsObject::itemChange(change, value);
}

void DiagramItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    QGraphicsObject::mouseReleaseEvent(event);
    if (flags() & ItemIsMovable)
        emit moveFinished(id(), pos(), event->modifiers());
}

VariableItem::VariableItem(const qds::core::ModelElement &variable, const QPointF &position, QGraphicsItem *parent)
    : DiagramItem(parent)
    , m_id(variable.id())
    , m_name(QString::fromStdString(variable.displayName))
    , m_kind(variable.kind())
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

void VariableItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    painter->setRenderHint(QPainter::Antialiasing);
    QPen pen(isSelected() ? QColor(35, 120, 210) : QColor(45, 55, 65), isSelected() ? 2.0 : 1.4);
    painter->setPen(pen);
    painter->setBrush(Qt::white);

    QRectF textRect;
    if (m_kind == qds::core::ElementKind::Stock) {
        const QRectF stockRect(-60.0, -25.0, 120.0, 50.0);
        painter->drawRect(stockRect);
        textRect = stockRect.adjusted(5.0, 0.0, -5.0, 0.0);
    } else if (m_kind == qds::core::ElementKind::Auxiliary) {
        const QRectF auxiliaryRect(-45.0, -25.0, 90.0, 50.0);
        painter->drawEllipse(auxiliaryRect);
        textRect = auxiliaryRect.adjusted(5.0, 0.0, -5.0, 0.0);
    } else if (m_kind == qds::core::ElementKind::Constant) {
        const QPolygonF diamond({QPointF(0.0, -28.0), QPointF(50.0, 0.0), QPointF(0.0, 28.0), QPointF(-50.0, 0.0)});
        painter->drawPolygon(diamond);
        textRect = QRectF(-40.0, -15.0, 80.0, 30.0);
    } else {
        const QRectF rect(-50.0, -25.0, 100.0, 50.0);
        painter->drawRoundedRect(rect, 8.0, 8.0);
        textRect = rect.adjusted(5.0, 0.0, -5.0, 0.0);
    }

    painter->setPen(QColor(25, 30, 35));
    const QString label = painter->fontMetrics().elidedText(m_name, Qt::ElideRight, static_cast<int>(textRect.width()));
    painter->drawText(textRect, Qt::AlignCenter, label);
}

QPointF VariableItem::boundaryPointToward(const QPointF &target) const
{
    const QPointF delta = target - pos();
    if (delta.isNull())
        return pos();

    double halfWidth = 60.0;
    double halfHeight = 25.0;
    if (m_kind == qds::core::ElementKind::Auxiliary)
        halfWidth = 45.0;
    else if (m_kind == qds::core::ElementKind::Constant)
        halfWidth = 50.0;

    double scale = 0.0;
    if (m_kind == qds::core::ElementKind::Auxiliary) {
        scale = 1.0 / std::sqrt(delta.x() * delta.x() / (halfWidth * halfWidth)
                                + delta.y() * delta.y() / (halfHeight * halfHeight));
    } else if (m_kind == qds::core::ElementKind::Constant) {
        scale = 1.0 / (std::abs(delta.x()) / halfWidth + std::abs(delta.y()) / halfHeight);
    } else {
        const double xScale = std::abs(delta.x()) > 1e-12 ? halfWidth / std::abs(delta.x()) : 1e12;
        const double yScale = std::abs(delta.y()) > 1e-12 ? halfHeight / std::abs(delta.y()) : 1e12;
        scale = std::min(xScale, yScale);
    }
    return pos() + delta * scale;
}

void VariableItem::refresh(const qds::core::ModelElement &variable, const QPointF &position)
{
    m_name = QString::fromStdString(variable.displayName);
    m_kind = variable.kind();
    if (pos() != position)
        setPos(position);
    update();
}

ViewObjectItem::ViewObjectItem(const qds::core::ViewElement &object, const QPointF &position, QGraphicsItem *parent)
    : DiagramItem(parent)
    , m_id(object.id())
    , m_text(viewObjectText(object))
    , m_kind(object.kind())
{
    setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
    setCursor(Qt::OpenHandCursor);
    setPos(position);
}

int ViewObjectItem::type() const
{
    return Type;
}

QRectF ViewObjectItem::boundingRect() const
{
    return QRectF(-65.0, -32.0, 130.0, 72.0);
}

void ViewObjectItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    painter->setRenderHint(QPainter::Antialiasing);
    const QColor color = isSelected() ? QColor(35, 120, 210) : QColor(45, 55, 65);
    QPen pen(color, isSelected() ? 2.0 : 1.4);
    painter->setPen(pen);
    painter->setBrush(Qt::white);
    QRectF textRect(-60.0, -25.0, 120.0, 50.0);
    if (m_kind == qds::core::ElementKind::Cloud) {
        painter->setPen(QPen(color, pen.width(), Qt::DashLine));
        painter->drawEllipse(QRectF(-45.0, -25.0, 90.0, 50.0));
        textRect = QRectF(-60.0, 28.0, 120.0, 24.0);
    } else {
        painter->setBrush(QColor(255, 250, 205));
        painter->drawRect(textRect);
    }
    painter->setPen(QColor(25, 30, 35));
    painter->drawText(textRect, Qt::AlignCenter,
                      painter->fontMetrics().elidedText(m_text, Qt::ElideRight, static_cast<int>(textRect.width())));
}

QPointF ViewObjectItem::boundaryPointToward(const QPointF &target) const
{
    if (m_kind != qds::core::ElementKind::Cloud)
        return DiagramItem::boundaryPointToward(target);
    const QPointF delta = target - pos();
    if (delta.isNull())
        return pos();
    const double scale = 1.0 / std::sqrt(delta.x() * delta.x() / (45.0 * 45.0) + delta.y() * delta.y() / 625.0);
    return pos() + delta * scale;
}

void ViewObjectItem::refresh(const qds::core::ViewElement &object, const QPointF &position)
{
    m_text = viewObjectText(object);
    if (pos() != position)
        setPos(position);
    update();
}

RoutedItem::RoutedItem(DiagramItem *from, DiagramItem *to, const qds::core::Route &route, QGraphicsItem *parent)
    : DiagramItem(parent)
    , m_from(from)
    , m_to(to)
    , m_route(route)
{
    setFlag(ItemIsSelectable);
    setZValue(1.0);
}

QRectF RoutedItem::boundingRect() const
{
    return geometryPath().boundingRect().adjusted(-16.0, -16.0, 16.0, 16.0);
}

QPainterPath RoutedItem::shape() const
{
    QPainterPathStroker stroker;
    stroker.setWidth(12.0);
    return stroker.createStroke(geometryPath());
}

void RoutedItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    painter->setRenderHint(QPainter::Antialiasing);
    const QVector<QLineF> guides = controlGuides();
    if (!guides.isEmpty()) {
        QPen guidePen(QColor(70, 95, 120), 2.0, Qt::DashLine);
        guidePen.setCosmetic(true);
        painter->setPen(guidePen);
        for (const QLineF &guide : guides)
            painter->drawLine(guide);
    }
    const QColor color = isSelected() ? QColor(35, 120, 210) : QColor(45, 100, 160);
    painter->setPen(QPen(color, isSelected() ? 3.4 : 2.4));
    for (const QPainterPath &path : linePaths())
        painter->drawPath(path);
    painter->setBrush(color);
    const QPolygonF arrow = arrowHead();
    if (!arrow.isEmpty())
        painter->drawPolygon(arrow);
}

bool RoutedItem::connects(qds::core::EntityId id) const
{
    return (m_from && m_from->id() == id) || (m_to && m_to->id() == id);
}

QVector<QPainterPath> RoutedItem::linePaths() const
{
    return {geometryPath()};
}

QPainterPath RoutedItem::geometryPath() const
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

qreal RoutedItem::arrowSize() const
{
    return 10.0;
}

QPolygonF RoutedItem::arrowHead() const
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
    const qreal arrowSize = this->arrowSize();
    constexpr qreal pi = 3.14159265358979323846;
    const QPointF arrow1 = end - QPointF(std::cos(angle + pi / 6.0) * arrowSize, -std::sin(angle + pi / 6.0) * arrowSize);
    const QPointF arrow2 = end - QPointF(std::cos(angle - pi / 6.0) * arrowSize, -std::sin(angle - pi / 6.0) * arrowSize);
    return QPolygonF({end, arrow1, arrow2});
}

QVector<QLineF> RoutedItem::controlGuides() const
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

void RoutedItem::setControlGuidesVisible(bool visible)
{
    if (m_controlGuidesVisible == visible)
        return;
    m_controlGuidesVisible = visible;
    update();
}

void RoutedItem::setRoute(const qds::core::Route &route)
{
    prepareGeometryChange();
    m_route = route;
    update();
}

void RoutedItem::rebind(DiagramItem *from, DiagramItem *to)
{
    prepareGeometryChange();
    m_from = from;
    m_to = to;
    update();
}

void RoutedItem::updatePosition()
{
    prepareGeometryChange();
    update();
}

QVector<QPointF> RoutedItem::points() const
{
    const auto samePoint = [](const QPointF &left, const QPointF &right) {
        return QLineF(left, right).length() < 1e-9;
    };
    QVector<QPointF> controls;
    for (const qds::core::Vec2 &point : m_route.controlPoints) {
        const QPointF candidate(point.x, point.y);
        if ((m_from && samePoint(candidate, m_from->pos())) || (m_to && samePoint(candidate, m_to->pos())))
            continue;
        if (controls.isEmpty() || !samePoint(candidate, controls.back()))
            controls.append(candidate);
    }

    const QPointF fromTarget = !controls.isEmpty() ? controls.front()
        : m_to ? m_to->pos() : m_fallbackPosition;
    const QPointF toTarget = !controls.isEmpty() ? controls.back()
        : m_from ? m_from->pos() : m_fallbackPosition;
    const QPointF start = m_from ? m_from->boundaryPointToward(fromTarget) : m_fallbackPosition + QPointF(-60.0, 0.0);
    const QPointF end = m_to ? m_to->boundaryPointToward(toTarget) : m_fallbackPosition + QPointF(60.0, 0.0);
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

ArrowItem::ArrowItem(qds::core::EntityId id, DiagramItem *from, DiagramItem *to, const qds::core::Route &route, QGraphicsItem *parent)
    : RoutedItem(from, to, route, parent)
    , m_id(id)
{
}

int ArrowItem::type() const
{
    return Type;
}

void ArrowItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    Q_UNUSED(option)
    Q_UNUSED(widget)
    painter->setRenderHint(QPainter::Antialiasing);
    const QVector<QLineF> guides = controlGuides();
    if (!guides.isEmpty()) {
        QPen guidePen(QColor(70, 95, 120), 2.0, Qt::DashLine);
        guidePen.setCosmetic(true);
        painter->setPen(guidePen);
        for (const QLineF &guide : guides)
            painter->drawLine(guide);
    }
    const QColor color = isSelected() ? QColor(35, 120, 210) : QColor(75, 85, 95);
    painter->setPen(QPen(color, isSelected() ? 2.4 : 1.4, Qt::DashLine));
    painter->drawPath(geometryPath());
    painter->setBrush(color);
    const QPolygonF arrow = arrowHead();
    if (!arrow.isEmpty())
        painter->drawPolygon(arrow);
}

FlowItem::FlowItem(
    const qds::core::Flow &flow,
    DiagramItem *from,
    DiagramItem *to,
    const qds::core::Route &route,
    const QPointF &fallbackPosition,
    QGraphicsItem *parent)
    : RoutedItem(from, to, route, parent)
    , m_id(flow.id())
    , m_name(QString::fromStdString(flow.displayName))
{
    m_fallbackPosition = fallbackPosition;
}

int FlowItem::type() const
{
    return Type;
}

QVector<QPainterPath> FlowItem::linePaths() const
{
    const QList<QPolygonF> polygons = trimmedPolygons(geometryPath(), arrowSize());
    QVector<QPainterPath> result;
    if (polygons.isEmpty())
        return result;
    for (double offset : {-3.0, 3.0}) {
        QPainterPath path;
        for (const QPolygonF &polygon : polygons) {
            const QPolygonF shifted = offsetPolygon(polygon, offset);
            if (shifted.size() < 2)
                continue;
            path.moveTo(shifted.front());
            for (int index = 1; index < shifted.size(); ++index)
                path.lineTo(shifted[index]);
        }
        result.append(path);
    }
    return result;
}

void FlowItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    RoutedItem::paint(painter, option, widget);
    painter->setBrush(Qt::white);
    for (const QPolygonF &polygon : valvePolygons())
        painter->drawPolygon(polygon);
    const QPainterPath path = geometryPath();
    if (!path.isEmpty()) {
        const QPointF center = path.pointAtPercent(0.5);
        painter->setPen(QColor(25, 30, 35));
        const QRectF textRect(center + QPointF(-60.0, 16.0), QSizeF(120.0, 24.0));
        painter->drawText(textRect, Qt::AlignCenter, painter->fontMetrics().elidedText(m_name, Qt::ElideRight, 120));
    }
}

QVector<QPolygonF> FlowItem::valvePolygons() const
{
    const QPainterPath path = geometryPath();
    if (path.length() < 1e-9)
        return {};
    const QPointF center = path.pointAtPercent(0.5);
    const QLineF tangent(path.pointAtPercent(0.4999), path.pointAtPercent(0.5001));
    if (tangent.length() < 1e-9)
        return {};
    const QPointF direction(tangent.dx() / tangent.length(), tangent.dy() / tangent.length());
    const QPointF normal(-direction.y(), direction.x());
    const QPointF positive = center + normal * 12.0;
    const QPointF negative = center - normal * 12.0;
    return {
        QPolygonF({positive - direction * 10.0, center, positive + direction * 10.0}),
        QPolygonF({negative - direction * 10.0, center, negative + direction * 10.0})};
}

void FlowItem::refresh(
    const qds::core::Flow &flow,
    DiagramItem *from,
    DiagramItem *to,
    const QPointF &fallbackPosition)
{
    prepareGeometryChange();
    m_name = QString::fromStdString(flow.displayName);
    m_from = from;
    m_to = to;
    m_fallbackPosition = fallbackPosition;
    update();
}

ControlPointHandle::ControlPointHandle(qds::core::EntityId id, int index, Role role, const QPointF &position)
    : m_routeId(id)
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
    const QColor color = m_role == Role::ControlPoint ? QColor(35, 120, 210)
        : m_role == Role::Source ? QColor(50, 160, 90) : QColor(210, 100, 45);
    painter->setPen(QPen(Qt::white, 1.5));
    painter->setBrush(color);
    painter->drawEllipse(QRectF(-5.0, -5.0, 10.0, 10.0));
}

void ControlPointHandle::assign(qds::core::EntityId id, int index, Role role, const QPointF &position)
{
    const QSignalBlocker blocker(this);
    m_routeId = id;
    m_index = index;
    m_role = role;
    setPos(position);
    setEnabled(true);
    show();
}

QVariant ControlPointHandle::itemChange(GraphicsItemChange change, const QVariant &value)
{
    if (change == ItemPositionHasChanged && scene())
        emit moved(m_routeId, m_index, value.toPointF());
    return QGraphicsObject::itemChange(change, value);
}

void ControlPointHandle::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    QGraphicsObject::mouseReleaseEvent(event);
    emit finished(m_routeId, m_index, pos());
}

void ControlPointHandle::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event)
{
    if (m_index >= 0) {
        emit removeRequested(m_routeId, m_index);
        event->accept();
        return;
    }
    QGraphicsObject::mouseDoubleClickEvent(event);
}
