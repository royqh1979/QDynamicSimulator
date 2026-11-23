#pragma once

#include <qds/core/Model.h>

#include <QGraphicsItem>
#include <QGraphicsObject>

class VariableItem : public QGraphicsObject
{
    Q_OBJECT

public:
    enum { Type = UserType + 1 };

    VariableItem(QString name, qds::core::VariableType variableType, QGraphicsItem *parent = nullptr);

    int type() const override;
    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

    QString name() const;
    qds::core::VariableType variableType() const;
    void setName(const QString &name);

signals:
    void positionChanged(const QString &name, const QPointF &position);

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;

private:
    QString m_name;
    qds::core::VariableType m_variableType;
};

class ConnectionItem : public QGraphicsItem
{
public:
    enum { Type = UserType + 2 };

    ConnectionItem(VariableItem *from, VariableItem *to, QGraphicsItem *parent = nullptr);

    int type() const override;
    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

    bool connects(const QString &name) const;
    VariableItem *from() const;
    VariableItem *to() const;
    void updatePosition();

private:
    QLineF line() const;

    VariableItem *m_from;
    VariableItem *m_to;
};
