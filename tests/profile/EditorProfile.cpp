#include "DiagramItems.h"
#include "DiagramScene.h"
#include "DiagramView.h"
#include "DocumentController.h"
#include "MainWindow.h"
#include "PropertyPanel.h"
#include "SimulationController.h"
#include "SimulationSettingsDialog.h"
#include <io/DocumentFile.h>
#include <io/XmileFile.h>

#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFocusEvent>
#include <QFrame>
#include <QGraphicsLineItem>
#include <QGraphicsProxyWidget>
#include <QGraphicsSceneMouseEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QScreen>
#include <QScrollBar>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <QTextStream>
#include <QTranslator>

#include <QVector>

using namespace qds::core;

QLineEdit *inlineNameEditor(QGraphicsScene &scene)
{
    for (QGraphicsItem *item : scene.items()) {
        if (auto *proxy = qgraphicsitem_cast<QGraphicsProxyWidget *>(item)) {
            if (auto *editor = qobject_cast<QLineEdit *>(proxy->widget()); editor && editor->objectName() == QStringLiteral("inlineNameEditor"))
                return editor;
        }
    }
    return nullptr;
}

QImage viewportImage(DiagramView &view)
{
    if (QGuiApplication::platformName() == QStringLiteral("windows"))
        return QGuiApplication::primaryScreen()->grabWindow(view.viewport()->winId()).toImage();
    return view.grab().toImage();
}

