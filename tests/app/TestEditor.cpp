#include "DiagramItems.h"
#include "DiagramScene.h"
#include "DiagramView.h"
#include "DocumentController.h"
#include "EntityPalette.h"
#include "EquationEditor.h"
#include "MainWindow.h"
#include "MiniMapView.h"
#include "PropertyPanel.h"
#include "RecentFiles.h"
#include "Routing.h"
#include <qds/io/DocumentFile.h>
#include <qds/io/XmileFile.h>

#include <QDropEvent>
#include <QFile>
#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsSceneMouseEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QMouseEvent>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolBar>
#include <QUrl>
#include <QtTest>

#include <cmath>
#include <optional>

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
    void commentContentEditing();
    void propertyPanelWidget();
    void moveUndoRedo();
    void flowAndCascadeRestore();
    void routeUndoRedo();
    void replaceDocumentResetsScene();
    void missingViewImportProjectsAllVariables();
    void connectorValidation();
    void connectionDeleteUndo();
    void layoutOperations();
    void gridAndSceneBounds();
    void viewOperations();
    void routeModesAndGeometry();
    void flowRoutesAndValveGeometry();
    void flowLabelRepaint();
    void bezierControlGuideRendering();
    void controlPointDoubleClick();
    void reconnectUndo();
    void boundaryEndpoints();
    void sirCanvasConnections();
    void endpointHandleReconnect();
    void connectorMoveRepaint();
    void rubberBandReverseRepaint();
    void automaticAndRelativeRoutePoints();
    void spatialIndexAndRouter();
    void equationEditor();
    void clipboardOperations();
    void recentSearchAndMiniMap();
    void paletteDrop();
    void rubberConnectionAndSelection();
    void connectionContextMenu();
    void orthogonalArrowDirections();
    void actionIcons();
    void unifiedIdUndoRedo();
    void modifierDragOperations();
    void fileDropOpen();
};

void TestEditor::createUndoRedo()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    const EntityId id = controller.createVariable(ElementKind::Stock, QPointF(10.0, 20.0));
    QVERIFY(scene.item(id));
    QCOMPARE(controller.document().diagram().position(id), Vec2({10.0, 20.0}));
    controller.undoStack()->undo();
    QVERIFY(controller.document().find(id) == nullptr);
    QVERIFY(scene.item(id) == nullptr);
    controller.undoStack()->redo();
    QVERIFY(scene.item(id));
    QCOMPARE(controller.document().find(id)->id(), id);
}

void TestEditor::renameAndEquationUndo()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    const EntityId id = controller.createVariable(ElementKind::Stock, QPointF());
    QVERIFY(controller.renameElement(id, QStringLiteral("Population")));
    controller.setEquation(id, QStringLiteral("100"));
    controller.setUnit(id, QStringLiteral("people"));
    controller.setComment(id, QStringLiteral("Population stock"));
    const auto *model = static_cast<const ModelElement *>(controller.document().find(id));
    QCOMPARE(model->unit, std::string("people"));
    QCOMPARE(model->comment, std::string("Population stock"));
    controller.undoStack()->undo();
    controller.undoStack()->undo();
    QCOMPARE(dynamic_cast<VariableItem *>(scene.item(id))->name(), QStringLiteral("Population"));
    controller.undoStack()->undo();
    QCOMPARE(static_cast<const ModelElement *>(controller.document().find(id))->equation, std::string());
    controller.undoStack()->undo();
    QCOMPARE(static_cast<const ModelElement *>(controller.document().find(id))->displayName, std::string("Stock1"));
    controller.undoStack()->redo();
    controller.undoStack()->redo();
    QCOMPARE(static_cast<const ModelElement *>(controller.document().find(id))->equation, std::string("100"));
    QVERIFY(!controller.renameElement(id, QString()));
}

void TestEditor::commentContentEditing()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    const EntityId id = controller.createComment(QStringLiteral("Before"), QPointF());
    controller.setCommentContent(id, QStringLiteral("After"));
    QCOMPARE(static_cast<const Comment *>(controller.document().find(id))->content, std::string("After"));
    QCOMPARE(dynamic_cast<ViewObjectItem *>(scene.item(id))->text(), QStringLiteral("After"));
    controller.undoStack()->undo();
    QCOMPARE(static_cast<const Comment *>(controller.document().find(id))->content, std::string("Before"));
    QCOMPARE(dynamic_cast<ViewObjectItem *>(scene.item(id))->text(), QStringLiteral("Before"));
    controller.undoStack()->redo();
    QCOMPARE(static_cast<const Comment *>(controller.document().find(id))->content, std::string("After"));

    PropertyPanel panel(&controller, &scene);
    const EntityId panelComment = controller.createComment(QStringLiteral("\u521D\u59CB\u6CE8\u91CA"), QPointF());
    scene.item(panelComment)->setSelected(true);
    auto *edit = panel.findChild<QLineEdit *>(QStringLiteral("commentEdit"));
    auto *label = panel.findChild<QLabel *>(QStringLiteral("commentLabel"));
    QVERIFY(edit && label);
    QCOMPARE(label->text(), QStringLiteral("Content"));
    QVERIFY(edit->isEnabled());
    edit->setText(QStringLiteral("\u4FEE\u6539\u540E\u7684\u6CE8\u91CA"));
    QTest::keyClick(edit, Qt::Key_Return);
    QCOMPARE(static_cast<const Comment *>(controller.document().find(panelComment))->content,
             std::string("\u4FEE\u6539\u540E\u7684\u6CE8\u91CA"));
    QCOMPARE(dynamic_cast<ViewObjectItem *>(scene.item(panelComment))->text(), QStringLiteral("\u4FEE\u6539\u540E\u7684\u6CE8\u91CA"));
    controller.undoStack()->undo();
    QCOMPARE(edit->text(), QStringLiteral("\u521D\u59CB\u6CE8\u91CA"));
    QCOMPARE(dynamic_cast<ViewObjectItem *>(scene.item(panelComment))->text(), QStringLiteral("\u521D\u59CB\u6CE8\u91CA"));
}

