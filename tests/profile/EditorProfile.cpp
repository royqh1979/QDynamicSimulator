#include "DiagramItems.h"
#include "DiagramScene.h"
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
        route.controlPoints = {{50.0, 60.0}};
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
    guideItem->setSelected(true);
    guideScene.setSelectedRouteMode(RouteMode::Bezier);
    Route guideRoute = guideController.document().diagram().route(guideConnection);
    timer.restart();
    int guideCount = 0;
    for (int index = 0; index < 1000; ++index) {
        const double y = index % 2 == 0 ? -80.0 : 80.0;
        guideRoute.controlPoints[0].y = y;
        guideRoute.controlPoints[1].y = y;
        guideItem->setRoute(guideRoute);
        guideCount += guideItem->controlGuides().size();
    }
    const qint64 bezierGuideNs = timer.nsecsElapsed();

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
    for (const auto &entry : sir.diagram().routes())
        sirRoutesAutomatic = sirRoutesAutomatic && !entry.second.manual && entry.second.controlPoints.empty();

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
                        << "bezier_control_guide_updates,1000," << bezierGuideNs / 1e6 << "," << bezierGuideNs / 1000.0 / 1000 << "\n"
                        << "bezier_guide_single_selection,1000," << guideSelectionNs / 1e6 << "," << guideSelectionNs / 1000.0 / 1000 << "\n"
                        << "flow_valve_geometry,1000," << valveNs / 1e6 << "," << valveNs / 1000.0 / 1000 << "\n"
                        << "flow_pipeline_geometry,1000," << pipelineNs / 1e6 << "," << pipelineNs / 1000.0 / 1000 << "\n"
                        << "flow_arrow_geometry,1000," << flowArrowNs / 1e6 << "," << flowArrowNs / 1000.0 / 1000 << "\n"
                        << "flow_label_geometry,1000," << flowLabelNs / 1e6 << "," << flowLabelNs / 1000.0 / 1000 << "\n"
                        << "shift_drag,1000," << modifierDragNs / 1e6 << "," << modifierDragNs / 1000.0 / 1000 << "\n"
                        << "blank_flow_create_undo_redo,1000," << blankFlowNs / 1e6 << "," << blankFlowNs / 1000.0 / 1000 << "\n"
                        << "comment_content_edit,1000," << commentEditNs / 1e6 << "," << commentEditNs / 1000.0 / 1000 << "\n"
                        << "property_panel_refresh,1000," << propertyPanelNs / 1e6 << "," << propertyPanelNs / 1000.0 / 1000 << "\n"
                        << "qds_save," << count << "," << saveNs / 1e6 << "," << saveNs / 1000.0 / count << "\n"
                        << "qds_open," << count << "," << loadNs / 1e6 << "," << loadNs / 1000.0 / count << "\n"
                        << "xmile_export," << count << "," << exportNs / 1e6 << "," << exportNs / 1000.0 / count << "\n"
                        << "xmile_import," << count << "," << importNs / 1e6 << "," << importNs / 1000.0 / count << "\n"
                        << "xmile_sir_import,100," << sirImportNs / 1e6 << "," << sirImportNs / 1000.0 / 100 << "\n";
    const bool valid = commands > 0 && pasted.size() == static_cast<std::size_t>(count) && pointCount > 0 && arrowCount > 0
        && matches > 0 && guideCount == 2000 && singleSelectionGuideCount == 1000 && valveCount == 2000
        && lineStyleChecksum == 3000
        && pipelineCount == 2000 && flowArrowPointCount == 3000 && flowLabelCount == 1000 && variableKindsMatch
        && exportedModelCount == originalModelCount && sirModelCount == 8 && sirConnectorCount == 6 && sirRoutesAutomatic
        && elementCount(loaded) == static_cast<std::size_t>(count) + connectionIds.size();
    if (!valid)
        QTextStream(stderr) << "validation failed: commands=" << commands << ", points=" << pointCount
                            << ", arrows=" << arrowCount << ", loaded=" << elementCount(loaded) << "\n";
    return valid ? 0 : 1;
}
