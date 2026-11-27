#pragma once

#include "DiagramItems.h"
#include "Routing.h"

#include <core/ChangeSet.h>
#include <core/Document.h>

#include <QGraphicsScene>
#include <QHash>
#include <QPointer>

class DocumentController;
class QGraphicsLineItem;
class QGraphicsProxyWidget;
class QImage;

class DiagramScene : public QGraphicsScene
{
    Q_OBJECT

public:
    enum class Tool {
        Select,
        AddStock,
        AddVariable,
        AddCloud,
        AddComment,
        AddFlow,
        Connect
    };

    explicit DiagramScene(DocumentController *controller, QObject *parent = nullptr);
    ~DiagramScene() override;

    void setTool(Tool tool);
    Tool tool() const;
    bool spacePressed() const { return m_spacePressed; }
    void clearSpacePressed() { m_spacePressed = false; }
    qds::core::EntityId createElementAt(qds::core::ElementKind kind, const QPointF &position);

signals:
    void editingFinished();

public:
    void removeSelected();
    bool renameSelected(const QString &newName);
    bool setSelectedEquation(const QString &equation);
    const qds::core::ModelElement *selectedModelElement() const;
    const qds::core::Comment *selectedComment() const;
    QVector<qds::core::EntityId> selectedElementIds() const;
    QVector<qds::core::EntityId> findElements(const QString &text) const;
    void selectAllItems();
    void alignSelected(Qt::Alignment alignment);
    void distributeSelected(bool horizontal);
    void setGridVisible(bool visible);
    bool gridVisible() const { return m_gridVisible; }
    QRectF contentRect(bool selectionOnly = false) const;
    QImage renderImage(bool selectionOnly = false);
    void setSnapEnabled(bool enabled);
    void setSelectedRouteMode(qds::core::RouteMode mode);
    void resetSelectedRoute();
    void rerouteAll();
    void toggleSelectedRouteLock();
    QPointF snapPoint(const QPointF &position) const;
    DiagramItem *item(qds::core::EntityId id) const;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) override;
    void contextMenuEvent(QGraphicsSceneContextMenuEvent *event) override;
    void drawBackground(QPainter *painter, const QRectF &rect) override;

private:
    void applyChangeNotification(const qds::core::ChangeNotification &notification);
    DiagramItem *createCanvasItem(qds::core::EntityId id);
    void createCanvasItems(QVector<qds::core::EntityId> ids);
    RoutedItem *selectedRoutedItem() const;
    void updateDependentRoutes(qds::core::EntityId id);
    void updateSceneRect();
    void finishMove(qds::core::EntityId id, const QPointF &position, Qt::KeyboardModifiers modifiers);
    void syncControlPoints();
    void previewControlPoint(qds::core::EntityId id, int index, const QPointF &position);
    void finishControlPoint(qds::core::EntityId id, int index, const QPointF &position);
    void removeControlPoint(qds::core::EntityId id, int index);
    void updateAutomaticRoute(RoutedItem *item);
    void setEndpointTarget(DiagramItem *target);
    void beginInlineEdit(DiagramItem *item);
    void finishInlineEdit(bool commit);
    void cancelRubberLine();
    void cancelEndpointPreview();

    DocumentController *m_controller;
    QHash<qds::core::EntityId, DiagramItem *> m_items;
    Tool m_tool = Tool::Select;
    qds::core::EntityId m_connectionSource = 0;
    QPointF m_connectionStart;
    QGraphicsLineItem *m_rubberLine = nullptr;
    QPointer<RoutedItem> m_endpointPreviewItem;
    QPointer<DiagramItem> m_endpointTarget;
    QPointer<QGraphicsProxyWidget> m_inlineEditor;
    qds::core::EntityId m_inlineEditId = 0;
    QList<ControlPointHandle *> m_controlHandles;
    ObstacleIndex m_obstacles;
    OrthogonalRouter m_router;
    bool m_gridVisible = true;
    bool m_snapEnabled = true;
    bool m_spacePressed = false;
    bool m_areaSelecting = false;
    bool m_variableDragging = false;
    QPointF m_dragStart;
    QHash<qds::core::EntityId, QPointF> m_dragOrigins;
    double m_gridSize = 20.0;
};