void TestEditor::propertyPanelWidget()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    PropertyPanel panel(&controller, &scene);
    const EntityId stock = controller.createVariable(ElementKind::Stock, QPointF());
    scene.item(stock)->setSelected(true);
    auto *name = panel.findChild<QLineEdit *>(QStringLiteral("nameEdit"));
    auto *equation = panel.findChild<EquationEditor *>(QStringLiteral("equationEdit"));
    auto *unit = panel.findChild<QLineEdit *>(QStringLiteral("unitEdit"));
    auto *comment = panel.findChild<QLineEdit *>(QStringLiteral("commentEdit"));
    QVERIFY(name && equation && unit && comment);
    name->setText(QStringLiteral("Population"));
    equation->setText(QStringLiteral("100"));
    unit->setText(QStringLiteral("people"));
    comment->setText(QStringLiteral("Population stock"));
    QVERIFY(panel.commit());
    const auto *model = static_cast<const ModelElement *>(controller.document().find(stock));
    QCOMPARE(model->displayName, std::string("Population"));
    QCOMPARE(model->equation, std::string("100"));
    QCOMPARE(model->unit, std::string("people"));
    QCOMPARE(model->comment, std::string("Population stock"));
    scene.clearSelection();
    QVERIFY(!name->isEnabled() && !equation->isEnabled() && !unit->isEnabled() && !comment->isEnabled());
}

void TestEditor::moveUndoRedo()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    const EntityId first = controller.createVariable(ElementKind::Stock, QPointF(0.0, 0.0));
    const EntityId second = controller.createVariable(ElementKind::Auxiliary, QPointF(10.0, 10.0));
    controller.moveElements({{first, QPointF(50.0, 60.0)}, {second, QPointF(70.0, 80.0)}});
    QCOMPARE(scene.item(first)->pos(), QPointF(50.0, 60.0));
    QCOMPARE(scene.item(second)->pos(), QPointF(70.0, 80.0));
    controller.undoStack()->undo();
    QCOMPARE(scene.item(first)->pos(), QPointF(0.0, 0.0));
    QCOMPARE(scene.item(second)->pos(), QPointF(10.0, 10.0));
    controller.undoStack()->redo();
    QCOMPARE(scene.item(first)->pos(), QPointF(50.0, 60.0));
}

void TestEditor::flowAndCascadeRestore()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    const EntityId stock = controller.createVariable(ElementKind::Stock, QPointF(0.0, 0.0));
    const EntityId cloud = controller.createCloud(QPointF(200.0, 0.0));
    const EntityId flow = controller.createFlow(stock, cloud);
    QVERIFY(dynamic_cast<FlowItem *>(scene.item(flow)));
    controller.removeElements({stock});
    QVERIFY(scene.item(stock) == nullptr);
    QVERIFY(scene.item(flow) == nullptr);
    controller.undoStack()->undo();
    QVERIFY(scene.item(stock));
    QVERIFY(dynamic_cast<FlowItem *>(scene.item(flow)));
    QCOMPARE(static_cast<const Flow *>(controller.document().find(flow))->endpoints.from, stock);
    controller.undoStack()->redo();
    QVERIFY(scene.item(flow) == nullptr);
}

void TestEditor::routeUndoRedo()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    const EntityId stock = controller.createVariable(ElementKind::Stock, QPointF());
    const EntityId auxiliary = controller.createVariable(ElementKind::Auxiliary, QPointF(100.0, 0.0));
    const EntityId id = controller.createConnector(stock, auxiliary);
    Route route;
    route.manual = true;
    route.controlPoints = {{0.5, 50.0}};
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
    controller.createVariable(ElementKind::Stock, QPointF());
    Document replacement;
    const EntityId id = replacement.createVariable(ElementKind::Auxiliary, {30.0, 40.0});
    controller.replaceDocument(std::move(replacement));
    QCOMPARE(scene.items().size(), 1);
    QVERIFY(scene.item(id));
    QCOMPARE(controller.undoStack()->count(), 0);
    QVERIFY(controller.undoStack()->isClean());
}

void TestEditor::missingViewImportProjectsAllVariables()
{
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("without-view.xmile"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("<xmile version=\"1.1\"><model><variables>"
               "<stock name=\"S\"><eqn>10</eqn></stock>"
               "<flow name=\"F\"><eqn>1</eqn></flow>"
               "<aux name=\"A\"><eqn>F * 2</eqn></aux>"
               "</variables></model></xmile>");
    file.close();
    Document loaded;
    QString error;
    QVERIFY2(qds::io::DocumentFile::open(path, loaded, &error), qPrintable(error));

    DocumentController controller;
    DiagramScene scene(&controller);
    controller.replaceDocument(std::move(loaded));
    int modelCount = 0;
    for (const ModelElement &element : controller.document().model().elements()) {
        ++modelCount;
        QVERIFY2(scene.item(element.id()), qPrintable(QString::fromStdString(element.displayName)));
        if (element.kind() == ElementKind::Flow) {
            const auto *item = dynamic_cast<FlowItem *>(scene.item(element.id()));
            QVERIFY(item && item->from() && item->to());
        }
    }
    QCOMPARE(modelCount, 3);
}

void TestEditor::connectorValidation()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    const EntityId stock = controller.createVariable(ElementKind::Stock, QPointF());
    const EntityId auxiliary = controller.createVariable(ElementKind::Auxiliary, QPointF(100.0, 0.0));
    const EntityId cloud = controller.createCloud(QPointF(200.0, 0.0));
    QCOMPARE(controller.createConnector(cloud, auxiliary), EntityId(0));
    const auto beforeInvalidFlow = std::distance(controller.document().elements().begin(), controller.document().elements().end());
    QCOMPARE(controller.createFlow(auxiliary, 0, std::nullopt, QPointF(400.0, 0.0)), EntityId(0));
    QCOMPARE(std::distance(controller.document().elements().begin(), controller.document().elements().end()), beforeInvalidFlow);
    QVERIFY(controller.document().find(auxiliary));
    const EntityId information = controller.createConnector(stock, auxiliary);
    QVERIFY(information != 0);
    QCOMPARE(controller.createConnector(stock, auxiliary), EntityId(0));
    auto *arrow = dynamic_cast<ArrowItem *>(scene.item(information));
    QVERIFY(arrow);
    QCOMPARE(arrow->linePaths().size(), 1);
    QCOMPARE(arrow->lineStyle(), Qt::SolidLine);

    const EntityId comment = controller.createComment(QStringLiteral("Note"), QPointF(100.0, 100.0));
    const EntityId otherComment = controller.createComment(QStringLiteral("Note"), QPointF(200.0, 100.0));
    const EntityId annotation = controller.createConnector(comment, stock);
    QVERIFY(annotation != 0);
    QCOMPARE(dynamic_cast<ArrowItem *>(scene.item(annotation))->lineStyle(), Qt::DashLine);
    QCOMPARE(controller.createConnector(comment, otherComment), EntityId(0));
}

