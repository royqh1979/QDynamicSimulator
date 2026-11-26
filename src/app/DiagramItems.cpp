#include "DiagramItems.h"
#include "DiagramPalette.h"

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
        return QString::fromStdString(comment->content());
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
    const QPointF delta = target - center();
    if (delta.isNull())
        return center();
    const QRectF bounds(-60.0, -25.0, 120.0, 50.0);
    const double xScale = std::abs(delta.x()) > 1e-12 ? bounds.width() / 2.0 / std::abs(delta.x()) : 1e12;
    const double yScale = std::abs(delta.y()) > 1e-12 ? bounds.height() / 2.0 / std::abs(delta.y()) : 1e12;
    return center() + delta * std::min(xScale, yScale);
}

void DiagramItem::setEndpointTargetHighlighted(bool highlighted)
{
    if (m_endpointTargetHighlighted == highlighted)
        return;
    m_endpointTargetHighlighted = highlighted;
    update();
}

void DiagramItem::setFontStyle(const qds::core::FontStyle &style)
{
    QFont font;
    if (!style.family.empty())
        font.setFamily(QString::fromStdString(style.family));
    if (style.pointSize > 0)
        font.setPointSize(style.pointSize);
    if (m_labelFont == font)
        return;
    m_labelFont = font;
    update();
}

QPen DiagramItem::outlinePen(const QColor &normalColor, qreal normalWidth, qreal selectedWidth, Qt::PenStyle normalStyle) const
{
    if (m_endpointTargetHighlighted) {
        QPen pen(diagram_palette::EndpointTargetColor, 3.0, Qt::DashLine);
        pen.setCosmetic(true);
        return pen;
    }
    return QPen(isSelected() ? diagram_palette::SelectionColor : normalColor,
        isSelected() ? selectedWidth : normalWidth, normalStyle);
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
    , m_name(QString::fromStdString(variable.displayName()))
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

QRectF VariableItem::labelRect() const
{
    if (m_kind == qds::core::ElementKind::Stock)
        return QRectF(-60.0, -25.0, 120.0, 50.0).adjusted(5.0, 0.0, -5.0, 0.0);
    if (m_kind == qds::core::ElementKind::Auxiliary)
        return QRectF(-45.0, -25.0, 90.0, 50.0).adjusted(5.0, 0.0, -5.0, 0.0);
    if (m_kind == qds::core::ElementKind::Constant)
        return QRectF(-40.0, -15.0, 80.0, 30.0);
    return QRectF(-50.0, -25.0, 100.0, 50.0).adjusted(5.0, 0.0, -5.0, 0.0);
}

void VariableItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setFont(labelFont());
    painter->setPen(outlinePen(diagram_palette::EntityOutlineColor, 1.4, 2.0));
    painter->setBrush(diagram_palette::EntityFillColor);

    const QRectF textRect = labelRect();
    if (m_kind == qds::core::ElementKind::Stock) {
        painter->drawRect(QRectF(-60.0, -25.0, 120.0, 50.0));
    } else if (m_kind == qds::core::ElementKind::Auxiliary) {
        painter->drawEllipse(QRectF(-45.0, -25.0, 90.0, 50.0));
    } else if (m_kind == qds::core::ElementKind::Constant) {
        const QPolygonF diamond({QPointF(0.0, -28.0), QPointF(50.0, 0.0), QPointF(0.0, 28.0), QPointF(-50.0, 0.0)});
        painter->drawPolygon(diamond);
    } else {
        painter->drawRoundedRect(QRectF(-50.0, -25.0, 100.0, 50.0), 8.0, 8.0);
    }

    painter->setPen(diagram_palette::EntityTextColor);
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
    return center() + delta * scale;
}

