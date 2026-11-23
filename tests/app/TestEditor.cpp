#include "DiagramItems.h"
#include "DiagramScene.h"
#include "DiagramView.h"
#include "DocumentController.h"
#include "EntityPalette.h"
#include "EquationEditor.h"
#include "MiniMapView.h"
#include "RecentFiles.h"
#include "Routing.h"

#include <QDropEvent>
#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsSceneMouseEvent>
#include <QMenu>
#include <QMimeData>
#include <QTimer>
#include <QtTest>

#include <cmath>

using namespace qds::core;

class TestEntityPalette : public EntityPalette
{
public:
    using EntityPalette::mimeData;
};

class TestEditor : public QObject
{
    Q_OBJECT

private slots:
    void createUndoRedo();
    void renameAndEquationUndo();
    void moveUndoRedo();
    void connectionAndCascadeRestore();
    void routeUndoRedo();
    void replaceDocumentResetsScene();
    void connectionTypeValidation();
    void connectionDeleteUndo();
    void layoutOperations();
    void gridAndSceneBounds();
    void viewOperations();
    void routeModesAndGeometry();
    void bezierControlGuideRendering();
    void controlPointDoubleClick();
    void reconnectUndo();
    void boundaryEndpoints();
    void spatialIndexAndRouter();
    void equationEditor();
    void clipboardOperations();
    void recentSearchAndMiniMap();
    void paletteDrop();
    void rubberConnectionAndSelection();
    void connectionContextMenu();
    void orthogonalArrowDirections();
};

void TestEditor::createUndoRedo()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    const EntityId id = controller.createVariable(VariableType::Stock, QPointF(10.0, 20.0));
    QVERIFY(scene.variableItem(id));
    QCOMPARE(controller.document().diagram().position(id), Vec2({10.0, 20.0}));
    controller.undoStack()->undo();
    QVERIFY(controller.document().variable(id) == nullptr);
    QVERIFY(scene.variableItem(id) == nullptr);
    controller.undoStack()->redo();
    QVERIFY(scene.variableItem(id));
    QCOMPARE(controller.document().variable(id)->id, id);
}

void TestEditor::renameAndEquationUndo()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    const EntityId id = controller.createVariable(VariableType::Stock, QPointF());
    QVERIFY(controller.renameVariable(id, QStringLiteral("Population")));
    controller.setEquation(id, QStringLiteral("100"));
    controller.setUnit(id, QStringLiteral("people"));
    controller.setComment(id, QStringLiteral("Population stock"));
    QCOMPARE(controller.document().variable(id)->unit, std::string("people"));
    QCOMPARE(controller.document().variable(id)->comment, std::string("Population stock"));
    controller.undoStack()->undo();
    controller.undoStack()->undo();
    QCOMPARE(scene.variableItem(id)->name(), QStringLiteral("Population"));
    controller.undoStack()->undo();
    QCOMPARE(controller.document().variable(id)->equation, std::string());
    controller.undoStack()->undo();
    QCOMPARE(controller.document().variable(id)->name, std::string("Stock"));
    controller.undoStack()->redo();
    controller.undoStack()->redo();
    QCOMPARE(controller.document().variable(id)->equation, std::string("100"));
    QVERIFY(!controller.renameVariable(id, QString()));
}

void TestEditor::moveUndoRedo()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    const EntityId first = controller.createVariable(VariableType::Stock, QPointF(0.0, 0.0));
    const EntityId second = controller.createVariable(VariableType::Auxiliary, QPointF(10.0, 10.0));
    controller.moveVariables({{first, QPointF(50.0, 60.0)}, {second, QPointF(70.0, 80.0)}});
    QCOMPARE(scene.variableItem(first)->pos(), QPointF(50.0, 60.0));
    QCOMPARE(scene.variableItem(second)->pos(), QPointF(70.0, 80.0));
    QCOMPARE(controller.undoStack()->count(), 3);
    controller.undoStack()->undo();
    QCOMPARE(scene.variableItem(first)->pos(), QPointF(0.0, 0.0));
    QCOMPARE(scene.variableItem(second)->pos(), QPointF(10.0, 10.0));
    controller.undoStack()->redo();
    QCOMPARE(scene.variableItem(first)->pos(), QPointF(50.0, 60.0));
}