void TestEditor::connectionDeleteUndo()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    const EntityId stock = controller.createVariable(ElementKind::Stock, QPointF());
    const EntityId auxiliary = controller.createVariable(ElementKind::Auxiliary, QPointF(100.0, 0.0));
    const EntityId id = controller.createConnector(stock, auxiliary);
    scene.item(id)->setSelected(true);
    scene.removeSelected();
    QVERIFY(scene.item(id) == nullptr);
    controller.undoStack()->undo();
    QVERIFY(scene.item(id));
}

void TestEditor::layoutOperations()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    const EntityId first = controller.createVariable(ElementKind::Stock, QPointF(0.0, 0.0));
    const EntityId second = controller.createVariable(ElementKind::Stock, QPointF(50.0, 50.0));
    const EntityId third = controller.createVariable(ElementKind::Stock, QPointF(100.0, 100.0));
    scene.item(first)->setSelected(true);
    scene.item(second)->setSelected(true);
    scene.item(third)->setSelected(true);
    scene.alignSelected(Qt::AlignLeft);
    QCOMPARE(scene.item(first)->pos().x(), 0.0);
    QCOMPARE(scene.item(second)->pos().x(), 0.0);
    QCOMPARE(scene.item(third)->pos().x(), 0.0);
    controller.undoStack()->undo();
    QCOMPARE(scene.item(third)->pos(), QPointF(100.0, 100.0));
    scene.distributeSelected(false);
    QCOMPARE(scene.item(second)->pos().y(), 50.0);
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
    const EntityId id = controller.createVariable(ElementKind::Stock, QPointF(5000.0, 5000.0));
    QVERIFY(scene.sceneRect().contains(scene.item(id)->sceneBoundingRect()));
}

void TestEditor::viewOperations()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    controller.createVariable(ElementKind::Stock, QPointF(100.0, 100.0));
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
    const EntityId stock = controller.createVariable(ElementKind::Stock, QPointF(0.0, 0.0));
    const EntityId auxiliary = controller.createVariable(ElementKind::Auxiliary, QPointF(240.0, 0.0));
    const EntityId id = controller.createConnector(stock, auxiliary);
    auto *item = dynamic_cast<ArrowItem *>(scene.item(id));
    QVERIFY(!item->geometryPath().isEmpty());
    item->setSelected(true);
    scene.setSelectedRouteMode(RouteMode::Bezier);
    QCOMPARE(controller.document().diagram().route(id).controlPoints.size(), std::size_t(2));
    QVERIFY(!item->geometryPath().isEmpty());
    QVERIFY(!item->arrowHead().isEmpty());
    const QVector<QLineF> guides = item->controlGuides();
    QCOMPARE(guides.size(), 2);
    const auto controls = controller.document().diagram().route(id).controlPoints;
    QCOMPARE(guides[0].p2(), item->scenePoint(controls[0]));
    QCOMPARE(guides[1].p1(), item->scenePoint(controls[1]));
    controller.undoStack()->undo();
    QVERIFY(item->controlGuides().isEmpty());
    QCOMPARE(controller.document().diagram().route(id).mode, RouteMode::Straight);
    scene.setSelectedRouteMode(RouteMode::Straight);
    QVERIFY(controller.document().diagram().route(id).controlPoints.empty());
    scene.resetSelectedRoute();
    QCOMPARE(controller.document().diagram().route(id), Route());
    scene.toggleSelectedRouteLock();
    QVERIFY(controller.document().diagram().route(id).locked);
    controller.undoStack()->undo();
    QVERIFY(!controller.document().diagram().route(id).locked);
}

void TestEditor::flowRoutesAndValveGeometry()
{
    const auto checkValve = [](const QPointF &targetPosition, bool bezier) {
        DocumentController controller;
        DiagramScene scene(&controller);
        const EntityId stock = controller.createVariable(ElementKind::Stock, QPointF());
        const EntityId cloud = controller.createCloud(targetPosition);
        const EntityId id = controller.createFlow(stock, cloud);
        auto *flow = dynamic_cast<FlowItem *>(scene.item(id));
        const QVector<QPainterPath> pipelines = flow->linePaths();
        QCOMPARE(pipelines.size(), 2);
        const QPolygonF arrow = flow->arrowHead();
        QCOMPARE(flow->arrowSize(), 16.0);
        QVERIFY(QLineF(arrow[1], arrow[2]).length() > 12.0);
        const QPointF arrowBase = (arrow[1] + arrow[2]) / 2.0;
        for (const QPainterPath &pipeline : pipelines) {
            QVERIFY(QLineF(pipeline.pointAtPercent(1.0), arrow[0]).length()
                    >= QLineF(arrowBase, arrow[0]).length());
        }
        if (bezier) {
            flow->setSelected(true);
            scene.setSelectedRouteMode(RouteMode::Bezier);
        }
        const QVector<QPolygonF> polygons = flow->valvePolygons();
        QCOMPARE(polygons.size(), 2);
        const QPainterPath path = flow->geometryPath();
        const QPointF tangent = path.pointAtPercent(0.5001) - path.pointAtPercent(0.4999);
        const QPointF axis = polygons[0][0] - polygons[1][0];
        QVERIFY(std::abs(QPointF::dotProduct(tangent, axis)) < 1e-6);
        QCOMPARE(flow->route().mode, bezier ? RouteMode::Bezier : RouteMode::Straight);
    };
    checkValve(QPointF(300.0, 0.0), false);
    checkValve(QPointF(0.0, 300.0), false);
    checkValve(QPointF(240.0, 180.0), false);
    checkValve(QPointF(240.0, 180.0), true);

    DocumentController shortController;
    DiagramScene shortScene(&shortController);
    const EntityId shortStock = shortController.createVariable(ElementKind::Stock, QPointF());
    const EntityId shortCloud = shortController.createCloud(QPointF(100.0, 0.0));
    const EntityId shortFlow = shortController.createFlow(shortStock, shortCloud);
    QVERIFY(dynamic_cast<FlowItem *>(shortScene.item(shortFlow))->linePaths().isEmpty());

    DocumentController controller;
    DiagramScene scene(&controller);
    const EntityId detached = controller.createFlow(0, 0, QPointF(0.0, 0.0), QPointF(200.0, 0.0));
    QVERIFY(detached != 0);
    const auto *blankFlow = static_cast<const Flow *>(controller.document().find(detached));
    QCOMPARE(controller.document().find(blankFlow->endpoints.from)->kind(), ElementKind::Cloud);
    QCOMPARE(controller.document().find(blankFlow->endpoints.to)->kind(), ElementKind::Cloud);
    scene.item(detached)->setSelected(true);
    int visibleEndpointHandles = 0;
    for (QGraphicsItem *item : scene.items()) {
        const auto *handle = qgraphicsitem_cast<ControlPointHandle *>(item);
        if (handle && handle->isVisible() && handle->role() != ControlPointHandle::Role::ControlPoint)
            ++visibleEndpointHandles;
    }
    QCOMPARE(visibleEndpointHandles, 2);
    Route invalid;
    invalid.mode = RouteMode::Orthogonal;
    controller.setRoute(detached, invalid);
    QCOMPARE(controller.document().diagram().route(detached).mode, RouteMode::Straight);
}