std::size_t elementCount(const Document &document)
{
    std::size_t count = 0;
    for (const DocumentElement &element : document.elements()) {
        Q_UNUSED(element)
        ++count;
    }
    return count;
}

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    const int count = application.arguments().value(1, QStringLiteral("1000")).toInt();
    if (count < 2)
        return 2;

    DocumentController controller;
    DiagramScene scene(&controller);
    QVector<EntityId> ids;
    ids.reserve(static_cast<std::size_t>(count));
    QElapsedTimer timer;
    timer.start();
    const ElementKind types[] = {
        ElementKind::Stock, ElementKind::Auxiliary, ElementKind::Constant,
        ElementKind::Data, ElementKind::Cloud, ElementKind::Comment};
    for (int index = 0; index < count; ++index) {
        const ElementKind kind = types[index % 6];
        const QPointF position((index % 40) * 180.0, (index / 40) * 120.0);
        if (kind == ElementKind::Cloud)
            ids.push_back(controller.createCloud(position));
        else if (kind == ElementKind::Comment)
            ids.push_back(controller.createComment(QStringLiteral("Note"), position));
        else
            ids.push_back(controller.createVariable(kind, position));
    }
    const qint64 createNs = timer.nsecsElapsed();

    timer.restart();
    int connectionCount = 0;
    QVector<EntityId> connectionIds;
    for (int index = 1; index < count; ++index) {
        const EntityId id = controller.createConnector(ids[static_cast<std::size_t>(index - 1)], ids[static_cast<std::size_t>(index)]);
        if (id != 0) {
            ++connectionCount;
            connectionIds.push_back(id);
        }
    }
    const qint64 connectNs = timer.nsecsElapsed();

    timer.restart();
    for (EntityId id : connectionIds) {
        Route route = controller.document().diagram().route(id);
        route.manual = true;
        route.controlPoints = {{0.5, 60.0}};
        controller.setRoute(id, route);
    }
    const qint64 routeEditNs = timer.nsecsElapsed();

    timer.restart();
    int pointCount = 0;
    int arrowCount = 0;
    for (EntityId id : connectionIds) {
        auto *item = dynamic_cast<ArrowItem *>(scene.item(id));
        pointCount += item->geometryPath().elementCount();
        arrowCount += item->arrowHead().isEmpty() ? 0 : 1;
    }
    const qint64 routeGeometryNs = timer.nsecsElapsed();

    timer.restart();
    controller.moveElements({{ids.front(), QPointF(20.0, 20.0)}, {ids.back(), QPointF(30.0, 30.0)}});
    const qint64 moveNs = timer.nsecsElapsed();

    timer.restart();
    controller.undoStack()->undo();
    controller.undoStack()->redo();
    const qint64 undoRedoNs = timer.nsecsElapsed();

    timer.restart();
    scene.selectAllItems();
    scene.alignSelected(Qt::AlignLeft);
    const qint64 alignNs = timer.nsecsElapsed();
    controller.undoStack()->undo();

    const ClipboardData clipboard = controller.copyElements(ids);
    timer.restart();
    const auto pasted = controller.pasteClipboard(clipboard, QPointF(20.0, 20.0));
    const qint64 pasteNs = timer.nsecsElapsed();
    controller.undoStack()->undo();

    timer.restart();
    std::size_t matches = 0;
    for (int repeat = 0; repeat < 100; ++repeat)
        matches += scene.findElements(QStringLiteral("Stock")).size();
    const qint64 searchNs = timer.nsecsElapsed();

    timer.restart();
    for (int index = 0; index < 1000; ++index) {
        auto *preview = new QGraphicsLineItem(QLineF(0.0, 0.0, 10.0, 10.0));
        scene.addItem(preview);
        preview->setLine(QLineF(0.0, 0.0, index * 1.0, index * 0.5));
        scene.removeItem(preview);
        delete preview;
    }
    const qint64 previewNs = timer.nsecsElapsed();

    DocumentController areaController;
    DiagramScene areaScene(&areaController);
    areaController.createVariable(ElementKind::Stock, QPointF());
    areaController.createVariable(ElementKind::Auxiliary, QPointF(200.0, 200.0));
    DiagramView areaView(&areaScene);
    areaView.resize(640, 480);
    auto *areaFrame = areaView.findChild<QFrame *>(QStringLiteral("selectionFrame"));
    if (!areaFrame)
        return 32;
    const QPoint areaStart(400, 300);
    QMouseEvent areaPress(QEvent::MouseButtonPress, areaStart, areaView.viewport()->mapToGlobal(areaStart), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(areaView.viewport(), &areaPress);
    timer.restart();
    int areaResizeCount = 0;
    for (int index = 0; index < 1000; ++index) {
        const QPoint point = index % 2 == 0 ? QPoint(100, 100) : QPoint(200, 200);
        QMouseEvent move(QEvent::MouseMove, point, areaView.viewport()->mapToGlobal(point), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(areaView.viewport(), &move);
        if (areaFrame->geometry() != QRect(areaStart, point).normalized())
            return 33;
        ++areaResizeCount;
    }
    const qint64 areaResizeNs = timer.nsecsElapsed();
    QMouseEvent areaRelease(QEvent::MouseButtonRelease, QPoint(200, 200), areaView.viewport()->mapToGlobal(QPoint(200, 200)), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(areaView.viewport(), &areaRelease);

    QKeyEvent spacePress(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
    QApplication::sendEvent(&areaView, &spacePress);
    const QPoint spacePanStart(300, 200);
    QMouseEvent spacePanPress(QEvent::MouseButtonPress, spacePanStart, areaView.viewport()->mapToGlobal(spacePanStart), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(areaView.viewport(), &spacePanPress);
    timer.restart();
    int spacePanCount = 0;
    for (int index = 0; index < 1000; ++index) {
        const QPoint point = index % 2 == 0 ? QPoint(100, 100) : QPoint(200, 200);
        const int oldX = areaView.horizontalScrollBar()->value();
        const int oldY = areaView.verticalScrollBar()->value();
        QMouseEvent move(QEvent::MouseMove, point, areaView.viewport()->mapToGlobal(point), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(areaView.viewport(), &move);
        if (areaView.horizontalScrollBar()->value() == oldX || areaView.verticalScrollBar()->value() == oldY)
            return 54;
        ++spacePanCount;
    }
    const qint64 spacePanNs = timer.nsecsElapsed();
    QMouseEvent spacePanRelease(QEvent::MouseButtonRelease, QPoint(200, 200), areaView.viewport()->mapToGlobal(QPoint(200, 200)), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(areaView.viewport(), &spacePanRelease);
    QKeyEvent spaceRelease(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier);
    QApplication::sendEvent(&areaView, &spaceRelease);

    timer.restart();
    int spaceFocusResetCount = 0;
    for (int index = 0; index < 1000; ++index) {
        QKeyEvent press(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
        QApplication::sendEvent(&areaView, &press);
        QFocusEvent focusOut(QEvent::FocusOut, Qt::ActiveWindowFocusReason);
        QApplication::sendEvent(&areaView, &focusOut);
        if (areaScene.spacePressed())
            return 55;
        QKeyEvent secondPress(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
        QApplication::sendEvent(&areaView, &secondPress);
        QFocusEvent focusIn(QEvent::FocusIn, Qt::ActiveWindowFocusReason);
        QApplication::sendEvent(&areaView, &focusIn);
        if (areaScene.spacePressed())
            return 56;
        QKeyEvent release(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier);
        QApplication::sendEvent(&areaView, &release);
        ++spaceFocusResetCount;
    }
    const qint64 spaceFocusResetNs = timer.nsecsElapsed();

    MainWindow cursorWindow;
    auto *cursorView = cursorWindow.findChild<DiagramView *>();
    auto *cursorLabel = cursorWindow.findChild<QLabel *>(QStringLiteral("cursorPosition"));
    if (!cursorView || !cursorLabel)
        return 35;
    timer.restart();
    int cursorStatusCount = 0;
    for (int index = 0; index < 1000; ++index) {
        const QPoint point(index % 640, (index * 3) % 480);
        QMouseEvent move(QEvent::MouseMove, point, cursorView->viewport()->mapToGlobal(point), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(cursorView->viewport(), &move);
        const QPointF scene = cursorView->mapToScene(point);
        const QString prefix = QStringLiteral("Cursor: %1, %2")
            .arg(QString::number(scene.x(), 'f', 1), QString::number(scene.y(), 'f', 1));
        if (!cursorLabel->text().startsWith(prefix))
            return 36;
        ++cursorStatusCount;
    }
    const qint64 cursorStatusNs = timer.nsecsElapsed();

    Document moveUndoDocument;
    QString moveUndoError;
    qds::io::Workspace moveUndoWorkspace;
    if (!qds::io::DocumentFile::open(QStringLiteral(QDS_MOVE_UNDO_QDS_PATH), moveUndoDocument, moveUndoWorkspace, &moveUndoError))
        return 37;
    MainWindow moveUndoWindow;
    const QList<QDockWidget *> moveUndoDocks = moveUndoWindow.findChildren<QDockWidget *>();
    for (QDockWidget *dock : moveUndoDocks)
        dock->hide();
    DocumentController &moveUndoController = *moveUndoWindow.findChild<DocumentController *>();
    DiagramScene &moveUndoScene = *moveUndoWindow.findChild<DiagramScene *>();
    moveUndoController.replaceDocument(std::move(moveUndoDocument));
    const ModelElement *moveUndoContact = moveUndoController.document().findByName("contact_infectivity");
    DiagramItem *moveUndoItem = moveUndoScene.item(moveUndoContact->id());
    moveUndoItem->setSelected(true);
    DiagramView &moveUndoView = *moveUndoWindow.findChild<DiagramView *>();
    moveUndoWindow.resize(1000, 800);
    moveUndoView.scale(0.66, 0.66);
    moveUndoView.centerOn(moveUndoScene.itemsBoundingRect().center());
    moveUndoWindow.show();
    QThread::msleep(20);
    QApplication::processEvents();
    const QImage moveUndoInitial = viewportImage(moveUndoView);
    const int moveUndoIterations = qMax(1, count / 50);
    qint64 moveUndoNs = 0;
    int moveUndoCount = 0;
    for (int index = 0; index < moveUndoIterations; ++index) {
        const QPointF position = moveUndoItem->pos();
        timer.restart();
        moveUndoController.moveElements({{moveUndoContact->id(), position + QPointF(0.0, 400.0)}});
        moveUndoController.undoStack()->undo();
        QApplication::processEvents();
        const QImage restored = viewportImage(moveUndoView);
        moveUndoNs += timer.nsecsElapsed();
        if (restored != moveUndoInitial)
            return 38;
        ++moveUndoCount;
    }

    timer.restart();
    for (int index = 0; index < 1000; ++index) {
        QMenu menu;
        QMenu *routeMenu = menu.addMenu(QStringLiteral("Route"));
        QActionGroup group(&menu);
        for (int mode = 0; mode < 3; ++mode) {
            QAction *action = routeMenu->addAction(QString::number(mode));
            action->setCheckable(true);
            group.addAction(action);
        }
    }
    const qint64 contextMenuNs = timer.nsecsElapsed();

    DocumentController renameController;
    DiagramScene renameScene(&renameController);
    const EntityId renameStock = renameController.createVariable(ElementKind::Stock, QPointF());
    DiagramView renameView(&renameScene);
    renameView.resize(640, 480);
    renameView.centerOn(QPointF());
    qint64 inlineRenameNs = 0;
    int inlineRenameCount = 0;
    for (int index = 0; index < count; ++index) {
        const QPoint point = renameView.mapFromScene(renameScene.item(renameStock)->center());
        timer.restart();
        QMouseEvent press(QEvent::MouseButtonPress, point, renameView.viewport()->mapToGlobal(point), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(renameView.viewport(), &press);
        QMouseEvent firstRelease(QEvent::MouseButtonRelease, point, renameView.viewport()->mapToGlobal(point), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(renameView.viewport(), &firstRelease);
        QMouseEvent doubleClick(QEvent::MouseButtonDblClick, point, renameView.viewport()->mapToGlobal(point), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(renameView.viewport(), &doubleClick);
        QMouseEvent secondRelease(QEvent::MouseButtonRelease, point, renameView.viewport()->mapToGlobal(point), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(renameView.viewport(), &secondRelease);
        QLineEdit *editor = inlineNameEditor(renameScene);
        if (!editor)
            return 22;
        editor->setText(QStringLiteral("Population"));
        QKeyEvent enterPress(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QApplication::sendEvent(editor, &enterPress);
        inlineRenameNs += timer.nsecsElapsed();
        const auto *model = dynamic_cast<const ModelElement *>(renameController.document().find(renameStock));
        if (!model || model->displayName() != "Population" || inlineNameEditor(renameScene))
            return 23;
        ++inlineRenameCount;
        renameController.undoStack()->undo();
    }

    DocumentController guideController;
    DiagramScene guideScene(&guideController);
    const EntityId guideFrom = guideController.createVariable(ElementKind::Stock, QPointF(0.0, 0.0));
    const EntityId guideTo = guideController.createVariable(ElementKind::Auxiliary, QPointF(200.0, 0.0));
    const EntityId guideConnection = guideController.createConnector(guideFrom, guideTo);
    auto *guideItem = dynamic_cast<ArrowItem *>(guideScene.item(guideConnection));
    const EntityId guideComment = guideController.createComment(QStringLiteral("Note"), QPointF(100.0, 100.0));
    const EntityId guideAnnotation = guideController.createConnector(guideComment, guideFrom);
    auto *annotationItem = dynamic_cast<ArrowItem *>(guideScene.item(guideAnnotation));
    timer.restart();
    int lineStyleChecksum = 0;
    for (int index = 0; index < 1000; ++index)
        lineStyleChecksum += guideItem->lineStyle() + annotationItem->lineStyle();
    const qint64 connectorStyleNs = timer.nsecsElapsed();

    Route automaticRoute;
    automaticRoute.mode = RouteMode::Orthogonal;
    guideController.setRoute(guideConnection, automaticRoute);
    timer.restart();
    int automaticPointCount = 0;
    bool automaticFlagsValid = true;
    for (int index = 0; index < 1000; ++index) {
        guideScene.rerouteAll();
        const Route route = guideItem->route();
        automaticPointCount += route.controlPoints.size();
        for (const RoutePoint &point : route.controlPoints)
            automaticFlagsValid = automaticFlagsValid && point.automatic;
    }
    const qint64 automaticRouteNs = timer.nsecsElapsed();
    guideItem->setSelected(true);
    guideScene.setSelectedRouteMode(RouteMode::Bezier);
    Route guideRoute = guideController.document().diagram().route(guideConnection);
    timer.restart();
    int guideCount = 0;
    for (int index = 0; index < 1000; ++index) {
        const double y = index % 2 == 0 ? -80.0 : 80.0;
        guideRoute.controlPoints[0].relative.y = y;
        guideRoute.controlPoints[1].relative.y = y;
        guideItem->setRoute(guideRoute);
        guideCount += guideItem->controlGuides().size();
    }
    const qint64 bezierGuideNs = timer.nsecsElapsed();

    guideScene.resetSelectedRoute();
    ControlPointHandle *automaticHandle = nullptr;
    for (QGraphicsItem *item : guideScene.items()) {
        auto *handle = qgraphicsitem_cast<ControlPointHandle *>(item);
        if (handle && handle->isVisible() && handle->routeId() == guideConnection && handle->index() == 0)
            automaticHandle = handle;
    }
    if (!automaticHandle)
        return 15;
    timer.restart();
    int automaticGuideCount = 0;
    bool automaticGroupManual = true;
    for (int index = 0; index < 1000; ++index) {
        guideScene.resetSelectedRoute();
        automaticGuideCount += guideItem->controlGuides().size();
        automaticHandle->setPos(automaticHandle->pos() + QPointF(1.0, 0.0));
        for (const RoutePoint &point : guideItem->route().controlPoints)
            automaticGroupManual = automaticGroupManual && !point.automatic;
    }
    const qint64 automaticBezierEditNs = timer.nsecsElapsed();

    timer.restart();
    int connectorDragGuideCount = 0;
    bool connectorDragStateValid = true;
    for (int index = 0; index < 1000; ++index) {
        const QPointF position = guideItem->geometryPath().pointAtPercent(0.5);
        QGraphicsSceneMouseEvent press(QEvent::GraphicsSceneMousePress);
        press.setButton(Qt::LeftButton);
        press.setButtons(Qt::LeftButton);
        press.setScenePos(position);
        QApplication::sendEvent(&guideScene, &press);
        QGraphicsSceneMouseEvent move(QEvent::GraphicsSceneMouseMove);
        move.setButtons(Qt::LeftButton);
        move.setScenePos(position + QPointF(100.0, 80.0));
        QApplication::sendEvent(&guideScene, &move);
        connectorDragGuideCount += guideItem->controlGuides().size();
        QGraphicsSceneMouseEvent release(QEvent::GraphicsSceneMouseRelease);
        release.setButton(Qt::LeftButton);
        release.setButtons(Qt::NoButton);
        release.setScenePos(position + QPointF(100.0, 80.0));
        QApplication::sendEvent(&guideScene, &release);
        connectorDragGuideCount += guideItem->controlGuides().size();
        connectorDragStateValid = connectorDragStateValid && guideItem->isSelected();
    }
    const qint64 connectorDragStateNs = timer.nsecsElapsed();

    ControlPointHandle *targetHandle = nullptr;
    for (QGraphicsItem *item : guideScene.items()) {
        auto *handle = qgraphicsitem_cast<ControlPointHandle *>(item);
        if (handle && handle->isVisible() && handle->routeId() == guideConnection && handle->index() == -2)
            targetHandle = handle;
    }
    if (!targetHandle)
        return 16;
    DiagramView guideView(&guideScene);
    guideView.resize(640, 480);
    guideView.centerOn(100.0, 0.0);
    timer.restart();
    int endpointPreviewGuideCount = 0;
    bool endpointPreviewHandlesValid = true;
    for (int index = 0; index < 1000; ++index) {
        const QPoint start = guideView.mapFromScene(targetHandle->pos());
        const QPoint end = guideView.mapFromScene(targetHandle->pos() + QPointF(200.0, 200.0));
        QMouseEvent press(QEvent::MouseButtonPress, start, guideView.viewport()->mapToGlobal(start), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(guideView.viewport(), &press);
        QMouseEvent move(QEvent::MouseMove, end, guideView.viewport()->mapToGlobal(end), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(guideView.viewport(), &move);
        endpointPreviewGuideCount += guideItem->controlGuides().size();
        for (QGraphicsItem *item : guideScene.items()) {
            const auto *handle = qgraphicsitem_cast<ControlPointHandle *>(item);
            if (handle && handle->routeId() == guideConnection
                && handle->role() == ControlPointHandle::Role::ControlPoint)
                endpointPreviewHandlesValid = endpointPreviewHandlesValid && !handle->isVisible();
        }
        QMouseEvent release(QEvent::MouseButtonRelease, end, guideView.viewport()->mapToGlobal(end), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(guideView.viewport(), &release);
        endpointPreviewGuideCount += guideItem->controlGuides().size();
        endpointPreviewHandlesValid = endpointPreviewHandlesValid && !guideItem->previewing();
    }
    const qint64 endpointPreviewNs = timer.nsecsElapsed();

    DocumentController previewFlowController;
    DiagramScene previewFlowScene(&previewFlowController);
    const EntityId previewFlowSource = previewFlowController.createVariable(ElementKind::Stock, QPointF());
    const EntityId previewFlowOldTarget = previewFlowController.createVariable(ElementKind::Stock, QPointF(400.0, 0.0));
    const EntityId previewFlowNewTarget = previewFlowController.createVariable(ElementKind::Stock, QPointF(400.0, 400.0));
    const EntityId previewFlow = previewFlowController.createFlow(previewFlowSource, previewFlowOldTarget);
    const EntityId previewFlowAuxiliary = previewFlowController.createVariable(ElementKind::Auxiliary, QPointF(-400.0, 400.0));
    const EntityId previewFlowConnector = previewFlowController.createConnector(previewFlowAuxiliary, previewFlow);
    auto *previewFlowItem = dynamic_cast<FlowItem *>(previewFlowScene.item(previewFlow));
    auto *previewFlowArrow = dynamic_cast<ArrowItem *>(previewFlowScene.item(previewFlowConnector));
    previewFlowItem->setSelected(true);
    ControlPointHandle *previewFlowHandle = nullptr;
    for (QGraphicsItem *item : previewFlowScene.items()) {
        auto *handle = qgraphicsitem_cast<ControlPointHandle *>(item);
        if (handle && handle->routeId() == previewFlow && handle->index() == -2)
            previewFlowHandle = handle;
    }
    if (!previewFlowHandle)
        return 20;
    DiagramView previewFlowView(&previewFlowScene);
    previewFlowView.resize(900, 800);
    previewFlowView.centerOn(QPointF(0.0, 200.0));
    qint64 flowEndpointPreviewNs = 0;
    int flowEndpointPreviewCount = 0;
    for (int index = 0; index < count; ++index) {
        const EntityId target = index % 2 == 0 ? previewFlowNewTarget : previewFlowOldTarget;
        const QPoint start = previewFlowView.mapFromScene(previewFlowHandle->pos());
        const QPoint end = previewFlowView.mapFromScene(previewFlowScene.item(target)->center());
        timer.restart();
        QMouseEvent press(QEvent::MouseButtonPress, start, previewFlowView.viewport()->mapToGlobal(start), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(previewFlowView.viewport(), &press);
        QMouseEvent move(QEvent::MouseMove, end, previewFlowView.viewport()->mapToGlobal(end), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(previewFlowView.viewport(), &move);
        QMouseEvent release(QEvent::MouseButtonRelease, end, previewFlowView.viewport()->mapToGlobal(end), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(previewFlowView.viewport(), &release);
        flowEndpointPreviewNs += timer.nsecsElapsed();
        const auto *flow = dynamic_cast<const Flow *>(previewFlowController.document().find(previewFlow));
        const QPainterPath arrowPath = previewFlowArrow->geometryPath();
        const QPointF expectedEnd = previewFlowItem->boundaryPointToward(arrowPath.elementAt(arrowPath.elementCount() - 2));
        if (!flow || flow->endpoints().to != target || previewFlowItem->previewing()
            || QLineF(arrowPath.pointAtPercent(1.0), expectedEnd).length() > 0.75)
            return 21;
        ++flowEndpointPreviewCount;
    }

    DocumentController flowTargetController;
    DiagramScene flowTargetScene(&flowTargetController);
    const EntityId flowTargetSource = flowTargetController.createVariable(ElementKind::Auxiliary, QPointF(0.0, -200.0));
    const EntityId flowTargetStock = flowTargetController.createVariable(ElementKind::Stock, QPointF());
    const EntityId flowTargetCloud = flowTargetController.createCloud(QPointF(400.0, 0.0));
    const EntityId flowTargetFlow = flowTargetController.createFlow(flowTargetStock, flowTargetCloud);
    DiagramView flowTargetView(&flowTargetScene);
    flowTargetView.resize(800, 600);
    flowTargetView.centerOn(QPointF(100.0, 0.0));
    flowTargetScene.setTool(DiagramScene::Tool::Connect);
    qint64 connectorToFlowNs = 0;
    int connectorToFlowCount = 0;
    for (int index = 0; index < count; ++index) {
        const QPoint start = flowTargetView.mapFromScene(flowTargetScene.item(flowTargetSource)->center());
        const QPoint end = flowTargetView.mapFromScene(flowTargetScene.item(flowTargetFlow)->center());
        timer.restart();
        QMouseEvent press(QEvent::MouseButtonPress, start, flowTargetView.viewport()->mapToGlobal(start), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(flowTargetView.viewport(), &press);
        QMouseEvent move(QEvent::MouseMove, end, flowTargetView.viewport()->mapToGlobal(end), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(flowTargetView.viewport(), &move);
        QMouseEvent release(QEvent::MouseButtonRelease, end, flowTargetView.viewport()->mapToGlobal(end), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(flowTargetView.viewport(), &release);
        connectorToFlowNs += timer.nsecsElapsed();
        const QVector<EntityId> selection = flowTargetScene.selectedElementIds();
        if (selection.size() != 1)
            return 17;
        const auto *connector = dynamic_cast<const Connector *>(flowTargetController.document().find(selection.front()));
        const auto *arrow = dynamic_cast<const ArrowItem *>(flowTargetScene.item(selection.front()));
        if (!connector || !arrow || connector->from() != flowTargetSource || connector->to() != flowTargetFlow
            || arrow->to() != flowTargetScene.item(flowTargetFlow))
            return 18;
        ++connectorToFlowCount;
        flowTargetController.undoStack()->undo();
    }

    const QPoint highlightStart = flowTargetView.mapFromScene(flowTargetScene.item(flowTargetSource)->center());
    QMouseEvent highlightPress(QEvent::MouseButtonPress, highlightStart, flowTargetView.viewport()->mapToGlobal(highlightStart), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(flowTargetView.viewport(), &highlightPress);
    qint64 endpointTargetHighlightNs = 0;
    int endpointTargetHighlightCount = 0;
    for (int index = 0; index < count; ++index) {
        const int state = index % 3;
        const QPointF targetPosition = state == 0 ? flowTargetScene.item(flowTargetFlow)->center()
            : state == 1 ? flowTargetScene.item(flowTargetStock)->center() : QPointF(100.0, 250.0);
        const QPoint end = flowTargetView.mapFromScene(targetPosition);
        timer.restart();
        QMouseEvent move(QEvent::MouseMove, end, flowTargetView.viewport()->mapToGlobal(end), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(flowTargetView.viewport(), &move);
        endpointTargetHighlightNs += timer.nsecsElapsed();
        const bool flowHighlighted = flowTargetScene.item(flowTargetFlow)->isEndpointTargetHighlighted();
        const bool stockHighlighted = flowTargetScene.item(flowTargetStock)->isEndpointTargetHighlighted();
        if (flowHighlighted != (state == 0) || stockHighlighted != (state == 1))
            return 19;
        ++endpointTargetHighlightCount;
    }
    flowTargetScene.setTool(DiagramScene::Tool::Select);

    timer.restart();
    int singleSelectionGuideCount = 0;
    for (int index = 0; index < 1000; ++index) {
        guideScene.item(guideFrom)->setSelected(index % 2 == 0);
        singleSelectionGuideCount += guideItem->controlGuides().size();
    }
    guideScene.item(guideFrom)->setSelected(false);
    const qint64 guideSelectionNs = timer.nsecsElapsed();

    const EntityId valveCloud = guideController.createCloud(QPointF(400.0, 200.0));
    const EntityId valveFlow = guideController.createFlow(guideFrom, valveCloud);
    auto *flowItem = dynamic_cast<FlowItem *>(guideScene.item(valveFlow));
    flowItem->setSelected(true);
    guideScene.setSelectedRouteMode(RouteMode::Bezier);
    timer.restart();
    int valveCount = 0;
    for (int index = 0; index < 1000; ++index)
        valveCount += flowItem->valvePolygons().size();
    const qint64 valveNs = timer.nsecsElapsed();

    timer.restart();
    int pipelineCount = 0;
    for (int index = 0; index < 1000; ++index)
        pipelineCount += flowItem->linePaths().size();
    const qint64 pipelineNs = timer.nsecsElapsed();

    timer.restart();
    int flowArrowPointCount = 0;
    for (int index = 0; index < 1000; ++index)
        flowArrowPointCount += flowItem->arrowHead().size();
    const qint64 flowArrowNs = timer.nsecsElapsed();

    timer.restart();
    int flowLabelCount = 0;
    for (int index = 0; index < 1000; ++index)
        flowLabelCount += flowItem->boundingRect().contains(flowItem->labelRect()) ? 1 : 0;
    const qint64 flowLabelNs = timer.nsecsElapsed();

    const EntityId flowArrow = guideController.createConnector(guideTo, valveFlow);
    auto *flowArrowItem = dynamic_cast<ArrowItem *>(guideScene.item(flowArrow));
    timer.restart();
    int centerBoundaryCount = 0;
    for (int index = 0; index < 1000; ++index) {
        const QPointF boundary = flowItem->boundaryPointToward(flowArrowItem->from()->center());
        centerBoundaryCount += QLineF(boundary, flowItem->center()).length() > 0.0 ? 1 : 0;
    }
    const qint64 centerBoundaryNs = timer.nsecsElapsed();

    DocumentController dragController;
    DiagramScene dragScene(&dragController);
    const EntityId dragStock = dragController.createVariable(ElementKind::Stock, QPointF());
    timer.restart();
    for (int index = 0; index < 1000; ++index) {
        const Vec2 current = dragController.document().diagram().position(dragStock);
        const QPointF start(current.x, current.y);
        QGraphicsSceneMouseEvent press(QEvent::GraphicsSceneMousePress);
        press.setButton(Qt::LeftButton);
        press.setButtons(Qt::LeftButton);
        press.setModifiers(Qt::ShiftModifier);
        press.setScenePos(start);
        QApplication::sendEvent(&dragScene, &press);
        QGraphicsSceneMouseEvent move(QEvent::GraphicsSceneMouseMove);
        move.setButtons(Qt::LeftButton);
        move.setModifiers(Qt::ShiftModifier);
        move.setLastScenePos(start);
        move.setScenePos(start + QPointF(100.0, 80.0));
        QApplication::sendEvent(&dragScene, &move);
        QGraphicsSceneMouseEvent release(QEvent::GraphicsSceneMouseRelease);
        release.setButton(Qt::LeftButton);
        release.setModifiers(Qt::ShiftModifier);
        release.setScenePos(start + QPointF(100.0, 80.0));
        QApplication::sendEvent(&dragScene, &release);
    }
    const qint64 modifierDragNs = timer.nsecsElapsed();
    if (dragController.document().diagram().position(dragStock).y != 0.0)
        return 6;

    DocumentController blankController;
    DiagramScene blankScene(&blankController);
    timer.restart();
    for (int index = 0; index < 1000; ++index) {
        const QPointF from(index * 4.0, index * 2.0);
        const EntityId id = blankController.createFlow(0, 0, from, from + QPointF(160.0, 0.0));
        if (id == 0)
            return 7;
        const FlowEndpoints endpoints = static_cast<const Flow *>(blankController.document().find(id))->endpoints();
        blankController.undoStack()->undo();
        if (blankController.document().find(id) || blankController.document().find(endpoints.from)
            || blankController.document().find(endpoints.to))
            return 8;
        blankController.undoStack()->redo();
        if (!blankScene.item(id))
            return 9;
        blankController.undoStack()->undo();
    }
    const qint64 blankFlowNs = timer.nsecsElapsed();

    const EntityId commentId = ids[5];
    timer.restart();
    for (int index = 0; index < 1000; ++index) {
        controller.setCommentContent(commentId, index % 2 == 0 ? QStringLiteral("After") : QStringLiteral("Before"));
    }
    const qint64 commentEditNs = timer.nsecsElapsed();
    if (dynamic_cast<ViewObjectItem *>(scene.item(commentId))->text() != QStringLiteral("Before"))
        return 10;
    scene.clearSelection();
    scene.item(commentId)->setSelected(true);
    PropertyPanel propertyPanel(&controller, &scene);
    timer.restart();
    for (int index = 0; index < 1000; ++index)
        propertyPanel.refresh();
    const qint64 propertyPanelNs = timer.nsecsElapsed();

    DocumentController fontController;
    DiagramScene fontScene(&fontController);
    const EntityId fontStock = fontController.createVariable(ElementKind::Stock, QPointF());
    qint64 fontStyleNs = 0;
    int fontStyleCount = 0;
    for (int index = 0; index < count; ++index) {
        const QFont font = index % 2 == 0 ? QFont(QStringLiteral("Arial"), 18) : QFont(QStringLiteral("Times New Roman"), 16);
        timer.restart();
        fontController.setFontStyle(fontStock, font);
        fontStyleNs += timer.nsecsElapsed();
        const FontStyle expected {font.family(), font.pointSize()};
        if (fontController.document().diagram().fontStyle(fontStock) != expected
            || fontScene.item(fontStock)->labelFont().family() != font.family()
            || fontScene.item(fontStock)->labelFont().pointSize() != font.pointSize())
            return 28;
        ++fontStyleCount;
        fontController.undoStack()->undo();
    }

    Document workspaceDocument;
    const EntityId workspaceStock = workspaceDocument.createVariable(ElementKind::Stock);
    Q_UNUSED(workspaceStock)
    const qds::io::Workspace workspace {true, 0.66, {123.5, 456.25}};
    QTemporaryDir workspaceDirectory;
    if (!workspaceDirectory.isValid())
        return 40;
    const QString workspacePath = workspaceDirectory.filePath(QStringLiteral("workspace.qds"));
    QString workspaceError;
    const int workspaceIterations = qMax(1, count / 10);
    timer.restart();
    int workspaceCount = 0;
    for (int index = 0; index < workspaceIterations; ++index) {
        if (!qds::io::DocumentFile::save(workspacePath, workspaceDocument, workspace, &workspaceError))
            return 41;
        Document loaded;
        qds::io::Workspace loadedWorkspace;
        if (!qds::io::DocumentFile::open(workspacePath, loaded, loadedWorkspace, &workspaceError)
            || loadedWorkspace != workspace)
            return 42;
        ++workspaceCount;
    }
    const qint64 workspaceNs = timer.nsecsElapsed();

    DocumentController pngController;
    DiagramScene pngScene(&pngController);
    QVector<EntityId> pngIds;
    for (int index = 0; index < 20; ++index) {
        const ElementKind kind = index % 2 == 0 ? ElementKind::Stock : ElementKind::Auxiliary;
        pngIds.push_back(pngController.createVariable(kind, QPointF((index % 5) * 180.0, (index / 5) * 140.0)));
    }
    for (int index = 1; index < 20; ++index)
        pngController.createConnector(pngIds[static_cast<std::size_t>(index - 1)], pngIds[static_cast<std::size_t>(index)]);
    const int pngIterations = qMax(1, count / 10);
    timer.restart();
    int pngRenderCount = 0;
    quint32 pngChecksum = 0;
    for (int index = 0; index < pngIterations; ++index) {
        const QImage image = pngScene.renderImage();
        pngChecksum += image.pixelColor(0, 0).rgb();
        ++pngRenderCount;
    }
    const qint64 pngRenderNs = timer.nsecsElapsed();
    pngScene.item(pngIds.front())->setSelected(true);
    timer.restart();
    int pngSelectionRenderCount = 0;
    for (int index = 0; index < pngIterations; ++index) {
        const QImage image = pngScene.renderImage(true);
        if (image.isNull())
            return 34;
        ++pngSelectionRenderCount;
    }
    const qint64 pngSelectionRenderNs = timer.nsecsElapsed();
    const QImage pngSelectionImage = pngScene.renderImage(true);
    const QImage pngImage = pngScene.renderImage();
    const bool pngSelectionSmaller = pngSelectionImage.width() < pngImage.width() && pngScene.item(pngIds.front())->isSelected();
    QTemporaryDir pngDirectory;
    if (!pngDirectory.isValid() || pngImage.isNull())
        return 29;
    const QString pngPath = pngDirectory.filePath(QStringLiteral("profile.png"));
    const int pngFileIterations = qMax(1, count / 50);
    timer.restart();
    int pngFileCount = 0;
    for (int index = 0; index < pngFileIterations; ++index) {
        if (!pngImage.save(pngPath, "PNG"))
            return 30;
        ++pngFileCount;
    }
    const qint64 pngFileNs = timer.nsecsElapsed();
    QMimeData previousClipboard;
    const QMimeData *clipboardData = QApplication::clipboard()->mimeData();
    if (clipboardData) {
        for (const QString &format : clipboardData->formats())
            previousClipboard.setData(format, clipboardData->data(format));
    }
    timer.restart();
    int pngClipboardCount = 0;
    for (int index = 0; index < pngIterations; ++index) {
        QApplication::clipboard()->setImage(pngSelectionImage);
        ++pngClipboardCount;
    }
    const qint64 pngClipboardNs = timer.nsecsElapsed();
    auto *restoreClipboard = new QMimeData;
    for (const QString &format : previousClipboard.formats())
        restoreClipboard->setData(format, previousClipboard.data(format));
    QApplication::clipboard()->setMimeData(restoreClipboard);

    Q_INIT_RESOURCE(i18n);
    timer.restart();
    int translationLoadCount = 0;
    for (int index = 0; index < count; ++index) {
        QTranslator translator;
        if (!translator.load(QStringLiteral(":/i18n/qds_zh_CN.qm")))
            return 39;
        ++translationLoadCount;
    }
    const qint64 translationLoadNs = timer.nsecsElapsed();

    QTemporaryDir layoutDirectory;
    if (!layoutDirectory.isValid())
        return 25;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, layoutDirectory.path());
    QCoreApplication::setOrganizationName(QStringLiteral("QDSProfile"));
    QCoreApplication::setApplicationName(QStringLiteral("WindowLayout"));
    const int layoutIterations = qMax(1, count / 10);
    qint64 windowLayoutNs = 0;
    int windowLayoutCount = 0;
    for (int index = 0; index < layoutIterations; ++index) {
        MainWindow window;
        window.show();
        QThread::msleep(20);
        QApplication::processEvents();
        window.resize(977, 633);
        window.move(41, 61);
        const QSize expectedSize = window.size();
        timer.restart();
        window.close();
        QSettings settings;
        const QByteArray expectedGeometry = settings.value(QStringLiteral("mainWindow/geometry")).toByteArray();
        const QByteArray expectedState = settings.value(QStringLiteral("mainWindow/state")).toByteArray();
        MainWindow restored;
        windowLayoutNs += timer.nsecsElapsed();
        if (expectedGeometry.isEmpty() || restored.size() != expectedSize)
            return 26;
        if (restored.saveState() != expectedState)
            return 27;
        ++windowLayoutCount;
    }

    QTemporaryDir directory;
    if (!directory.isValid())
        return 3;
    const QString path = directory.filePath(QStringLiteral("profile.qds"));
    QString error;
    controller.setFontStyle(ids.front(), QFont(QStringLiteral("Arial"), 18));
    timer.restart();
    if (!qds::io::DocumentFile::save(path, controller.document(), qds::io::Workspace {}, &error))
        return 4;
    const qint64 saveNs = timer.nsecsElapsed();

    Document loaded;
    timer.restart();
    qds::io::Workspace loadedWorkspace;
    if (!qds::io::DocumentFile::open(path, loaded, loadedWorkspace, &error))
        return 5;
    const qint64 loadNs = timer.nsecsElapsed();
    const ModelElement *loadedFontStock = loaded.findByName("Stock1");

    const QString xmilePath = directory.filePath(QStringLiteral("profile.xmile"));
    timer.restart();
    if (!qds::io::XmileFile::exportFile(xmilePath, controller.document(), &error))
        return 11;
    const qint64 exportNs = timer.nsecsElapsed();
    Document exported;
    timer.restart();
    if (!qds::io::XmileFile::importFile(xmilePath, exported, &error))
        return 12;
    const qint64 importNs = timer.nsecsElapsed();

    Document sir;
    timer.restart();
    for (int index = 0; index < 100; ++index) {
        sir.clear();
        if (!qds::io::XmileFile::importFile(QStringLiteral(QDS_SIR_XMILE_PATH), sir, &error))
            return 13;
    }
    const qint64 sirImportNs = timer.nsecsElapsed();
    std::size_t sirModelCount = 0;
    std::size_t sirConnectorCount = 0;
    bool sirRoutesAutomatic = true;
    for (const ModelElement &element : sir.model().elements()) {
        Q_UNUSED(element)
        ++sirModelCount;
    }
    for (const ViewElement &element : sir.diagram().elements()) {
        if (element.kind() == ElementKind::Connector)
            ++sirConnectorCount;
    }
    for (const auto &entry : sir.diagram().routes()) {
        sirRoutesAutomatic = sirRoutesAutomatic && !entry.second.manual;
        for (const RoutePoint &point : entry.second.controlPoints)
            sirRoutesAutomatic = sirRoutesAutomatic && point.automatic;
    }

    DocumentController sirController;
    DiagramScene sirScene(&sirController);
    sirController.replaceDocument(std::move(sir));
    const ModelElement *infectious = sirController.document().findByName("infectious");
    if (!infectious)
        return 14;
    QVector<ArrowItem *> dependentArrows;
    for (const ViewElement &element : sirController.document().diagram().elements()) {
        if (element.kind() != ElementKind::Connector)
            continue;
        auto *arrow = dynamic_cast<ArrowItem *>(sirScene.item(element.id()));
        const auto *sourceFlow = dynamic_cast<FlowItem *>(arrow->from());
        const auto *targetFlow = dynamic_cast<FlowItem *>(arrow->to());
        if ((sourceFlow && sourceFlow->connects(infectious->id()))
            || (targetFlow && targetFlow->connects(infectious->id())))
            dependentArrows.push_back(arrow);
    }
    timer.restart();
    double dependentEndpointChecksum = 0.0;
    const Vec2 initialInfectiousPosition = sirController.document().diagram().position(infectious->id());
    const QPointF infectiousPosition(initialInfectiousPosition.x, initialInfectiousPosition.y);
    for (int index = 0; index < 1000; ++index) {
        const QPointF offset(index % 2 == 0 ? 80.0 : 0.0, index % 2 == 0 ? 60.0 : 0.0);
        sirController.moveElements({{infectious->id(), infectiousPosition + offset}});
        for (ArrowItem *arrow : dependentArrows)
            dependentEndpointChecksum += arrow->geometryPath().pointAtPercent(0.5).x();
    }
    const qint64 sirDependentMoveNs = timer.nsecsElapsed();

    std::size_t exportedModelCount = 0;
    for (const ModelElement &element : exported.model().elements()) {
        Q_UNUSED(element)
        ++exportedModelCount;
    }

    bool variableKindsMatch = true;
    std::size_t originalModelCount = 0;
    for (const ModelElement &element : controller.document().model().elements()) {
        ++originalModelCount;
        const ModelElement *loadedElement = loaded.findByName(element.displayName());
        variableKindsMatch = variableKindsMatch && loadedElement && loadedElement->kind() == element.kind();
    }

    DocumentController sessionDocument;
    Document sessionModel;
    const EntityId sessionStock = sessionModel.createVariable(ElementKind::Stock);
    const EntityId sessionCloud = sessionModel.createCloud();
    sessionModel.createFlow(sessionCloud, sessionStock);
    sessionModel.setInitialEquation(sessionStock, "0");
    sessionModel.setEquation(sessionModel.findByName("Flow1")->id(), "1");
    sessionModel.setSimulationSettings({0.0, 1.0, 1.0, 1.0});
    sessionDocument.replaceDocument(std::move(sessionModel));
    SimulationController session(&sessionDocument);
    timer.restart();
    int sessionCount = 0;
    int sessionSamples = 0;
    for (int index = 0; index < count; ++index) {
        if (!session.start("Run", true))
            return 50;
        QElapsedTimer waitTimer;
        waitTimer.start();
        while (session.isBusy()) {
            if (waitTimer.elapsed() > 10000)
                return 51;
            QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
        }
        if (session.status() != SimulationSessionStatus::Completed || !session.lastRun()
            || session.lastRun()->times().size() != 2)
            return 52;
        sessionSamples += session.lastRun()->times().size();
        ++sessionCount;
    }
    const qint64 sessionNs = timer.nsecsElapsed();
    session.reset();

    timer.restart();
    int settingsDialogCount = 0;
    for (int index = 0; index < count; ++index) {
        SimulationSettingsDialog dialog(SimulationSettings {});
        dialog.findChild<QDoubleSpinBox *>("dt")->setValue(0.5);
        dialog.accept();
        if (dialog.result() != QDialog::Accepted || dialog.settings().saveInterval() != 0.5)
            return 53;
        ++settingsDialogCount;
    }
    const qint64 settingsDialogNs = timer.nsecsElapsed();

    const qint64 commands = controller.undoStack()->count();
    QTextStream(stdout) << "operation,count,total_ms,per_operation_us\n"
                        << "editor_create," << count << "," << createNs / 1e6 << "," << createNs / 1000.0 / count << "\n"
                        << "editor_connect," << connectionCount << "," << connectNs / 1e6 << "," << connectNs / 1000.0 / connectionCount << "\n"
                        << "route_edit," << connectionCount << "," << routeEditNs / 1e6 << "," << routeEditNs / 1000.0 / connectionCount << "\n"
                        << "route_geometry," << connectionCount << "," << routeGeometryNs / 1e6 << "," << routeGeometryNs / 1000.0 / connectionCount << "\n"
                        << "editor_move_two,2," << moveNs / 1e6 << "," << moveNs / 1000.0 / 2 << "\n"
                        << "editor_undo_redo_macro,2," << undoRedoNs / 1e6 << "," << undoRedoNs / 1000.0 / 2 << "\n"
                        << "editor_align," << count << "," << alignNs / 1e6 << "," << alignNs / 1000.0 / count << "\n"
                        << "editor_paste," << pasted.size() << "," << pasteNs / 1e6 << "," << pasteNs / 1000.0 / count << "\n"
                        << "editor_search_100," << count * 100 << "," << searchNs / 1e6 << "," << searchNs / 1000.0 / (count * 100) << "\n"
                        << "rubber_preview,1000," << previewNs / 1e6 << "," << previewNs / 1000.0 / 1000 << "\n"
                        << "rubber_band_resize,1000," << areaResizeNs / 1e6 << "," << areaResizeNs / 1000.0 / 1000 << "\n"
                        << "space_pan,1000," << spacePanNs / 1e6 << "," << spacePanNs / 1000.0 / 1000 << "\n"
                        << "space_focus_reset,1000," << spaceFocusResetNs / 1e6 << "," << spaceFocusResetNs / 1000.0 / 1000 << "\n"
                        << "cursor_status_update,1000," << cursorStatusNs / 1e6 << "," << cursorStatusNs / 1000.0 / 1000 << "\n"
                        << "move_undo_repaint," << moveUndoCount << "," << moveUndoNs / 1e6 << "," << moveUndoNs / 1000.0 / moveUndoCount << "\n"
                        << "connection_context_menu,1000," << contextMenuNs / 1e6 << "," << contextMenuNs / 1000.0 / 1000 << "\n"
                        << "inline_rename," << inlineRenameCount << "," << inlineRenameNs / 1e6 << "," << inlineRenameNs / 1000.0 / count << "\n"
                        << "connector_line_style,1000," << connectorStyleNs / 1e6 << "," << connectorStyleNs / 1000.0 / 1000 << "\n"
                        << "orthogonal_auto_route,1000," << automaticRouteNs / 1e6 << "," << automaticRouteNs / 1000.0 / 1000 << "\n"
                        << "bezier_control_guide_updates,1000," << bezierGuideNs / 1e6 << "," << bezierGuideNs / 1000.0 / 1000 << "\n"
                        << "automatic_bezier_edit_reset,1000," << automaticBezierEditNs / 1e6 << "," << automaticBezierEditNs / 1000.0 / 1000 << "\n"
                        << "connector_drag_selection,1000," << connectorDragStateNs / 1e6 << "," << connectorDragStateNs / 1000.0 / 1000 << "\n"
                        << "endpoint_preview_cancel,1000," << endpointPreviewNs / 1e6 << "," << endpointPreviewNs / 1000.0 / 1000 << "\n"
                        << "flow_endpoint_preview_dependents," << flowEndpointPreviewCount << "," << flowEndpointPreviewNs / 1e6 << "," << flowEndpointPreviewNs / 1000.0 / count << "\n"
                        << "connector_create_to_flow," << connectorToFlowCount << "," << connectorToFlowNs / 1e6 << "," << connectorToFlowNs / 1000.0 / count << "\n"
                        << "endpoint_target_highlight," << endpointTargetHighlightCount << "," << endpointTargetHighlightNs / 1e6 << "," << endpointTargetHighlightNs / 1000.0 / count << "\n"
                        << "bezier_guide_single_selection,1000," << guideSelectionNs / 1e6 << "," << guideSelectionNs / 1000.0 / 1000 << "\n"
                        << "flow_valve_geometry,1000," << valveNs / 1e6 << "," << valveNs / 1000.0 / 1000 << "\n"
                        << "flow_pipeline_geometry,1000," << pipelineNs / 1e6 << "," << pipelineNs / 1000.0 / 1000 << "\n"
                        << "flow_arrow_geometry,1000," << flowArrowNs / 1e6 << "," << flowArrowNs / 1000.0 / 1000 << "\n"
                        << "flow_label_geometry,1000," << flowLabelNs / 1e6 << "," << flowLabelNs / 1000.0 / 1000 << "\n"
                        << "flow_center_boundary,1000," << centerBoundaryNs / 1e6 << "," << centerBoundaryNs / 1000.0 / 1000 << "\n"
                        << "shift_drag,1000," << modifierDragNs / 1e6 << "," << modifierDragNs / 1000.0 / 1000 << "\n"
                        << "blank_flow_create_undo_redo,1000," << blankFlowNs / 1e6 << "," << blankFlowNs / 1000.0 / 1000 << "\n"
                        << "comment_content_edit,1000," << commentEditNs / 1e6 << "," << commentEditNs / 1000.0 / 1000 << "\n"
                        << "property_panel_refresh,1000," << propertyPanelNs / 1e6 << "," << propertyPanelNs / 1000.0 / 1000 << "\n"
                        << "font_style_edit," << fontStyleCount << "," << fontStyleNs / 1e6 << "," << fontStyleNs / 1000.0 / count << "\n"
                        << "workspace_roundtrip," << workspaceCount << "," << workspaceNs / 1e6 << "," << workspaceNs / 1000.0 / workspaceCount << "\n"
                        << "png_render," << pngRenderCount << "," << pngRenderNs / 1e6 << "," << pngRenderNs / 1000.0 / pngRenderCount << "\n"
                        << "png_selection_render," << pngSelectionRenderCount << "," << pngSelectionRenderNs / 1e6 << "," << pngSelectionRenderNs / 1000.0 / pngSelectionRenderCount << "\n"
                        << "png_file_export," << pngFileCount << "," << pngFileNs / 1e6 << "," << pngFileNs / 1000.0 / pngFileCount << "\n"
                        << "png_clipboard_copy," << pngClipboardCount << "," << pngClipboardNs / 1e6 << "," << pngClipboardNs / 1000.0 / pngClipboardCount << "\n"
                        << "translation_load," << translationLoadCount << "," << translationLoadNs / 1e6 << "," << translationLoadNs / 1000.0 / translationLoadCount << "\n"
                        << "window_layout_restore," << windowLayoutCount << "," << windowLayoutNs / 1e6 << "," << windowLayoutNs / 1000.0 / windowLayoutCount << "\n"
                        << "qds_save," << count << "," << saveNs / 1e6 << "," << saveNs / 1000.0 / count << "\n"
                        << "qds_open," << count << "," << loadNs / 1e6 << "," << loadNs / 1000.0 / count << "\n"
                        << "xmile_export," << count << "," << exportNs / 1e6 << "," << exportNs / 1000.0 / count << "\n"
                        << "xmile_import," << count << "," << importNs / 1e6 << "," << importNs / 1000.0 / count << "\n"
                        << "xmile_sir_import,100," << sirImportNs / 1e6 << "," << sirImportNs / 1000.0 / 100 << "\n"
                        << "sir_dependent_route_moves,1000," << sirDependentMoveNs / 1e6 << "," << sirDependentMoveNs / 1000.0 / 1000 << "\n"
                        << "simulation_session," << sessionCount << "," << sessionNs / 1e6 << "," << sessionNs / 1000.0 / sessionCount << "\n"
                        << "simulation_session_samples," << sessionSamples << ",\n"
                        << "simulation_settings_dialog," << settingsDialogCount << "," << settingsDialogNs / 1e6 << ","
                        << settingsDialogNs / 1000.0 / settingsDialogCount << "\n";
    const bool valid = commands > 0 && pasted.size() == static_cast<std::size_t>(count) && pointCount > 0 && arrowCount > 0
        && inlineRenameCount == count && areaResizeCount == 1000 && spacePanCount == 1000
        && spaceFocusResetCount == 1000 && cursorStatusCount == 1000
        && moveUndoCount == moveUndoIterations && matches > 0 && guideCount == 2000 && automaticGuideCount == 2000 && automaticGroupManual
        && connectorDragGuideCount == 2000 && connectorDragStateValid
        && endpointPreviewGuideCount == 2000 && endpointPreviewHandlesValid
        && flowEndpointPreviewCount == count
        && connectorToFlowCount == count && endpointTargetHighlightCount == count
        && singleSelectionGuideCount == 1000 && valveCount == 2000
        && lineStyleChecksum == 3000 && automaticPointCount > 0 && automaticFlagsValid
        && pipelineCount == 2000 && flowArrowPointCount == 3000 && flowLabelCount == 1000
        && centerBoundaryCount == 1000 && variableKindsMatch && windowLayoutCount == layoutIterations
        && fontStyleCount == count && loadedFontStock
        && translationLoadCount == count && workspaceCount == workspaceIterations
        && pngRenderCount == pngIterations && pngSelectionRenderCount == pngIterations && pngSelectionSmaller
        && pngFileCount == pngFileIterations && pngClipboardCount == pngIterations && pngChecksum != 0
        && loaded.diagram().fontStyle(loadedFontStock->id()) == (FontStyle {"Arial", 18})
        && exportedModelCount == originalModelCount && sirModelCount == 8 && sirConnectorCount == 6 && sirRoutesAutomatic
        && !dependentArrows.empty() && dependentEndpointChecksum != 0.0
        && sessionCount == count && sessionSamples == count * 2 && settingsDialogCount == count
        && elementCount(loaded) == static_cast<std::size_t>(count) + connectionIds.size();
    if (!valid)
        QTextStream(stderr) << "validation failed: commands=" << commands << ", points=" << pointCount
                            << ", arrows=" << arrowCount << ", loaded=" << elementCount(loaded) << "\n";
    return valid ? 0 : 1;
}