void TestEditor::connectionAndCascadeRestore()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    const EntityId stock = controller.createVariable(VariableType::Stock, QPointF(0.0, 0.0));
    const EntityId flow = controller.createVariable(VariableType::Flow, QPointF(200.0, 0.0));
    const ConnectionId connection = controller.createConnection(flow, stock);
    QVERIFY(scene.connectionItem(connection));
    QCOMPARE(scene.connectionItem(connection)->from(), scene.variableItem(flow));
    controller.removeVariables({stock});
    QVERIFY(scene.variableItem(stock) == nullptr);
    QVERIFY(scene.connectionItem(connection) == nullptr);
    controller.undoStack()->undo();
    QVERIFY(scene.variableItem(stock));
    QVERIFY(scene.connectionItem(connection));
    QCOMPARE(controller.document().connection(connection)->from, flow);
    controller.undoStack()->redo();
    QVERIFY(scene.connectionItem(connection) == nullptr);
}

void TestEditor::routeUndoRedo()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    const EntityId stock = controller.createVariable(VariableType::Stock, QPointF());
    const EntityId flow = controller.createVariable(VariableType::Flow, QPointF(100.0, 0.0));
    const ConnectionId id = controller.createConnection(flow, stock);
    Route route;
    route.manual = true;
    route.controlPoints = {{50.0, 50.0}};
    controller.setRoute(id, route);
    QCOMPARE(controller.document().diagram().route(id), route);
    controller.undoStack()->undo();
    QCOMPARE(controller.document().diagram().route(id), Route());
    controller.undoStack()->redo();
    QCOMPARE(controller.document().diagram().route(id), route);
}

void TestEditor::replaceDocumentResetsScene()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    controller.createVariable(VariableType::Stock, QPointF());
    Document replacement;
    const EntityId id = replacement.createVariable(VariableType::Auxiliary, {30.0, 40.0});
    controller.replaceDocument(replacement);
    QCOMPARE(scene.items().size(), 1);
    QVERIFY(scene.variableItem(id));
    QCOMPARE(controller.undoStack()->count(), 0);
    QVERIFY(controller.undoStack()->isClean());
}

void TestEditor::connectionTypeValidation()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    const EntityId stock = controller.createVariable(VariableType::Stock, QPointF());
    const EntityId auxiliary = controller.createVariable(VariableType::Auxiliary, QPointF(100.0, 0.0));
    const int before = controller.undoStack()->count();
    QCOMPARE(controller.createConnection(auxiliary, stock, ConnectionType::MaterialFlow), ConnectionId(0));
    QCOMPARE(controller.undoStack()->count(), before);
    const ConnectionId information = controller.createConnection(stock, auxiliary, ConnectionType::Information);
    QVERIFY(information != 0);
    QVERIFY(scene.connectionItem(information));
}

void TestEditor::connectionDeleteUndo()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    const EntityId stock = controller.createVariable(VariableType::Stock, QPointF());
    const EntityId auxiliary = controller.createVariable(VariableType::Auxiliary, QPointF(100.0, 0.0));
    const ConnectionId id = controller.createConnection(stock, auxiliary);
    scene.connectionItem(id)->setSelected(true);
    scene.removeSelected();
    QVERIFY(scene.connectionItem(id) == nullptr);
    controller.undoStack()->undo();
    QVERIFY(scene.connectionItem(id));
}

void TestEditor::layoutOperations()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    const EntityId first = controller.createVariable(VariableType::Stock, QPointF(0.0, 0.0));
    const EntityId second = controller.createVariable(VariableType::Stock, QPointF(50.0, 50.0));
    const EntityId third = controller.createVariable(VariableType::Stock, QPointF(100.0, 100.0));
    scene.variableItem(first)->setSelected(true);
    scene.variableItem(second)->setSelected(true);
    scene.variableItem(third)->setSelected(true);
    scene.alignSelected(Qt::AlignLeft);
    QCOMPARE(scene.variableItem(first)->pos().x(), 0.0);
    QCOMPARE(scene.variableItem(second)->pos().x(), 0.0);
    QCOMPARE(scene.variableItem(third)->pos().x(), 0.0);
    controller.undoStack()->undo();
    QCOMPARE(scene.variableItem(third)->pos(), QPointF(100.0, 100.0));
    scene.distributeSelected(false);
    QCOMPARE(scene.variableItem(second)->pos().y(), 50.0);
    scene.selectAllItems();
    QVERIFY(scene.selectedItems().size() >= 3);
}

