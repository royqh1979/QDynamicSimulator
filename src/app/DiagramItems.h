#pragma once

#include <core/Document.h>

#include <QFont>
#include <QGraphicsObject>
#include <QPen>

class DiagramItem : public QGraphicsObject
{
    Q_OBJECT

public:
    explicit DiagramItem(QGraphicsItem *parent = nullptr)
        : QGraphicsObject(parent)
    {
    }

    virtual qds::core::EntityId id() const = 0;
    virtual QPointF center() const { return pos(); }
    virtual QRectF labelRect() const { return {}; }
    virtual QPointF boundaryPointToward(const QPointF &target) const;
    void setEndpointTargetHighlighted(bool highlighted);
    bool isEndpointTargetHighlighted() const { return m_endpointTargetHighlighted; }
    void setFontStyle(const qds::core::FontStyle &font);
    QFont labelFont() const { return m_labelFont; }

signals:
    void positionChanged(qds::core::EntityId id, const QPointF &position);
    void moveFinished(qds::core::EntityId id, const QPointF &position, Qt::KeyboardModifiers modifiers);

protected:
    QPen outlinePen(const QColor &normalColor, qreal normalWidth, qreal selectedWidth,
        Qt::PenStyle normalStyle = Qt::SolidLine) const;
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;

private:
    bool m_endpointTargetHighlighted = false;
    QFont m_labelFont;
};

class VariableItem : public DiagramItem
{
public:
    enum { Type = UserType + 1 };

    VariableItem(const qds::core::ModelElement &variable, const QPointF &position, QGraphicsItem *parent = nullptr);

    int type() const override;
    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

    qds::core::EntityId id() const override { return m_id; }
    QString name() const { return m_name; }
    qds::core::ElementKind elementKind() const { return m_kind; }
    QRectF labelRect() const override;
    QPointF boundaryPointToward(const QPointF &target) const override;
    void refresh(const qds::core::ModelElement &variable, const QPointF &position);

private:
    qds::core::EntityId m_id;
    QString m_name;
    qds::core::ElementKind m_kind;
};

class ViewObjectItem : public DiagramItem
{
public:
    enum { Type = UserType + 5 };

    ViewObjectItem(const qds::core::ViewElement &object, const QPointF &position, QGraphicsItem *parent = nullptr);

    int type() const override;
    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;
    qds::core::EntityId id() const override { return m_id; }
    qds::core::ElementKind kind() const { return m_kind; }
    QString text() const { return m_text; }
    QPointF boundaryPointToward(const QPointF &target) const override;
    void refresh(const qds::core::ViewElement &object, const QPointF &position);

private:
    qds::core::EntityId m_id;
    QString m_text;
    qds::core::ElementKind m_kind;
};

class RoutedItem : public DiagramItem
{
public:
    RoutedItem(DiagramItem *from, DiagramItem *to, const qds::core::Route &route, QGraphicsItem *parent = nullptr);

    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

    bool connects(qds::core::EntityId id) const;
    DiagramItem *from() const { return m_from; }
    DiagramItem *to() const { return m_to; }
    const qds::core::Route &route() const { return m_route; }
    bool previewing() const { return m_previewEndpoint != 0; }
    void previewEndpoint(int index, const QPointF &position);
    void clearEndpointPreview();
    QPainterPath geometryPath() const;
    QPointF scenePoint(const qds::core::RoutePoint &point) const;
    qds::core::RoutePoint routePointFromScene(const QPointF &position) const;
    QPolygonF arrowHead() const;
    virtual qreal arrowSize() const;
    virtual QVector<QPainterPath> linePaths() const;
    QVector<QLineF> controlGuides() const;
    void setControlGuidesVisible(bool visible);
    void updateLayout(const qds::core::Route &route);
    void updateLayout(DiagramItem *from, DiagramItem *to);
    void updateLayout();
    void setAutomaticRoute(const qds::core::Route &route);
    void setRoute(const qds::core::Route &route);
    void rebind(DiagramItem *from, DiagramItem *to);
    void updatePosition();

protected:
    QVector<QPointF> points() const;
    void routeFrame(QPointF &from, QPointF &delta, QPointF &normal) const;

    DiagramItem *m_from;
    DiagramItem *m_to;
    qds::core::Route m_route;
    QPointF m_fallbackPosition;
    bool m_controlGuidesVisible = false;
    int m_previewEndpoint = 0;
    QPointF m_previewPosition;
};

class ArrowItem : public RoutedItem
{
public:
    enum { Type = UserType + 2 };

    ArrowItem(qds::core::EntityId id, DiagramItem *from, DiagramItem *to, const qds::core::Route &route, QGraphicsItem *parent = nullptr);

    int type() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;
    qds::core::EntityId id() const override { return m_id; }
    Qt::PenStyle lineStyle() const;

private:
    qds::core::EntityId m_id;
};

class FlowItem : public RoutedItem
{
public:
    enum { Type = UserType + 4 };

    FlowItem(
        const qds::core::Flow &flow,
        DiagramItem *from,
        DiagramItem *to,
        const qds::core::Route &route,
        const QPointF &fallbackPosition,
        QGraphicsItem *parent = nullptr);

    int type() const override;
    QRectF boundingRect() const override;
    QRectF labelRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;
    qds::core::EntityId id() const override { return m_id; }
    QPointF center() const override;
    qreal arrowSize() const override { return 16.0; }
    QVector<QPainterPath> linePaths() const override;
    QVector<QPolygonF> valvePolygons() const;
    void refresh(const qds::core::Flow &flow, DiagramItem *from, DiagramItem *to, const QPointF &fallbackPosition);

private:
    qds::core::EntityId m_id;
    QString m_name;
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

    ControlPointHandle(qds::core::EntityId id, int index, Role role, const QPointF &position);

    int type() const override;
    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;
    qds::core::EntityId routeId() const { return m_routeId; }
    int index() const { return m_index; }
    Role role() const { return m_role; }
    void assign(qds::core::EntityId id, int index, Role role, const QPointF &position);

signals:
    void moved(qds::core::EntityId id, int index, const QPointF &position);
    void finished(qds::core::EntityId id, int index, const QPointF &position);
    void removeRequested(qds::core::EntityId id, int index);

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) override;

private:
    qds::core::EntityId m_routeId;
    int m_index;
    Role m_role;
};
