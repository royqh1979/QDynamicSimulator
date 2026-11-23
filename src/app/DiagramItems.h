#pragma once

#include <qds/core/Document.h>

#include <QGraphicsItem>
#include <QGraphicsObject>

class VariableItem : public QGraphicsObject
{
    Q_OBJECT

public:
    enum { Type = UserType + 1 };

    VariableItem(const qds::core::Variable &variable, const QPointF &position, QGraphicsItem *parent = nullptr);

    int type() const override;
    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

    qds::core::EntityId id() const;
    QString name() const;
    qds::core::VariableType variableType() const;
    QPointF boundaryPointToward(const QPointF &target) const;
    void refresh(const qds::core::Variable &variable, const QPointF &position);

signals:
    void positionChanged(qds::core::EntityId id, const QPointF &position);
    void moveFinished(qds::core::EntityId id, const QPointF &position);

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;

private:
    qds::core::EntityId m_id;
    QString m_name;
    qds::core::VariableType m_variableType;
};

class ConnectionItem : public QGraphicsItem
{
public:
    enum { Type = UserType + 2 };

    ConnectionItem(
        qds::core::ConnectionId id,
        qds::core::ConnectionType connectionType,
        VariableItem *from,
        VariableItem *to,
        const qds::core::Route &route,
        QGraphicsItem *parent = nullptr);

    int type() const override;
    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

    qds::core::ConnectionId id() const;
    bool connects(qds::core::EntityId id) const;
    VariableItem *from() const;
    VariableItem *to() const;
    QPainterPath geometryPath() const;
    QPolygonF arrowHead() const;
    QVector<QLineF> controlGuides() const;
    void setControlGuidesVisible(bool visible);
    void setRoute(const qds::core::Route &route);
    void rebind(VariableItem *from, VariableItem *to);
    void updatePosition();

private:
    QVector<QPointF> points() const;

    qds::core::ConnectionId m_id;
    qds::core::ConnectionType m_connectionType;
    VariableItem *m_from;
    VariableItem *m_to;
    qds::core::Route m_route;
    bool m_controlGuidesVisible = false;
};

class ControlPointHandle : public QGraphicsObject
{
    Q_OBJECT

public:
    enum class Role {
        ControlPoint,
        Source,
        Target
    };
    enum { Type = UserType + 3 };

    ControlPointHandle(qds::core::ConnectionId id, int index, Role role, const QPointF &position);

    int type() const override;
    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;
    qds::core::ConnectionId connectionId() const;
    int index() const;
    Role role() const;
    void assign(qds::core::ConnectionId id, int index, Role role, const QPointF &position);

signals:
    void moved(qds::core::ConnectionId id, int index, const QPointF &position);
    void finished(qds::core::ConnectionId id, int index, const QPointF &position);
    void removeRequested(qds::core::ConnectionId id, int index);

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) override;

private:
    qds::core::ConnectionId m_connectionId;
    int m_index;
    Role m_role;
};