void VariableItem::refresh(const qds::core::ModelElement &variable, const QPointF &position)
{
    m_name = QString::fromStdString(variable.displayName());
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
    painter->setFont(labelFont());
    const Qt::PenStyle normalStyle = m_kind == qds::core::ElementKind::Cloud ? Qt::DashLine : Qt::SolidLine;
    painter->setPen(outlinePen(diagram_palette::EntityOutlineColor, 1.4, 2.0, normalStyle));
    painter->setBrush(diagram_palette::EntityFillColor);
    QRectF textRect(-60.0, -25.0, 120.0, 50.0);
    if (m_kind == qds::core::ElementKind::Cloud) {
        painter->drawEllipse(QRectF(-45.0, -25.0, 90.0, 50.0));
        textRect = QRectF(-60.0, 28.0, 120.0, 24.0);
    } else {
        painter->setBrush(diagram_palette::CommentFillColor);
        painter->drawRect(textRect);
    }
    painter->setPen(diagram_palette::EntityTextColor);
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
    const QPainterPath path = geometryPath();
    return path.boundingRect().united(path.controlPointRect()).adjusted(-16.0, -16.0, 16.0, 16.0);
}

QPainterPath RoutedItem::shape() const
{
    QPainterPathStroker stroker;
    stroker.setWidth(12.0);
    return stroker.createStroke(geometryPath());
}

void RoutedItem::previewEndpoint(int index, const QPointF &position)
{
    if (index != -1 && index != -2)
        return;
    prepareGeometryChange();
    m_previewEndpoint = index;
    m_previewPosition = position;
    update();
}

void RoutedItem::clearEndpointPreview()
{
    if (m_previewEndpoint == 0)
        return;
    prepareGeometryChange();
    m_previewEndpoint = 0;
    m_previewPosition = {};
    update();
}

void RoutedItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    painter->setRenderHint(QPainter::Antialiasing);
    const QVector<QLineF> guides = controlGuides();
    if (!guides.isEmpty()) {
        QPen guidePen(diagram_palette::ControlGuideColor, 2.0, Qt::DashLine);
        guidePen.setCosmetic(true);
        painter->setPen(guidePen);
        for (const QLineF &guide : guides)
            painter->drawLine(guide);
    }
    const QPen pen = outlinePen(diagram_palette::FlowOutlineColor, 2.4, 3.4);
    painter->setPen(pen);
    painter->setBrush(pen.color());
    for (const QPainterPath &path : linePaths())
        painter->drawPath(path);
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

void RoutedItem::updateLayout(const qds::core::Route &route)
{
    prepareGeometryChange();
    m_route = route;
    update();
}

void RoutedItem::updateLayout(DiagramItem *from, DiagramItem *to)
{
    prepareGeometryChange();
    m_from = from;
    m_to = to;
    update();
}

void RoutedItem::updateLayout()
{
    prepareGeometryChange();
    update();
}

void RoutedItem::setAutomaticRoute(const qds::core::Route &route)
{
    qds::core::Route automatic = route;
    for (qds::core::RoutePoint &point : automatic.controlPoints) {
        const QPointF absolute(point.relative.x, point.relative.y);
        point = routePointFromScene(absolute);
        point.automatic = true;
    }
    updateLayout(automatic);
}

void RoutedItem::setRoute(const qds::core::Route &route)
{
    updateLayout(route);
}

void RoutedItem::rebind(DiagramItem *from, DiagramItem *to)
{
    updateLayout(from, to);
}

void RoutedItem::updatePosition()
{
    updateLayout();
}

void RoutedItem::routeFrame(QPointF &from, QPointF &delta, QPointF &normal) const
{
    const QPointF fromPosition = m_from ? m_from->center() : m_fallbackPosition + QPointF(-60.0, 0.0);
    const QPointF toPosition = m_to ? m_to->center() : m_fallbackPosition + QPointF(60.0, 0.0);
    from = fromPosition;
    delta = toPosition - fromPosition;
    const double length = std::sqrt(delta.x() * delta.x() + delta.y() * delta.y());
    normal = length > 1e-9 ? QPointF(-delta.y() / length, delta.x() / length) : QPointF();
}

QPointF RoutedItem::scenePoint(const qds::core::RoutePoint &point) const
{
    QPointF from;
    QPointF delta;
    QPointF normal;
    routeFrame(from, delta, normal);
    return from + delta * point.relative.x + normal * point.relative.y;
}

qds::core::RoutePoint RoutedItem::routePointFromScene(const QPointF &position) const
{
    QPointF from;
    QPointF delta;
    QPointF normal;
    routeFrame(from, delta, normal);
    const QPointF offset = position - from;
    const double lengthSquared = delta.x() * delta.x() + delta.y() * delta.y();
    if (lengthSquared < 1e-12)
        return {};
    return {{QPointF::dotProduct(offset, delta) / lengthSquared, QPointF::dotProduct(offset, normal)}, false};
}