void TestEditor::flowLabelRepaint()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    scene.setGridVisible(false);
    const EntityId stock = controller.createVariable(ElementKind::Stock, QPointF(0.0, 0.0));
    const EntityId cloud = controller.createCloud(QPointF(400.0, 0.0));
    const EntityId flow = controller.createFlow(stock, cloud);
    controller.renameElement(flow, QStringLiteral("Flow label repaint"));
    auto *item = dynamic_cast<FlowItem *>(scene.item(flow));
    QVERIFY(item->boundingRect().contains(item->labelRect()));

    DiagramView view(&scene);
    view.resize(640, 480);
    view.centerOn(200.0, 0.0);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(20);
    const QImage before = view.grab().toImage();
    const QRect oldLabel = view.mapFromScene(item->labelRect()).boundingRect().adjusted(-2, -2, 2, 2);
    const QColor background(250, 250, 250);
    int paintedBefore = 0;
    for (int y = oldLabel.top(); y <= oldLabel.bottom(); ++y) {
        for (int x = oldLabel.left(); x <= oldLabel.right(); ++x) {
            if (before.pixelColor(x, y) != background)
                ++paintedBefore;
        }
    }
    QVERIFY(paintedBefore > 0);

    controller.moveElements({{stock, QPointF(160.0, 100.0)}});
    QTest::qWait(20);
    const QImage after = view.grab().toImage();
    int remaining = 0;
    for (int y = oldLabel.top(); y <= oldLabel.bottom(); ++y) {
        for (int x = oldLabel.left(); x <= oldLabel.right(); ++x) {
            if (after.pixelColor(x, y) != background)
                ++remaining;
        }
    }
    QCOMPARE(remaining, 0);
}

void TestEditor::bezierControlGuideRendering()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    scene.setGridVisible(false);
    const EntityId stock = controller.createVariable(ElementKind::Stock, QPointF(0.0, 0.0));
    const EntityId auxiliary = controller.createVariable(ElementKind::Auxiliary, QPointF(300.0, 0.0));
    const EntityId id = controller.createConnector(stock, auxiliary);
    auto *item = dynamic_cast<ArrowItem *>(scene.item(id));
    item->setSelected(true);
    QVERIFY(item->zValue() > scene.item(stock)->zValue());
    Route route;
    route.mode = RouteMode::Bezier;
    route.manual = true;
    route.controlPoints = {{1.0 / 3.0, -100.0}, {2.0 / 3.0, -100.0}};
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

    route.controlPoints = {{1.0 / 3.0, 100.0}, {2.0 / 3.0, 100.0}};
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
    scene.item(stock)->setSelected(true);
    QVERIFY(item->controlGuides().isEmpty());
    QCOMPARE(visibleHandleCount(), 0);
    scene.item(stock)->setSelected(false);
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
    const EntityId stock = controller.createVariable(ElementKind::Stock, QPointF(0.0, 0.0));
    const EntityId auxiliary = controller.createVariable(ElementKind::Auxiliary, QPointF(240.0, 0.0));
    const EntityId id = controller.createConnector(stock, auxiliary);
    QGraphicsSceneMouseEvent event(QEvent::GraphicsSceneMouseDoubleClick);
    event.setButton(Qt::LeftButton);
    event.setButtons(Qt::LeftButton);
    event.setScenePos(QPointF(120.0, 0.0));
    QApplication::sendEvent(&scene, &event);
    QCOMPARE(controller.document().diagram().route(id).controlPoints.size(), std::size_t(1));
    controller.undoStack()->undo();
    QVERIFY(controller.document().diagram().route(id).controlPoints.empty());

    QGraphicsScene handleScene;
    auto *handle = new ControlPointHandle(id, 0, ControlPointHandle::Role::ControlPoint, QPointF());
    handleScene.addItem(handle);
    qRegisterMetaType<EntityId>("qds::core::EntityId");
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
    const EntityId first = controller.createVariable(ElementKind::Stock, QPointF(0.0, 0.0));
    const EntityId second = controller.createVariable(ElementKind::Stock, QPointF(100.0, 0.0));
    const EntityId auxiliary = controller.createVariable(ElementKind::Auxiliary, QPointF(200.0, 0.0));
    const EntityId id = controller.createConnector(first, auxiliary);
    QVERIFY(controller.reconnectConnector(id, second, auxiliary));
    QCOMPARE(dynamic_cast<ArrowItem *>(scene.item(id))->from(), scene.item(second));
    controller.undoStack()->undo();
    QCOMPARE(dynamic_cast<ArrowItem *>(scene.item(id))->from(), scene.item(first));
    QVERIFY(!controller.reconnectConnector(id, 9999, auxiliary));

    const EntityId cloud = controller.createCloud(QPointF(400.0, 0.0));
    const EntityId flow = controller.createFlow(first, cloud);
    QVERIFY(controller.reconnectFlow(flow, second, cloud));
    QCOMPARE(static_cast<const Flow *>(controller.document().find(flow))->endpoints.from, second);
    controller.undoStack()->undo();
    QCOMPARE(static_cast<const Flow *>(controller.document().find(flow))->endpoints.from, first);
}

