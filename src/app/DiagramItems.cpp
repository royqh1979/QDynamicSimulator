#include "DiagramItems.h"

#include <QFontMetrics>
#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOptionGraphicsItem>

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
    QGraphicsItem *parent)
    : QGraphicsItem(parent)
    , m_id(id)
    , m_connectionType(connectionType)
    , m_from(from)
    , m_to(to)
{
    setZValue(-1.0);
}

int ConnectionItem::type() const
{
    return Type;
}

QRectF ConnectionItem::boundingRect() const
{
    return QRectF(line().p1(), line().p2()).normalized().adjusted(-12.0, -12.0, 12.0, 12.0);
}

QPainterPath ConnectionItem::shape() const
{
    QPainterPath path(line().p1());
    path.lineTo(line().p2());
    QPainterPathStroker stroker;
    stroker.setWidth(12.0);
    return stroker.createStroke(path);
}

void ConnectionItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    painter->setRenderHint(QPainter::Antialiasing);
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
    painter->setPen(QPen(color, width, style));
    const QLineF connection = line();
    if (connection.length() < 1.0)
        return;

    painter->drawLine(connection);
    const qreal angle = std::atan2(-connection.dy(), connection.dx());
    const qreal arrowSize = 10.0;
    constexpr qreal pi = 3.14159265358979323846;
    const QPointF arrow1 = connection.p2() - QPointF(
        std::cos(angle + pi / 6.0) * arrowSize,
        -std::sin(angle + pi / 6.0) * arrowSize);
    const QPointF arrow2 = connection.p2() - QPointF(
        std::cos(angle - pi / 6.0) * arrowSize,
        -std::sin(angle - pi / 6.0) * arrowSize);
    painter->setBrush(color);
    painter->drawPolygon(QPolygonF({connection.p2(), arrow1, arrow2}));
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

void ConnectionItem::updatePosition()
{
    prepareGeometryChange();
    update();
}

QLineF ConnectionItem::line() const
{
    const QPointF start = m_from->pos();
    const QPointF end = m_to->pos();
    const QLineF centerLine(start, end);
    if (centerLine.length() <= 120.0)
        return centerLine;

    const QPointF direction = (end - start) / centerLine.length();
    return QLineF(start + direction * 55.0, end - direction * 55.0);
}