void TestEditor::gridAndSceneBounds()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    QCOMPARE(scene.snapPoint(QPointF(23.0, 37.0)), QPointF(20.0, 40.0));
    scene.setSnapEnabled(false);
    QCOMPARE(scene.snapPoint(QPointF(23.0, 37.0)), QPointF(23.0, 37.0));
    const EntityId id = controller.createVariable(VariableType::Stock, QPointF(5000.0, 5000.0));
    QVERIFY(scene.sceneRect().contains(scene.variableItem(id)->sceneBoundingRect()));
}

void TestEditor::viewOperations()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    controller.createVariable(VariableType::Stock, QPointF(100.0, 100.0));
    DiagramView view(&scene);
    view.zoomIn();
    QVERIFY(view.transform().m11() > 1.0);
    view.resetZoom();
    QCOMPARE(view.transform().m11(), 1.0);
    view.fitAll();
    QVERIFY(view.transform().m11() > 0.0);
}

void TestEditor::routeModesAndGeometry()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    const EntityId stock = controller.createVariable(VariableType::Stock, QPointF(0.0, 0.0));
    const EntityId flow = controller.createVariable(VariableType::Flow, QPointF(240.0, 0.0));
    const ConnectionId id = controller.createConnection(flow, stock, ConnectionType::MaterialFlow);
    ConnectionItem *item = scene.connectionItem(id);
    QVERIFY(!item->geometryPath().isEmpty());
    item->setSelected(true);
    scene.setSelectedRouteMode(RouteMode::Bezier);
    QCOMPARE(controller.document().diagram().route(id).controlPoints.size(), std::size_t(2));
    QVERIFY(!item->geometryPath().isEmpty());
    QVERIFY(!item->arrowHead().isEmpty());
    const QVector<QLineF> guides = item->controlGuides();
    QCOMPARE(guides.size(), 2);
    const auto controls = controller.document().diagram().route(id).controlPoints;
    QCOMPARE(guides[0].p2(), QPointF(controls[0].x, controls[0].y));
    QCOMPARE(guides[1].p1(), QPointF(controls[1].x, controls[1].y));
    controller.undoStack()->undo();
    QVERIFY(item->controlGuides().isEmpty());
    QCOMPARE(controller.document().diagram().route(id).mode, RouteMode::Orthogonal);
    scene.setSelectedRouteMode(RouteMode::Straight);
    QVERIFY(controller.document().diagram().route(id).controlPoints.empty());
    scene.resetSelectedRoute();
    QCOMPARE(controller.document().diagram().route(id), Route());
    scene.toggleSelectedRouteLock();
    QVERIFY(controller.document().diagram().route(id).locked);
    controller.undoStack()->undo();
    QVERIFY(!controller.document().diagram().route(id).locked);
}