void TestEditor::boundaryEndpoints()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    const EntityId stock = controller.createVariable(ElementKind::Stock, QPointF(0.0, 0.0));
    DiagramItem *item = scene.item(stock);
    QCOMPARE(item->boundaryPointToward(QPointF(100.0, 0.0)), QPointF(60.0, 0.0));
    QCOMPARE(item->boundaryPointToward(QPointF(0.0, 100.0)), QPointF(0.0, 25.0));
    QCOMPARE(item->boundaryPointToward(QPointF(0.0, 0.0)), QPointF(0.0, 0.0));
}

void TestEditor::sirCanvasConnections()
{
    Document imported;
    QString error;
    QVERIFY2(qds::io::XmileFile::importFile(QStringLiteral(QDS_SIR_XMILE_PATH), imported, &error), qPrintable(error));
    DocumentController controller;
    DiagramScene scene(&controller);
    controller.replaceDocument(std::move(imported));
    int connectorCount = 0;
    for (const ViewElement &element : controller.document().diagram().elements()) {
        if (element.kind() != ElementKind::Connector)
            continue;
        ++connectorCount;
        const auto &connector = static_cast<const Connector &>(element);
        auto *arrow = dynamic_cast<ArrowItem *>(scene.item(connector.id()));
        QVERIFY(arrow && arrow->from() && arrow->to());
        for (DiagramItem *endpoint : {arrow->from(), arrow->to()}) {
            if (auto *flow = dynamic_cast<FlowItem *>(endpoint)) {
                QCOMPARE(flow->pos(), QPointF());
                QCOMPARE(flow->center(), flow->geometryPath().pointAtPercent(0.5));
                const DiagramItem *other = endpoint == arrow->from() ? arrow->to() : arrow->from();
                const QPointF boundary = flow->boundaryPointToward(other->center());
                QVERIFY2(QLineF(boundary, flow->center()).length() <= 60.1,
                         qPrintable(QString("center=%1,%2 boundary=%3,%4")
                             .arg(flow->center().x()).arg(flow->center().y())
                             .arg(boundary.x()).arg(boundary.y())));
            }
        }
    }
    QCOMPARE(connectorCount, 6);
}

void TestEditor::endpointHandleReconnect()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    const EntityId source = controller.createVariable(ElementKind::Auxiliary, QPointF(0.0, 0.0));
    const EntityId stock = controller.createVariable(ElementKind::Stock, QPointF(0.0, 200.0));
    const EntityId targetStock = controller.createVariable(ElementKind::Stock, QPointF(0.0, -200.0));
    const EntityId cloud = controller.createCloud(QPointF(400.0, 0.0));
    const EntityId flow = controller.createFlow(stock, cloud);
    const EntityId connector = controller.createConnector(source, flow);
    auto *arrow = dynamic_cast<ArrowItem *>(scene.item(connector));
    arrow->setSelected(true);

    ControlPointHandle *targetHandle = nullptr;
    for (QGraphicsItem *item : scene.items()) {
        auto *handle = qgraphicsitem_cast<ControlPointHandle *>(item);
        if (handle && handle->routeId() == connector && handle->index() == -2)
            targetHandle = handle;
    }
    QVERIFY(targetHandle);
    QCOMPARE(targetHandle->pos(), arrow->geometryPath().pointAtPercent(1.0));

    DiagramView view(&scene);
    view.resize(640, 480);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    const QPointF destination = scene.item(targetStock)->pos();
    view.centerOn(targetHandle);
    const QPoint start = view.mapFromScene(targetHandle->pos());
    const QPoint end = view.mapFromScene(destination);
    QCOMPARE(view.itemAt(start), targetHandle);
    QMouseEvent press(QEvent::MouseButtonPress, start, view.viewport()->mapToGlobal(start), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &press);
    QCOMPARE(scene.mouseGrabberItem(), targetHandle);
    QMouseEvent move(QEvent::MouseMove, end, view.viewport()->mapToGlobal(end), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &move);
    QVERIFY(QLineF(targetHandle->pos(), destination).length() < 0.3);
    QVERIFY(arrow->previewing());
    QMouseEvent release(QEvent::MouseButtonRelease, end, view.viewport()->mapToGlobal(end), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &release);
    QVERIFY(!arrow->previewing());

    QCOMPARE(static_cast<const Connector *>(controller.document().find(connector))->to, targetStock);
    controller.undoStack()->undo();
    QCOMPARE(static_cast<const Connector *>(controller.document().find(connector))->to, flow);
}

void TestEditor::connectorMoveRepaint()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    scene.setGridVisible(false);
    const EntityId stock = controller.createVariable(ElementKind::Stock, QPointF(0.0, 0.0));
    const EntityId auxiliary = controller.createVariable(ElementKind::Auxiliary, QPointF(400.0, 0.0));
    const EntityId connector = controller.createConnector(stock, auxiliary);
    auto *arrow = dynamic_cast<ArrowItem *>(scene.item(connector));
    DiagramView view(&scene);
    view.resize(640, 480);
    view.centerOn(200.0, 0.0);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(20);
    const QImage before = view.grab().toImage();
    const QColor background(250, 250, 250);
    QVector<QPoint> samples;
    int painted = 0;
    for (int index = 1; index < 9; ++index) {
        const QPoint sample = view.mapFromScene(arrow->geometryPath().pointAtPercent(index / 10.0));
        samples.append(sample);
        painted += before.pixelColor(sample) != background ? 1 : 0;
    }
    QVERIFY(painted > 0);

    controller.moveElements({{stock, QPointF(100.0, 100.0)}});
    QTest::qWait(20);
    const QImage after = view.grab().toImage();
    for (const QPoint &sample : samples)
        QCOMPARE(after.pixelColor(sample), background);
}

