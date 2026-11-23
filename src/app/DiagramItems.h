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
        QGraphicsItem *parent = nullptr);

    int type() const override;
    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

    qds::core::ConnectionId id() const;
    bool connects(qds::core::EntityId id) const;
    VariableItem *from() const;
    VariableItem *to() const;
    void updatePosition();

private:
    QLineF line() const;

    qds::core::ConnectionId m_id;
    qds::core::ConnectionType m_connectionType;
    VariableItem *m_from;
    VariableItem *m_to;
};