void TestEditor::bezierControlGuideRendering()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    scene.setGridVisible(false);
    const EntityId stock = controller.createVariable(VariableType::Stock, QPointF(0.0, 0.0));
    const EntityId auxiliary = controller.createVariable(VariableType::Auxiliary, QPointF(300.0, 0.0));
    const ConnectionId id = controller.createConnection(stock, auxiliary);
    ConnectionItem *item = scene.connectionItem(id);
    item->setSelected(true);
    QVERIFY(item->zValue() > scene.variableItem(stock)->zValue());
    Route route;
    route.mode = RouteMode::Bezier;
    route.manual = true;
    route.controlPoints = {{100.0, -100.0}, {200.0, -100.0}};
    controller.setRoute(id, route);

    DiagramView view(&scene);
    view.resize(640, 480);
    view.centerOn(150.0, 0.0);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(20);
    const QImage before = view.grab().toImage();
    const QLineF oldGuide = item->controlGuides().front();
    QVector<QPoint> samples;
    for (int index = 3; index <= 17; ++index)
        samples.append(view.mapFromScene(oldGuide.pointAt(index / 20.0)));

    route.controlPoints = {{100.0, 100.0}, {200.0, 100.0}};
    item->setRoute(route);
    QTest::qWait(20);
    const QImage after = view.grab().toImage();
    int clearedPixels = 0;
    const QColor background(250, 250, 250);
    for (const QPoint &sample : samples) {
        if (before.pixelColor(sample) != background && after.pixelColor(sample) == background)
            ++clearedPixels;
    }
    QVERIFY(clearedPixels > 0);

    item->setSelected(false);
    QVERIFY(item->zValue() > scene.variableItem(stock)->zValue());
    QVERIFY(item->controlGuides().isEmpty());
    QTest::qWait(20);
    const QImage deselected = view.grab().toImage();
    for (const QPoint &sample : samples)
        QCOMPARE(deselected.pixelColor(sample), background);

    auto visibleHandleCount = [&scene]() {
        int count = 0;
        for (QGraphicsItem *sceneItem : scene.items()) {
            if (qgraphicsitem_cast<ControlPointHandle *>(sceneItem) && sceneItem->isVisible())
                ++count;
        }
        return count;
    };
    item->setSelected(true);
    QCOMPARE(item->controlGuides().size(), 2);
    QCOMPARE(visibleHandleCount(), 4);
    scene.variableItem(stock)->setSelected(true);
    QVERIFY(item->controlGuides().isEmpty());
    QCOMPARE(visibleHandleCount(), 0);
    scene.variableItem(stock)->setSelected(false);
    QCOMPARE(item->controlGuides().size(), 2);
    QCOMPARE(visibleHandleCount(), 4);

    QGraphicsSceneMouseEvent press(QEvent::GraphicsSceneMousePress);
    press.setButton(Qt::LeftButton);
    press.setButtons(Qt::LeftButton);
    press.setModifiers(Qt::ControlModifier);
    press.setScenePos(QPointF(500.0, 500.0));
    QApplication::sendEvent(&scene, &press);
    QVERIFY(item->controlGuides().isEmpty());
    QCOMPARE(visibleHandleCount(), 0);
    QGraphicsSceneMouseEvent release(QEvent::GraphicsSceneMouseRelease);
    release.setButton(Qt::LeftButton);
    release.setModifiers(Qt::ControlModifier);
    release.setScenePos(QPointF(550.0, 550.0));
    QApplication::sendEvent(&scene, &release);
    QCOMPARE(item->controlGuides().size(), 2);
    QCOMPARE(visibleHandleCount(), 4);
}

void TestEditor::controlPointDoubleClick()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    const EntityId stock = controller.createVariable(VariableType::Stock, QPointF(0.0, 0.0));
    const EntityId auxiliary = controller.createVariable(VariableType::Auxiliary, QPointF(240.0, 0.0));
    const ConnectionId id = controller.createConnection(stock, auxiliary);
    QGraphicsSceneMouseEvent event(QEvent::GraphicsSceneMouseDoubleClick);
    event.setButton(Qt::LeftButton);
    event.setButtons(Qt::LeftButton);
    event.setScenePos(QPointF(120.0, 0.0));
    QApplication::sendEvent(&scene, &event);
    QCOMPARE(controller.document().diagram().route(id).controlPoints.size(), std::size_t(1));
    controller.undoStack()->undo();
    QVERIFY(controller.document().diagram().route(id).controlPoints.empty());

    QGraphicsScene handleScene;
    ControlPointHandle *handle = new ControlPointHandle(id, 0, ControlPointHandle::Role::ControlPoint, QPointF());
    handleScene.addItem(handle);
    qRegisterMetaType<ConnectionId>("qds::core::ConnectionId");
    QSignalSpy removeSpy(handle, &ControlPointHandle::removeRequested);
    QGraphicsSceneMouseEvent removeEvent(QEvent::GraphicsSceneMouseDoubleClick);
    removeEvent.setScenePos(QPointF());
    removeEvent.setButton(Qt::LeftButton);
    removeEvent.setButtons(Qt::LeftButton);
    QApplication::sendEvent(&handleScene, &removeEvent);
    QCOMPARE(removeSpy.count(), 1);
}

void TestEditor::reconnectUndo()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    const EntityId first = controller.createVariable(VariableType::Stock, QPointF(0.0, 0.0));
    const EntityId second = controller.createVariable(VariableType::Stock, QPointF(100.0, 0.0));
    const EntityId auxiliary = controller.createVariable(VariableType::Auxiliary, QPointF(200.0, 0.0));
    const ConnectionId id = controller.createConnection(first, auxiliary);
    QVERIFY(controller.reconnectConnection(id, second, auxiliary));
    QCOMPARE(scene.connectionItem(id)->from(), scene.variableItem(second));
    controller.undoStack()->undo();
    QCOMPARE(scene.connectionItem(id)->from(), scene.variableItem(first));
    QVERIFY(!controller.reconnectConnection(id, 9999, auxiliary));
}