void TestEditor::rubberBandReverseRepaint()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    scene.setGridVisible(false);
    DiagramView view(&scene);
    view.resize(640, 480);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(20);
    const QImage before = view.grab().toImage();
    QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(400, 300));
    QTest::mouseMove(view.viewport(), QPoint(100, 100));
    QTest::mouseMove(view.viewport(), QPoint(200, 200));
    QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(200, 200));
    const QImage after = view.grab().toImage();
    QCOMPARE(after, before);
}

void TestEditor::automaticAndRelativeRoutePoints()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    const EntityId stock = controller.createVariable(ElementKind::Stock, QPointF(0.0, 0.0));
    const EntityId auxiliary = controller.createVariable(ElementKind::Auxiliary, QPointF(400.0, 0.0));
    const EntityId connector = controller.createConnector(stock, auxiliary);
    Route route;
    route.mode = RouteMode::Orthogonal;
    controller.setRoute(connector, route);
    scene.rerouteAll();
    auto *arrow = dynamic_cast<ArrowItem *>(scene.item(connector));
    QVERIFY(!arrow->route().controlPoints.empty());
    for (const RoutePoint &point : arrow->route().controlPoints) {
        QVERIFY(point.automatic);
        QVERIFY(point.relative.x >= 0.0 && point.relative.x <= 1.0);
        const QPointF absolute = arrow->scenePoint(point);
        QCOMPARE(arrow->routePointFromScene(absolute).relative, point.relative);
    }
    arrow->setSelected(true);
    for (QGraphicsItem *item : scene.items()) {
        const auto *handle = qgraphicsitem_cast<ControlPointHandle *>(item);
        if (handle && handle->routeId() == connector && handle->role() == ControlPointHandle::Role::ControlPoint)
            QVERIFY(!handle->isVisible());
    }

    scene.setSelectedRouteMode(RouteMode::Bezier);
    const auto manualPoints = controller.document().diagram().route(connector).controlPoints;
    QCOMPARE(manualPoints.size(), std::size_t(2));
    for (const RoutePoint &point : manualPoints)
        QVERIFY(!point.automatic);
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
    for (const RoutePoint &point : route.controlPoints)
        points.append(QPointF(point.relative.x, point.relative.y));
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
    const EntityId stock = controller.createVariable(ElementKind::Stock, QPointF());
    const EntityId auxiliary = controller.createVariable(ElementKind::Auxiliary, QPointF(100.0, 0.0));
    controller.setEquation(stock, QStringLiteral("100"));
    const EntityId connector = controller.createConnector(stock, auxiliary);
    QCOMPARE(controller.copyElements({stock}).elements.size(), std::size_t(1));
    const ClipboardData clipboard = controller.copyElements({stock, auxiliary, connector});
    QCOMPARE(clipboard.elements.size(), std::size_t(3));
    const auto pasted = controller.pasteClipboard(clipboard, QPointF(20.0, 20.0));
    QCOMPARE(pasted.size(), std::size_t(3));
    QCOMPARE(controller.document().diagram().position(pasted[0]), Vec2({20.0, 20.0}));
    controller.undoStack()->undo();
    QVERIFY(controller.document().find(connector));
    for (EntityId id : pasted)
        QVERIFY(controller.document().find(id) == nullptr);
    controller.undoStack()->redo();
    for (EntityId id : pasted)
        QVERIFY(controller.document().find(id));

    const EntityId cloud = controller.createCloud(QPointF(300.0, 0.0));
    const EntityId flow = controller.createFlow(stock, cloud);
    QVERIFY(controller.copyElements({flow}).empty());
    const ClipboardData flowClipboard = controller.copyElements({stock, cloud, flow});
    QCOMPARE(flowClipboard.elements.size(), std::size_t(3));
    DocumentController other;
    QCOMPARE(other.pasteClipboard(flowClipboard, QPointF()).size(), std::size_t(3));
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
    controller.createVariable(ElementKind::Stock, QPointF());
    const EntityId population = controller.createVariable(ElementKind::Stock, QPointF(100.0, 100.0));
    controller.renameElement(population, QStringLiteral("Population"));
    QCOMPARE(scene.findElements(QStringLiteral("pop")).size(), std::size_t(1));
    QCOMPARE(scene.findElements(QStringLiteral("pop")).front(), population);
    MiniMapView miniMap(&scene);
    QCOMPARE(miniMap.scene(), &scene);
}

void TestEditor::paletteDrop()
{
    TestEntityPalette palette;
    QCOMPARE(palette.count(), 9);
    QCOMPARE(palette.item(3)->text(), QStringLiteral("Data"));
    for (int index = 0; index < palette.count(); ++index) {
        if (index != 6)
            QVERIFY(!palette.item(index)->icon().isNull());
    }
    QVERIFY(!(palette.item(7)->flags() & Qt::ItemIsDragEnabled));
    QVERIFY(!(palette.item(8)->flags() & Qt::ItemIsDragEnabled));
    QVERIFY(palette.mimeData({palette.item(7)}) == nullptr);
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
    QCOMPARE(scene.selectedElementIds().size(), std::size_t(1));
    QCOMPARE(scene.selectedModelElement()->kind(), ElementKind::Auxiliary);
    delete mime;
}

