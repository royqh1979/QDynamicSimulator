#pragma once

#include "Routing.h"

#include <qds/core/ChangeSet.h>
#include <qds/core/Document.h>

#include <QGraphicsScene>
#include <QHash>

class DocumentController;
class QGraphicsLineItem;
class VariableItem;
class ConnectionItem;
class ControlPointHandle;

class DiagramScene : public QGraphicsScene
{
    Q_OBJECT

public:
    enum class Tool {
        Select,
        AddStock,
        AddFlow,
        AddAuxiliary,
        AddConstant,
        AddExogenous,
        AddCloud,
        AddAnnotation,
        Connect,
        ConnectMaterial,
        ConnectAnnotation
    };

    explicit DiagramScene(DocumentController *controller, QObject *parent = nullptr);
    ~DiagramScene() override;

    void setTool(Tool tool);
    Tool tool() const;
    qds::core::EntityId createVariableAt(qds::core::VariableType type, const QPointF &position);

signals:
    void editingFinished();

public:
    void removeSelected();
    bool renameSelected(const QString &newName);
    bool setSelectedEquation(const QString &equation);
    const qds::core::Variable *selectedVariable() const;
    std::vector<qds::core::EntityId> selectedVariableIds() const;
    std::vector<qds::core::EntityId> findVariables(const QString &text) const;
    void selectAllItems();
    void alignSelected(Qt::Alignment alignment);
    void distributeSelected(bool horizontal);
    void setGridVisible(bool visible);
    void setSnapEnabled(bool enabled);
    void setSelectedRouteMode(qds::core::RouteMode mode);
    void resetSelectedRoute();
    void rerouteAll();
    void toggleSelectedRouteLock();
    QPointF snapPoint(const QPointF &position) const;
    VariableItem *variableItem(qds::core::EntityId id) const;
    ConnectionItem *connectionItem(qds::core::ConnectionId id) const;

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) override;
    void drawBackground(QPainter *painter, const QRectF &rect) override;

private:
    void applyChangeSet(const qds::core::ChangeSet &changeSet);
    VariableItem *createVariableItem(qds::core::EntityId id);
    ConnectionItem *createConnectionItem(qds::core::ConnectionId id);
    void updateConnections(qds::core::EntityId id);
    void updateSceneRect();
    void finishMove();
    void syncControlPoints();
    void previewControlPoint(qds::core::ConnectionId id, int index, const QPointF &position);
    void finishControlPoint(qds::core::ConnectionId id, int index, const QPointF &position);
    void removeControlPoint(qds::core::ConnectionId id, int index);
    void updateAutomaticRoute(ConnectionItem *item);
    void cancelRubberLine();

    DocumentController *m_controller;
    QHash<qds::core::EntityId, VariableItem *> m_entityItems;
    QHash<qds::core::ConnectionId, ConnectionItem *> m_connectionItems;
    Tool m_tool = Tool::Select;
    qds::core::EntityId m_connectionSource = 0;
    QGraphicsLineItem *m_rubberLine = nullptr;
    QList<ControlPointHandle *> m_controlHandles;
    ObstacleIndex m_obstacles;
    OrthogonalRouter m_router;
    bool m_gridVisible = true;
    bool m_snapEnabled = true;
    double m_gridSize = 20.0;
};
