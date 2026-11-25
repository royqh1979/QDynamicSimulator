#include "DiagramItems.h"
#include "DiagramScene.h"
#include "DiagramView.h"
#include "DocumentController.h"
#include "PropertyPanel.h"
#include <qds/io/DocumentFile.h>
#include <qds/io/XmileFile.h>

#include <QActionGroup>
#include <QApplication>
#include <QElapsedTimer>
#include <QGraphicsLineItem>
#include <QGraphicsSceneMouseEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QTemporaryDir>
#include <QTextStream>

#include <vector>

using namespace qds::core;

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
    std::vector<EntityId> ids;
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
    std::vector<EntityId> connectionIds;
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
        if (!flow || flow->endpoints.to != target || previewFlowItem->previewing()
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
        const std::vector<EntityId> selection = flowTargetScene.selectedElementIds();
        if (selection.size() != 1)
            return 17;
        const auto *connector = dynamic_cast<const Connector *>(flowTargetController.document().find(selection.front()));
        const auto *arrow = dynamic_cast<const ArrowItem *>(flowTargetScene.item(selection.front()));
        if (!connector || !arrow || connector->from != flowTargetSource || connector->to != flowTargetFlow
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
        const FlowEndpoints endpoints = static_cast<const Flow *>(blankController.document().find(id))->endpoints;
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

    QTemporaryDir directory;
    if (!directory.isValid())
        return 3;
    const QString path = directory.filePath(QStringLiteral("profile.qds"));
    QString error;
    timer.restart();
    if (!qds::io::DocumentFile::save(path, controller.document(), &error))
        return 4;
    const qint64 saveNs = timer.nsecsElapsed();

    Document loaded;
    timer.restart();
    if (!qds::io::DocumentFile::open(path, loaded, &error))
        return 5;
    const qint64 loadNs = timer.nsecsElapsed();

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
    std::vector<ArrowItem *> dependentArrows;
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
        const ModelElement *loadedElement = loaded.findByName(element.displayName);
        variableKindsMatch = variableKindsMatch && loadedElement && loadedElement->kind() == element.kind();
    }

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
                        << "connection_context_menu,1000," << contextMenuNs / 1e6 << "," << contextMenuNs / 1000.0 / 1000 << "\n"
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
                        << "qds_save," << count << "," << saveNs / 1e6 << "," << saveNs / 1000.0 / count << "\n"
                        << "qds_open," << count << "," << loadNs / 1e6 << "," << loadNs / 1000.0 / count << "\n"
                        << "xmile_export," << count << "," << exportNs / 1e6 << "," << exportNs / 1000.0 / count << "\n"
                        << "xmile_import," << count << "," << importNs / 1e6 << "," << importNs / 1000.0 / count << "\n"
                        << "xmile_sir_import,100," << sirImportNs / 1e6 << "," << sirImportNs / 1000.0 / 100 << "\n"
                        << "sir_dependent_route_moves,1000," << sirDependentMoveNs / 1e6 << "," << sirDependentMoveNs / 1000.0 / 1000 << "\n";
    const bool valid = commands > 0 && pasted.size() == static_cast<std::size_t>(count) && pointCount > 0 && arrowCount > 0
        && matches > 0 && guideCount == 2000 && automaticGuideCount == 2000 && automaticGroupManual
        && connectorDragGuideCount == 2000 && connectorDragStateValid
        && endpointPreviewGuideCount == 2000 && endpointPreviewHandlesValid
        && flowEndpointPreviewCount == count
        && connectorToFlowCount == count && endpointTargetHighlightCount == count
        && singleSelectionGuideCount == 1000 && valveCount == 2000
        && lineStyleChecksum == 3000 && automaticPointCount > 0 && automaticFlagsValid
        && pipelineCount == 2000 && flowArrowPointCount == 3000 && flowLabelCount == 1000
        && centerBoundaryCount == 1000 && variableKindsMatch
        && exportedModelCount == originalModelCount && sirModelCount == 8 && sirConnectorCount == 6 && sirRoutesAutomatic
        && !dependentArrows.empty() && dependentEndpointChecksum != 0.0
        && elementCount(loaded) == static_cast<std::size_t>(count) + connectionIds.size();
    if (!valid)
        QTextStream(stderr) << "validation failed: commands=" << commands << ", points=" << pointCount
                            << ", arrows=" << arrowCount << ", loaded=" << elementCount(loaded) << "\n";
    return valid ? 0 : 1;
}