void TestEditor::rubberConnectionAndSelection()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    const EntityId source = controller.createVariable(ElementKind::Stock, QPointF(0.0, 0.0));
    const EntityId target = controller.createVariable(ElementKind::Auxiliary, QPointF(240.0, 0.0));

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
    QVERIFY(scene.item(source)->isSelected());

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
    const auto &document = controller.document();
    QVERIFY(document.find(3));
    QCOMPARE(document.find(3)->kind(), ElementKind::Connector);
    QCOMPARE(scene.tool(), DiagramScene::Tool::Select);

    const EntityId cloud = controller.createCloud(QPointF(480.0, 0.0));
    scene.setTool(DiagramScene::Tool::AddFlow);
    QApplication::sendEvent(&scene, &press);
    release.setScenePos(QPointF(480.0, 0.0));
    QApplication::sendEvent(&scene, &release);
    const EntityId flow = controller.document().findByName("Flow1")->id();
    QCOMPARE(document.find(flow)->kind(), ElementKind::Flow);
    QCOMPARE(static_cast<const Flow *>(document.find(flow))->endpoints.from, source);
    QCOMPARE(static_cast<const Flow *>(document.find(flow))->endpoints.to, cloud);

    scene.setTool(DiagramScene::Tool::AddFlow);
    press.setScenePos(QPointF(600.0, 100.0));
    QApplication::sendEvent(&scene, &press);
    move.setScenePos(QPointF(700.0, 100.0));
    QApplication::sendEvent(&scene, &move);
    release.setScenePos(QPointF(800.0, 100.0));
    QApplication::sendEvent(&scene, &release);
    const EntityId blankFlow = document.findByName("Flow2")->id();
    const FlowEndpoints blankEndpoints = static_cast<const Flow *>(document.find(blankFlow))->endpoints;
    QCOMPARE(document.find(blankEndpoints.from)->kind(), ElementKind::Cloud);
    QCOMPARE(document.find(blankEndpoints.to)->kind(), ElementKind::Cloud);
    QCOMPARE(document.diagram().position(blankEndpoints.from), Vec2({600.0, 100.0}));
    QCOMPARE(document.diagram().position(blankEndpoints.to), Vec2({800.0, 100.0}));
    controller.undoStack()->undo();
    QVERIFY(!document.find(blankFlow) && !document.find(blankEndpoints.from) && !document.find(blankEndpoints.to));
    QVERIFY(!scene.item(blankFlow) && !scene.item(blankEndpoints.from) && !scene.item(blankEndpoints.to));
    controller.undoStack()->redo();
    QVERIFY(scene.item(blankFlow) && scene.item(blankEndpoints.from) && scene.item(blankEndpoints.to));
    QCOMPARE(static_cast<const Flow *>(document.find(blankFlow))->endpoints, blankEndpoints);

    const auto beforeArrow = std::distance(document.elements().begin(), document.elements().end());
    scene.setTool(DiagramScene::Tool::Connect);
    press.setScenePos(QPointF(1000.0, 300.0));
    QApplication::sendEvent(&scene, &press);
    release.setScenePos(QPointF(1100.0, 300.0));
    QApplication::sendEvent(&scene, &release);
    QCOMPARE(std::distance(document.elements().begin(), document.elements().end()), beforeArrow);
}

void TestEditor::connectionContextMenu()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    const EntityId stock = controller.createVariable(ElementKind::Stock, QPointF(0.0, 0.0));
    const EntityId auxiliary = controller.createVariable(ElementKind::Auxiliary, QPointF(240.0, 0.0));
    const EntityId id = controller.createConnector(stock, auxiliary);

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
            if (child->title() == QStringLiteral("Route type"))
                routeMenu = child;
        }
        if (!routeMenu)
            return;
        const QList<QAction *> actions = routeMenu->actions();
        state->valid = actions.size() == 3;
        QActionGroup *group = routeMenu->actions().front()->actionGroup();
        state->valid = state->valid && group && group->isExclusive();
        for (QAction *action : actions) {
            state->valid = state->valid && action->isCheckable() && !action->icon().isNull();
            if (action->text() == QStringLiteral("Straight"))
                state->valid = state->valid && action->isChecked();
            if (action->text() == QStringLiteral("Bezier")) {
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
        const EntityId source = controller.createVariable(ElementKind::Auxiliary, sourcePosition);
        const EntityId target = controller.createVariable(ElementKind::Stock, QPointF());
        const EntityId id = controller.createConnector(source, target);
        return dynamic_cast<ArrowItem *>(scene.item(id))->arrowHead();
    };
    const QPolygonF left = arrowFor(QPointF(-200.0, 0.0));
    const QPolygonF right = arrowFor(QPointF(200.0, 0.0));
    const QPolygonF above = arrowFor(QPointF(0.0, -200.0));
    const QPolygonF below = arrowFor(QPointF(0.0, 200.0));
    QVERIFY(!left.isEmpty() && !right.isEmpty() && !above.isEmpty() && !below.isEmpty());
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
    QVERIFY(std::abs(direction(diagonal).x()) > 0.0);
    QVERIFY(std::abs(direction(diagonal).y()) > 0.0);
}

void TestEditor::actionIcons()
{
    MainWindow window;
    for (QAction *action : window.menuBar()->actions()) {
        QVERIFY(!action->text().isEmpty());
        QVERIFY(action->icon().isNull());
    }
    QVERIFY(window.findChild<PropertyPanel *>());
    const QList<QToolBar *> toolBars = window.findChildren<QToolBar *>();
    QCOMPARE(toolBars.size(), 2);
    for (QToolBar *toolBar : toolBars) {
        for (QAction *action : toolBar->actions()) {
            if (!action->isSeparator())
                QVERIFY2(!action->icon().isNull(), qPrintable(action->text()));
        }
    }
    const EntityPalette *palette = window.findChild<EntityPalette *>();
    QVERIFY(palette);
    for (int index = 0; index < palette->count(); ++index) {
        if (index != 6)
            QVERIFY(!palette->item(index)->icon().isNull());
    }
}

void TestEditor::unifiedIdUndoRedo()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    const EntityId stock = controller.createVariable(ElementKind::Stock, QPointF());
    const EntityId cloud = controller.createCloud(QPointF(200.0, 0.0));
    const EntityId auxiliary = controller.createVariable(ElementKind::Auxiliary, QPointF(100.0, 100.0));
    const EntityId flow = controller.createFlow(stock, cloud);
    const EntityId connector = controller.createConnector(auxiliary, flow);
    QCOMPARE(std::vector<EntityId>({stock, cloud, auxiliary, flow, connector}), std::vector<EntityId>({1, 2, 3, 4, 5}));
    controller.removeElements({stock});
    QVERIFY(!scene.item(flow) && !scene.item(connector));
    controller.undoStack()->undo();
    QVERIFY(scene.item(stock) && scene.item(flow) && scene.item(connector));
    QCOMPARE(static_cast<const Flow *>(controller.document().find(flow))->endpoints.from, stock);
    QCOMPARE(static_cast<const Connector *>(controller.document().find(connector))->to, flow);
    controller.undoStack()->redo();
    QVERIFY(!scene.item(flow) && !scene.item(connector));
    controller.undoStack()->undo();
    const EntityId next = controller.createVariable(ElementKind::Stock, QPointF(400.0, 0.0));
    QVERIFY(next > connector);
}