void TestEditor::boundaryEndpoints()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    const EntityId stock = controller.createVariable(VariableType::Stock, QPointF(0.0, 0.0));
    VariableItem *item = scene.variableItem(stock);
    QCOMPARE(item->boundaryPointToward(QPointF(100.0, 0.0)), QPointF(60.0, 0.0));
    QCOMPARE(item->boundaryPointToward(QPointF(0.0, 100.0)), QPointF(0.0, 25.0));
    QCOMPARE(item->boundaryPointToward(QPointF(0.0, 0.0)), QPointF(0.0, 0.0));
}

void TestEditor::spatialIndexAndRouter()
{
    ObstacleIndex index;
    index.upsert(3, QRectF(80.0, -20.0, 40.0, 40.0));
    index.upsert(4, QRectF(80.0, 200.0, 40.0, 40.0));
    QCOMPARE(index.query(QRectF(70.0, -30.0, 60.0, 60.0)).size(), 1);
    index.upsert(3, QRectF(300.0, 300.0, 40.0, 40.0));
    QVERIFY(index.query(QRectF(70.0, -30.0, 60.0, 60.0)).isEmpty());
    index.remove(4);
    QVERIFY(index.query(QRectF(70.0, 190.0, 60.0, 60.0)).isEmpty());

    index.clear();
    index.upsert(3, QRectF(80.0, -20.0, 40.0, 40.0));
    OrthogonalRouter router;
    const Route route = router.route(QPointF(0.0, 0.0), QPointF(200.0, 0.0), 1, 2, index);
    QVERIFY(!route.controlPoints.empty());
    QVector<QPointF> points {QPointF(0.0, 0.0)};
    for (const Vec2 &point : route.controlPoints)
        points.append(QPointF(point.x, point.y));
    points.append(QPointF(200.0, 0.0));
    QPainterPath path(points.front());
    for (int i = 1; i < points.size(); ++i)
        path.lineTo(points[i]);
    const QRectF obstacle(80.0, -20.0, 40.0, 40.0);
    const QLineF edges[] = {
        QLineF(obstacle.topLeft(), obstacle.topRight()), QLineF(obstacle.topRight(), obstacle.bottomRight()),
        QLineF(obstacle.bottomRight(), obstacle.bottomLeft()), QLineF(obstacle.bottomLeft(), obstacle.topLeft())};
    bool collision = false;
    for (int i = 1; i < points.size() && !collision; ++i) {
        const QLineF segment(points[i - 1], points[i]);
        for (const QLineF &edge : edges) {
            QPointF intersection;
            if (segment.intersect(edge, &intersection) == QLineF::BoundedIntersection) {
                collision = true;
                break;
            }
        }
    }
    QVERIFY(!collision);
}

void TestEditor::equationEditor()
{
    EquationEditor editor;
    editor.setCompletionWords({QStringLiteral("Population"), QStringLiteral("Flow")});
    editor.setText(QStringLiteral("EXP(1) + Population"));
    QCOMPARE(editor.text(), QStringLiteral("EXP(1) + Population"));
    QSignalSpy finishedSpy(&editor, &EquationEditor::editingFinished);
    QTest::keyClick(&editor, Qt::Key_Return);
    QCOMPARE(finishedSpy.count(), 1);
}

void TestEditor::clipboardOperations()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    const EntityId stock = controller.createVariable(VariableType::Stock, QPointF(0.0, 0.0));
    const EntityId auxiliary = controller.createVariable(VariableType::Auxiliary, QPointF(100.0, 0.0));
    controller.setEquation(stock, QStringLiteral("100"));
    controller.createConnection(stock, auxiliary);
    const ClipboardData clipboard = controller.copyVariables({stock, auxiliary});
    QCOMPARE(clipboard.variables.size(), std::size_t(2));
    QCOMPARE(clipboard.connections.size(), std::size_t(1));
    const auto pasted = controller.pasteClipboard(clipboard, QPointF(20.0, 20.0));
    QCOMPARE(pasted.size(), std::size_t(2));
    QCOMPARE(controller.document().model().variables().size(), std::size_t(4));
    QCOMPARE(controller.document().model().connections().size(), std::size_t(2));
    QCOMPARE(controller.document().diagram().position(pasted[0]), Vec2({20.0, 20.0}));
    QVERIFY(controller.document().variable(pasted[0])->name != "Stock");
    controller.undoStack()->undo();
    QCOMPARE(controller.document().model().variables().size(), std::size_t(2));
    controller.undoStack()->redo();
    QCOMPARE(controller.document().model().variables().size(), std::size_t(4));

    DocumentController other;
    const auto crossDocument = other.pasteClipboard(clipboard, QPointF());
    QCOMPARE(crossDocument.size(), std::size_t(2));
    QCOMPARE(other.document().model().connections().size(), std::size_t(1));
}