QVector<QPointF> RoutedItem::points() const
{
    const auto samePoint = [](const QPointF &left, const QPointF &right) {
        return QLineF(left, right).length() < 1e-9;
    };
    QVector<QPointF> controls;
    for (const qds::core::RoutePoint &point : m_previewEndpoint == 0 ? m_route.controlPoints : std::vector<qds::core::RoutePoint> {}) {
        const QPointF candidate = scenePoint(point);
        if ((m_from && samePoint(candidate, m_from->center())) || (m_to && samePoint(candidate, m_to->center())))
            continue;
        if (controls.isEmpty() || !samePoint(candidate, controls.back()))
            controls.append(candidate);
    }

    const QPointF fromTarget = !controls.isEmpty() ? controls.front()
        : m_to ? m_to->center() : m_fallbackPosition;
    const QPointF toTarget = !controls.isEmpty() ? controls.back()
        : m_from ? m_from->center() : m_fallbackPosition;
    const QPointF start = m_previewEndpoint == -1 ? m_previewPosition
        : m_from ? m_from->boundaryPointToward(fromTarget) : m_fallbackPosition + QPointF(-60.0, 0.0);
    const QPointF end = m_previewEndpoint == -2 ? m_previewPosition
        : m_to ? m_to->boundaryPointToward(toTarget) : m_fallbackPosition + QPointF(60.0, 0.0);
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

Qt::PenStyle ArrowItem::lineStyle() const
{
    const auto *source = dynamic_cast<const ViewObjectItem *>(m_from);
    const auto *target = dynamic_cast<const ViewObjectItem *>(m_to);
    const bool commented = (source && source->kind() == qds::core::ElementKind::Comment)
        || (target && target->kind() == qds::core::ElementKind::Comment);
    return commented ? Qt::DashLine : Qt::SolidLine;
}

void ArrowItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    Q_UNUSED(option)
    Q_UNUSED(widget)
    painter->setRenderHint(QPainter::Antialiasing);
    const QVector<QLineF> guides = controlGuides();
    if (!guides.isEmpty()) {
        QPen guidePen(diagram_palette::ControlGuideColor, 2.0, Qt::DashLine);
        guidePen.setCosmetic(true);
        painter->setPen(guidePen);
        for (const QLineF &guide : guides)
            painter->drawLine(guide);
    }
    const QPen pen = outlinePen(diagram_palette::ArrowOutlineColor, 1.4, 2.4, lineStyle());
    painter->setPen(pen);
    painter->drawPath(geometryPath());
    painter->setBrush(pen.color());
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
    , m_name(QString::fromStdString(flow.displayName()))
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

QPointF FlowItem::center() const
{
    return geometryPath().pointAtPercent(0.5);
}

QRectF FlowItem::boundingRect() const
{
    return RoutedItem::boundingRect().united(labelRect().adjusted(-2.0, -2.0, 2.0, 2.0));
}

QRectF FlowItem::labelRect() const
{
    const QPainterPath path = geometryPath();
    if (path.isEmpty())
        return {};
    const QPointF center = path.pointAtPercent(0.5);
    return QRectF(center + QPointF(-60.0, 16.0), QSizeF(120.0, 24.0));
}

void FlowItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    painter->setFont(labelFont());
    RoutedItem::paint(painter, option, widget);
    painter->setBrush(diagram_palette::EntityFillColor);
    for (const QPolygonF &polygon : valvePolygons())
        painter->drawPolygon(polygon);
    const QPainterPath path = geometryPath();
    if (!path.isEmpty()) {
        painter->setPen(diagram_palette::EntityTextColor);
        const QRectF textRect = labelRect();
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
    m_name = QString::fromStdString(flow.displayName());
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
    setAcceptedMouseButtons(Qt::LeftButton);
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
    const QColor color = m_role == Role::ControlPoint ? diagram_palette::SelectionColor
        : m_role == Role::Source ? diagram_palette::SourceHandleColor : diagram_palette::TargetHandleColor;
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