void TestEditor::modifierDragOperations()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    const EntityId stock = controller.createVariable(ElementKind::Stock, QPointF());
    auto drag = [&scene](const QPointF &start, const QPointF &end, Qt::KeyboardModifiers modifiers) {
        QGraphicsSceneMouseEvent press(QEvent::GraphicsSceneMousePress);
        press.setButton(Qt::LeftButton);
        press.setButtons(Qt::LeftButton);
        press.setModifiers(modifiers);
        press.setScenePos(start);
        QApplication::sendEvent(&scene, &press);
        QGraphicsSceneMouseEvent move(QEvent::GraphicsSceneMouseMove);
        move.setButtons(Qt::LeftButton);
        move.setModifiers(modifiers);
        move.setLastScenePos(start);
        move.setScenePos(end);
        QApplication::sendEvent(&scene, &move);
        QGraphicsSceneMouseEvent release(QEvent::GraphicsSceneMouseRelease);
        release.setButton(Qt::LeftButton);
        release.setModifiers(modifiers);
        release.setScenePos(end);
        QApplication::sendEvent(&scene, &release);
    };

    drag(QPointF(), QPointF(100.0, 80.0), Qt::ShiftModifier);
    QCOMPARE(controller.document().diagram().position(stock), Vec2({100.0, 0.0}));
    drag(QPointF(100.0, 0.0), QPointF(200.0, 80.0), Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(controller.document().diagram().position(stock), Vec2({100.0, 0.0}));
    const std::vector<EntityId> selected = scene.selectedElementIds();
    QCOMPARE(selected.size(), std::size_t(1));
    QVERIFY(selected.front() != stock);
    const EntityId copy = selected.front();
    QCOMPARE(controller.document().diagram().position(copy), Vec2({200.0, 0.0}));
    controller.undoStack()->undo();
    QVERIFY(controller.document().find(copy) == nullptr);
    QCOMPARE(controller.document().diagram().position(stock), Vec2({100.0, 0.0}));
    controller.undoStack()->redo();
    QVERIFY(controller.document().find(copy));

    drag(QPointF(200.0, 0.0), QPointF(260.0, 80.0), Qt::ControlModifier);
    const std::vector<EntityId> secondSelection = scene.selectedElementIds();
    QCOMPARE(secondSelection.size(), std::size_t(1));
    const EntityId secondCopy = secondSelection.front();
    QVERIFY(secondCopy != copy);
    QCOMPARE(controller.document().diagram().position(copy), Vec2({200.0, 0.0}));
    QCOMPARE(controller.document().diagram().position(secondCopy), Vec2({260.0, 80.0}));
    drag(QPointF(260.0, 80.0), QPointF(300.0, 200.0), Qt::ShiftModifier);
    QCOMPARE(controller.document().diagram().position(secondCopy), Vec2({260.0, 200.0}));
}

void TestEditor::fileDropOpen()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("\u62D6\u653E\u6A21\u578B.qds"));
    Document document;
    const EntityId stock = document.createVariable(ElementKind::Stock, {10.0, 20.0});
    document.rename(stock, "\u4E2D\u6587\u5E93\u5B58");
    QString error;
    QVERIFY2(qds::io::DocumentFile::save(path, document, &error), qPrintable(error));

    MainWindow window;
    DiagramView *view = window.findChild<DiagramView *>();
    DiagramScene *scene = window.findChild<DiagramScene *>();
    QVERIFY(view && scene);
    QMimeData data;
    data.setUrls({QUrl::fromLocalFile(path)});
    QDragEnterEvent enter(QPoint(20, 20), Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view->viewport(), &enter);
    QVERIFY(enter.isAccepted());
    QDropEvent drop(QPoint(20, 20), Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view->viewport(), &drop);
    QVERIFY(drop.isAccepted());
    QVERIFY(window.windowTitle().contains(QStringLiteral("\u62D6\u653E\u6A21\u578B.qds")));
    QVERIFY(scene->item(1));
    QCOMPARE(scene->selectedModelElement(), nullptr);
    QCOMPARE(scene->findElements(QStringLiteral("\u4E2D\u6587\u5E93\u5B58")).size(), std::size_t(1));

    const QString plainPath = directory.filePath(QStringLiteral("plain.xmile"));
    QFile plainFile(plainPath);
    QVERIFY(plainFile.open(QIODevice::WriteOnly));
    plainFile.write("<xmile version=\"1.1\"><model><variables>"
                    "<stock name=\"Plain\"><eqn>1</eqn></stock>"
                    "</variables></model></xmile>");
    plainFile.close();
    data.setUrls({QUrl::fromLocalFile(plainPath)});
    QDragEnterEvent plainEnter(QPoint(20, 20), Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view->viewport(), &plainEnter);
    QVERIFY(plainEnter.isAccepted());
    QDropEvent plainDrop(QPoint(20, 20), Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view->viewport(), &plainDrop);
    QVERIFY(plainDrop.isAccepted());
    QVERIFY(window.windowTitle().contains(QStringLiteral("Untitled.qds")));
    QCOMPARE(scene->findElements(QStringLiteral("Plain")).size(), std::size_t(1));

    QMimeData invalid;
    invalid.setUrls({QUrl::fromLocalFile(directory.filePath(QStringLiteral("not-model.txt")))});
    QDragEnterEvent rejected(QPoint(20, 20), Qt::CopyAction, &invalid, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view->viewport(), &rejected);
    QVERIFY(!rejected.isAccepted());
}

QTEST_MAIN(TestEditor)
#include "TestEditor.moc"