void TestEditor::recentSearchAndMiniMap()
{
    RecentFiles recent;
    for (int index = 0; index < 10; ++index)
        recent.add(QStringLiteral("R:/model%1.xmile").arg(index));
    QCOMPARE(recent.files().size(), 8);
    QCOMPARE(recent.files().front(), QStringLiteral("R:/model9.xmile"));
    recent.add(QStringLiteral("R:/model8.xmile"));
    QCOMPARE(recent.files().front(), QStringLiteral("R:/model8.xmile"));
    QCOMPARE(recent.files().size(), 8);

    DocumentController controller;
    DiagramScene scene(&controller);
    controller.createVariable(VariableType::Stock, QPointF());
    const EntityId population = controller.createVariable(VariableType::Stock, QPointF(100.0, 100.0));
    controller.renameVariable(population, QStringLiteral("Population"));
    QCOMPARE(scene.findVariables(QStringLiteral("pop")).size(), std::size_t(1));
    QCOMPARE(scene.findVariables(QStringLiteral("pop")).front(), population);
    MiniMapView miniMap(&scene);
    QCOMPARE(miniMap.scene(), &scene);
}

void TestEditor::paletteDrop()
{
    TestEntityPalette palette;
    QMimeData *mime = palette.mimeData({palette.item(1)});
    QVERIFY(mime->hasFormat(EntityPalette::mimeType()));

    DocumentController controller;
    DiagramScene scene(&controller);
    DiagramView view(&scene);
    QDragEnterEvent enter(QPoint(10, 10), Qt::CopyAction, mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &enter);
    QVERIFY(enter.isAccepted());
    QDropEvent event(QPoint(10, 10), Qt::CopyAction, mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &event);
    QCOMPARE(controller.document().model().variables().size(), std::size_t(1));
    QCOMPARE(controller.document().model().variables().front().type, VariableType::Flow);
    delete mime;
}

void TestEditor::rubberConnectionAndSelection()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    const EntityId source = controller.createVariable(VariableType::Stock, QPointF(0.0, 0.0));
    const EntityId target = controller.createVariable(VariableType::Auxiliary, QPointF(240.0, 0.0));

    QGraphicsSceneMouseEvent press(QEvent::GraphicsSceneMousePress);
    press.setButton(Qt::LeftButton);
    press.setButtons(Qt::LeftButton);
    press.setScenePos(QPointF(0.0, 0.0));
    QApplication::sendEvent(&scene, &press);
    QGraphicsSceneMouseEvent release(QEvent::GraphicsSceneMouseRelease);
    release.setButton(Qt::LeftButton);
    release.setButtons(Qt::NoButton);
    release.setScenePos(QPointF(0.0, 0.0));
    QApplication::sendEvent(&scene, &release);
    QVERIFY(scene.variableItem(source)->isSelected());
    QCOMPARE(controller.document().model().variables().size(), std::size_t(2));

    scene.setTool(DiagramScene::Tool::Connect);
    connect(&scene, &DiagramScene::editingFinished, &scene, [&scene]() {
        scene.setTool(DiagramScene::Tool::Select);
    });
    QApplication::sendEvent(&scene, &press);
    QGraphicsSceneMouseEvent move(QEvent::GraphicsSceneMouseMove);
    move.setButtons(Qt::LeftButton);
    move.setScenePos(QPointF(120.0, 0.0));
    QApplication::sendEvent(&scene, &move);
    release.setScenePos(QPointF(240.0, 0.0));
    QApplication::sendEvent(&scene, &release);
    QCOMPARE(controller.document().model().connections().size(), std::size_t(1));
    QCOMPARE(controller.document().model().connections().front().type, ConnectionType::Information);
    QCOMPARE(scene.tool(), DiagramScene::Tool::Select);

    scene.setTool(DiagramScene::Tool::ConnectMaterial);
    QApplication::sendEvent(&scene, &press);
    release.setScenePos(QPointF(500.0, 500.0));
    QApplication::sendEvent(&scene, &release);
    QCOMPARE(controller.document().model().connections().size(), std::size_t(1));
}

void TestEditor::connectionContextMenu()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    const EntityId stock = controller.createVariable(VariableType::Stock, QPointF(0.0, 0.0));
    const EntityId auxiliary = controller.createVariable(VariableType::Auxiliary, QPointF(240.0, 0.0));
    const ConnectionId id = controller.createConnection(stock, auxiliary);

    struct State {
        bool executed = false;
        bool valid = false;
    };
    auto state = std::make_shared<State>();
    QTimer::singleShot(0, &scene, [state]() {
        state->executed = true;
        QMenu *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        if (!menu)
            return;
        const QList<QMenu *> menus = menu->findChildren<QMenu *>();
        QMenu *routeMenu = nullptr;
        for (QMenu *child : menus) {
            if (child->title() == QStringLiteral("布线类型"))
                routeMenu = child;
        }
        if (!routeMenu)
            return;
        const QList<QAction *> actions = routeMenu->actions();
        state->valid = actions.size() == 3;
        QActionGroup *group = routeMenu->actions().front()->actionGroup();
        state->valid = state->valid && group && group->isExclusive();
        for (QAction *action : actions) {
            state->valid = state->valid && action->isCheckable();
            if (action->text() == QStringLiteral("直角线"))
                state->valid = state->valid && action->isChecked();
            if (action->text() == QStringLiteral("贝塞尔曲线")) {
                action->trigger();
                menu->close();
            }
        }
    });

    QGraphicsSceneContextMenuEvent event(QEvent::GraphicsSceneContextMenu);
    event.setReason(QGraphicsSceneContextMenuEvent::Mouse);
    event.setScenePos(QPointF(120.0, 0.0));
    event.setScreenPos(QPoint(100, 100));
    QApplication::sendEvent(&scene, &event);
    QVERIFY(state->executed);
    QVERIFY(state->valid);
    QCOMPARE(controller.document().diagram().route(id).mode, RouteMode::Bezier);
}

void TestEditor::orthogonalArrowDirections()
{
    auto arrowFor = [](const QPointF &sourcePosition) {
        DocumentController controller;
        DiagramScene scene(&controller);
        const EntityId source = controller.createVariable(VariableType::Auxiliary, sourcePosition);
        const EntityId target = controller.createVariable(VariableType::Stock, QPointF());
        const ConnectionId id = controller.createConnection(source, target);
        return scene.connectionItem(id)->arrowHead();
    };
    const QPolygonF left = arrowFor(QPointF(-200.0, 0.0));
    const QPolygonF right = arrowFor(QPointF(200.0, 0.0));
    const QPolygonF above = arrowFor(QPointF(0.0, -200.0));
    const QPolygonF below = arrowFor(QPointF(0.0, 200.0));
    QVERIFY(!left.isEmpty());
    QVERIFY(!right.isEmpty());
    QVERIFY(!above.isEmpty());
    QVERIFY(!below.isEmpty());
    const auto direction = [](const QPolygonF &arrow) {
        return arrow[0] - (arrow[1] + arrow[2]) / 2.0;
    };
    QVERIFY(direction(left).x() > 0.0);
    QVERIFY(direction(right).x() < 0.0);
    QVERIFY(direction(above).y() > 0.0);
    QVERIFY(direction(below).y() < 0.0);
    QCOMPARE(left[0], QPointF(-60.0, 0.0));
    QCOMPARE(right[0], QPointF(60.0, 0.0));
    QCOMPARE(above[0], QPointF(0.0, -25.0));
    QCOMPARE(below[0], QPointF(0.0, 25.0));
    const QPolygonF diagonal = arrowFor(QPointF(200.0, 200.0));
    QVERIFY(!diagonal.isEmpty());
    QVERIFY(std::abs(direction(diagonal).x()) > 0.0);
    QVERIFY(std::abs(direction(diagonal).y()) > 0.0);
}

QTEST_MAIN(TestEditor)
#include "TestEditor.moc"
