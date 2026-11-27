#include "DiagramItems.h"
#include "DiagramPalette.h"
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
#include "SimulationController.h"
#include "CausesStripWidget.h"
#include "RunsCompareModel.h"
#include "SimulationResultsWidget.h"
#include "SimulationSettingsDialog.h"
#include "SimulationTableModel.h"
#include "TimeSeriesChartWidget.h"
#include <io/DocumentFile.h>
#include <io/XmileFile.h>

#include <QAbstractButton>
#include <QChart>
#include <QClipboard>
#include <QComboBox>
#include <QVector>
#include <QDir>
#include <QDockWidget>
#include <QDropEvent>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QFocusEvent>
#include <QFontComboBox>
#include <QFrame>
#include <QGraphicsProxyWidget>
#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsSceneMouseEvent>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QLineSeries>
#include <QLegendMarker>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QScreen>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolBar>
#include <QTableView>
#include <QTabWidget>
#include <QValueAxis>
#include <QTranslator>
#include <QUrl>
#include <QWheelEvent>
#include <QRegularExpression>
#include <QtTest>

#include <cmath>
#include <limits>
#include <optional>

using namespace qds::core;

class PaintRegionRecorder : public QObject
{
public:
    QRegion region;

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::Paint)
            region |= static_cast<QPaintEvent *>(event)->region();
        return QObject::eventFilter(watched, event);
    }
};

class TestEntityPalette : public EntityPalette
{
public:
    using EntityPalette::mimeData;
};

QGraphicsProxyWidget *inlineNameEditorProxy(QGraphicsScene &scene)
{
    for (QGraphicsItem *item : scene.items()) {
        if (auto *proxy = qgraphicsitem_cast<QGraphicsProxyWidget *>(item)) {
            if (auto *editor = qobject_cast<QLineEdit *>(proxy->widget()); editor && editor->objectName() == QStringLiteral("inlineNameEditor"))
                return proxy;
        }
    }
    return nullptr;
}

QLineEdit *inlineNameEditor(QGraphicsScene &scene)
{
    QGraphicsProxyWidget *proxy = inlineNameEditorProxy(scene);
    return proxy ? qobject_cast<QLineEdit *>(proxy->widget()) : nullptr;
}

QImage viewportImage(DiagramView &view)
{
    if (QGuiApplication::platformName() == QStringLiteral("windows"))
        return QGuiApplication::primaryScreen()->grabWindow(view.viewport()->winId()).toImage();
    return view.grab().toImage();
}

int itemColorCount(const QImage &image, DiagramView &view, const QGraphicsItem *item, const QColor &color)
{
    const QRect area = view.mapFromScene(item->sceneBoundingRect()).boundingRect().intersected(image.rect());
    int count = 0;
    for (int y = area.top(); y <= area.bottom(); ++y) {
        for (int x = area.left(); x <= area.right(); ++x)
            count += image.pixelColor(x, y) == color ? 1 : 0;
    }
    return count;
}

class TestEditor : public QObject
{
    Q_OBJECT

private slots:
    void createUndoRedo();
    void renameAndEquationUndo();
    void inlineRenameUndoRedo();
    void flowAndCommentInlineEditing();
    void namesAllowSpaces();
    void commentContentEditing();
    void propertyPanelWidget();
    void fontStyleUndoRedo();
    void moveUndoRedo();
    void undoRedoCanvasRepaint();
    void directConnectorMoveUndoRepaint();
    void multipleFlowConnectorsMoveUndoRepaint();
    void flowAndCascadeRestore();
    void routeUndoRedo();
    void replaceDocumentResetsScene();
    void missingViewImportProjectsAllVariables();
    void connectorValidation();
    void connectionDeleteUndo();
    void layoutOperations();
    void gridAndSceneBounds();
    void viewOperations();
    void cloudShapeAndBoundary();
    void routeModesAndGeometry();
    void bezierControlGeometry();
    void flowRoutesAndValveGeometry();
    void flowLabelRepaint();
    void flowDefaultAndSelectionColors();
    void bezierControlGuideRendering();
    void controlPointMouseRepaint();
    void automaticControlPointMouseEdit();
    void controlPointDoubleClick();
    void reconnectUndo();
    void boundaryEndpoints();
    void sirCanvasConnections();
    void endpointHandleReconnect();
    void flowEndpointPreviewDependentsRepaint();
    void connectorEndpointCrossingRepaint();
    void endpointTargetHighlight();
    void connectorMoveRepaint();
    void connectorDragSelectionRepaint();
    void rubberBandReverseRepaint();
    void automaticAndRelativeRoutePoints();
    void spatialIndexAndRouter();
    void equationEditor();
    void clipboardOperations();
    void recentSearchAndMiniMap();
    void paletteDrop();
    void rubberConnectionAndSelection();
    void connectToolTargetsFlow();
    void connectionContextMenu();
    void orthogonalArrowDirections();
    void actionIcons();
    void unifiedIdUndoRedo();
    void modifierDragOperations();
    void fileDropOpen();
    void mdlOpenAndDrop();
    void documentTitlesAndSaveNames();
    void pngExportAndClipboard();
    void workspaceRestore();
    void simulationControls();
    void resultsDock();
    void causesStripGraph();
    void runsCompare();
    void resultsAutoOpenCurrentRun();
    void simulationSettingsAction();
    void stockInitialValuePresentation();
    void recoveryRestoresWorkspace();
    void cursorPositionStatus();
    void windowLayoutPersistence();
    void simplifiedChineseTranslation();
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
    controller.setInitialEquation(id, QStringLiteral("100"));
    controller.setUnit(id, QStringLiteral("people"));
    controller.setComment(id, QStringLiteral("Population stock"));
    const auto *model = static_cast<const ModelElement *>(controller.document().find(id));
    QCOMPARE(model->unit(), QString("people"));
    QCOMPARE(model->comment(), QString("Population stock"));
    controller.undoStack()->undo();
    controller.undoStack()->undo();
    QCOMPARE(dynamic_cast<VariableItem *>(scene.item(id))->name(), QStringLiteral("Population"));
    controller.undoStack()->undo();
    QCOMPARE(dynamic_cast<const Stock *>(controller.document().find(id))->initialEquation(), QString());
    controller.undoStack()->undo();
    QCOMPARE(static_cast<const ModelElement *>(controller.document().find(id))->displayName(), QString("Stock1"));
    controller.undoStack()->redo();
    controller.undoStack()->redo();
    QCOMPARE(dynamic_cast<const Stock *>(controller.document().find(id))->initialEquation(), QString("100"));
    QVERIFY(!controller.renameElement(id, QString()));
}

void TestEditor::inlineRenameUndoRedo()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    const EntityId stock = controller.createVariable(ElementKind::Stock, QPointF());
    controller.createVariable(ElementKind::Variable, QPointF(0.0, 200.0));
    const EntityId cloud = controller.createCloud(QPointF(400.0, 0.0));
    const EntityId flow = controller.createFlow(stock, cloud);
    DiagramView view(&scene);
    view.resize(800, 600);
    view.centerOn(QPointF(200.0, 0.0));
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    QTest::mouseDClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, view.mapFromScene(scene.item(stock)->center()));
    QLineEdit *editor = inlineNameEditor(scene);
    QVERIFY(editor);
    QCOMPARE(editor->text(), QStringLiteral("Stock1"));
    QVERIFY(editor->styleSheet().contains(QStringLiteral("border: 2px solid")));
    if (QGuiApplication::platformName() == QStringLiteral("windows")) {
        QTest::qWait(20);
        QGraphicsProxyWidget *proxy = inlineNameEditorProxy(scene);
        QVERIFY(proxy);
        QVERIFY(itemColorCount(view.grab().toImage(), view, proxy, diagram_palette::SelectionColor) > 0);
    }
    editor->setText(QStringLiteral("Population"));
    QTest::keyClick(editor, Qt::Key_Return);
    QVERIFY(!inlineNameEditor(scene));
    QCOMPARE(static_cast<const ModelElement *>(controller.document().find(stock))->displayName(), QString("Population"));
    QCOMPARE(dynamic_cast<VariableItem *>(scene.item(stock))->name(), QStringLiteral("Population"));
    controller.undoStack()->undo();
    QCOMPARE(static_cast<const ModelElement *>(controller.document().find(stock))->displayName(), QString("Stock1"));
    controller.undoStack()->redo();
    QCOMPARE(static_cast<const ModelElement *>(controller.document().find(stock))->displayName(), QString("Population"));

    QTest::mouseDClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, view.mapFromScene(scene.item(stock)->center()));
    editor = inlineNameEditor(scene);
    QVERIFY(editor);
    const int commands = controller.undoStack()->count();
    QVERIFY(controller.document().model().findByName("Variable1"));
    editor->setText(QStringLiteral("Variable1"));
    QTest::keyClick(editor, Qt::Key_Return);
    QCOMPARE(inlineNameEditor(scene), editor);
    QCOMPARE(controller.undoStack()->count(), commands);
    QCOMPARE(static_cast<const ModelElement *>(controller.document().find(stock))->displayName(), QString("Population"));
    editor->setText(QStringLiteral("Cancelled"));
    QTest::keyClick(editor, Qt::Key_Escape);
    QVERIFY(!inlineNameEditor(scene));
    QCOMPARE(static_cast<const ModelElement *>(controller.document().find(stock))->displayName(), QString("Population"));

    QTest::mouseDClick(view.viewport(), Qt::LeftButton, Qt::NoModifier,
        view.mapFromScene(dynamic_cast<FlowItem *>(scene.item(flow))->labelRect().center()));
    editor = inlineNameEditor(scene);
    QVERIFY(editor);
    QCOMPARE(editor->text(), QStringLiteral("Flow1"));
    editor->setText(QStringLiteral("Infection"));
    QTest::keyClick(editor, Qt::Key_Return);
    QVERIFY(!inlineNameEditor(scene));
    QCOMPARE(static_cast<const ModelElement *>(controller.document().find(flow))->displayName(), QString("Infection"));
    controller.undoStack()->undo();
    QCOMPARE(static_cast<const ModelElement *>(controller.document().find(flow))->displayName(), QString("Flow1"));
    controller.undoStack()->redo();
    QCOMPARE(static_cast<const ModelElement *>(controller.document().find(flow))->displayName(), QString("Infection"));
}

void TestEditor::flowAndCommentInlineEditing()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    const EntityId stock = controller.createVariable(ElementKind::Stock, QPointF(0.0, 0.0));
    const EntityId cloud = controller.createCloud(QPointF(400.0, 0.0));
    const EntityId flow = controller.createFlow(stock, cloud);
    controller.renameElement(flow, QStringLiteral("Rate"));
    const EntityId comment = controller.createComment(QStringLiteral("Original comment"), QPointF(200.0, 250.0));
    auto *flowItem = dynamic_cast<FlowItem *>(scene.item(flow));
    auto *commentItem = dynamic_cast<ViewObjectItem *>(scene.item(comment));
    QVERIFY(flowItem && commentItem);

    DiagramView view(&scene);
    view.resize(800, 600);
    view.centerOn(200.0, 100.0);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    QTest::mouseDClick(view.viewport(), Qt::LeftButton, Qt::NoModifier,
        view.mapFromScene(flowItem->labelRect().center()));
    QLineEdit *nameEditor = inlineNameEditor(scene);
    QVERIFY(nameEditor);
    QCOMPARE(nameEditor->text(), QStringLiteral("Rate"));
    nameEditor->setText(QStringLiteral("Edited rate"));
    QTest::keyClick(nameEditor, Qt::Key_Return);
    QCOMPARE(controller.document().findByName("Edited rate")->id(), flow);

    QTest::mouseDClick(view.viewport(), Qt::LeftButton, Qt::NoModifier,
        view.mapFromScene(flowItem->center()));
    QVERIFY(!inlineNameEditor(scene));

    QTest::mouseDClick(view.viewport(), Qt::LeftButton, Qt::NoModifier,
        view.mapFromScene(commentItem->center()));
    QLineEdit *commentEditor = nullptr;
    for (QGraphicsItem *candidate : scene.items()) {
        auto *proxy = qgraphicsitem_cast<QGraphicsProxyWidget *>(candidate);
        auto *editor = proxy ? qobject_cast<QLineEdit *>(proxy->widget()) : nullptr;
        if (editor && editor->objectName() == QStringLiteral("inlineCommentEditor"))
            commentEditor = editor;
    }
    QVERIFY(commentEditor);
    QCOMPARE(commentEditor->text(), QStringLiteral("Original comment"));
    commentEditor->setText(QStringLiteral("Edited comment"));
    QTest::keyClick(commentEditor, Qt::Key_Return);
    QCOMPARE(dynamic_cast<const Comment *>(controller.document().find(comment))->content(), QStringLiteral("Edited comment"));
    controller.undoStack()->undo();
    QCOMPARE(dynamic_cast<const Comment *>(controller.document().find(comment))->content(), QStringLiteral("Original comment"));
    controller.undoStack()->redo();
    QCOMPARE(dynamic_cast<const Comment *>(controller.document().find(comment))->content(), QStringLiteral("Edited comment"));
}

void TestEditor::namesAllowSpaces()
{
    MainWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto *controller = window.findChild<DocumentController *>();
    auto *scene = window.findChild<DiagramScene *>();
    const EntityId stock = controller->createVariable(ElementKind::Stock, QPointF());
    auto *view = window.findChild<DiagramView *>();
    QVERIFY(view);
    view->centerOn(scene->item(stock));
    QTest::mouseDClick(view->viewport(), Qt::LeftButton, Qt::NoModifier,
        view->mapFromScene(scene->item(stock)->center()));
    auto *editor = inlineNameEditor(*scene);
    QVERIFY(editor);
    editor->setText("birth");
    editor->setCursorPosition(5);
    QVERIFY(editor->hasFocus());
    QTest::keyClick(window.windowHandle(), ' ');
    QCOMPARE(editor->text(), QStringLiteral("birth "));
    for (const char character : {'r', 'a', 't', 'e'})
        QTest::keyClick(window.windowHandle(), character);
    QTest::keyClick(window.windowHandle(), Qt::Key_Return);
    QCOMPARE(controller->document().findByName("birth rate")->id(), stock);

    auto *name = window.findChild<QLineEdit *>("nameEdit");
    QVERIFY(name);
    name->setText("birth");
    name->setCursorPosition(5);
    name->setFocus();
    QVERIFY(name->hasFocus());
    QTest::keyClick(window.windowHandle(), ' ');
    QCOMPARE(name->text(), QStringLiteral("birth "));

    view->centerOn(scene->item(stock));
    const QPoint pressed = view->mapFromScene(scene->item(stock)->center());
    const int scrollX = view->horizontalScrollBar()->value();
    const int scrollY = view->verticalScrollBar()->value();
    QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, pressed);
    QTest::keyClick(window.windowHandle(), ' ');
    QTest::mouseMove(view->viewport(), pressed + QPoint(30.0, 20.0));
    QCOMPARE(view->horizontalScrollBar()->value(), scrollX);
    QCOMPARE(view->verticalScrollBar()->value(), scrollY);
    QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(-50, -50));
    QTest::keyRelease(window.windowHandle(), ' ');
}

void TestEditor::commentContentEditing()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    const EntityId id = controller.createComment(QStringLiteral("Before"), QPointF());
    controller.setCommentContent(id, QStringLiteral("After"));
    QCOMPARE(static_cast<const Comment *>(controller.document().find(id))->content(), QString("After"));
    QCOMPARE(dynamic_cast<ViewObjectItem *>(scene.item(id))->text(), QStringLiteral("After"));
    controller.undoStack()->undo();
    QCOMPARE(static_cast<const Comment *>(controller.document().find(id))->content(), QString("Before"));
    QCOMPARE(dynamic_cast<ViewObjectItem *>(scene.item(id))->text(), QStringLiteral("Before"));
    controller.undoStack()->redo();
    QCOMPARE(static_cast<const Comment *>(controller.document().find(id))->content(), QString("After"));

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
    QCOMPARE(static_cast<const Comment *>(controller.document().find(panelComment))->content(),
             QString("\u4FEE\u6539\u540E\u7684\u6CE8\u91CA"));
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
    const auto *model = dynamic_cast<const Stock *>(controller.document().find(stock));
    QVERIFY(model);
    QCOMPARE(model->displayName(), QString("Population"));
    QCOMPARE(model->initialEquation(), QString("100"));
    QCOMPARE(model->unit(), QString("people"));
    QCOMPARE(model->comment(), QString("Population stock"));
    QCOMPARE(controller.undoStack()->count(), 5);
    controller.undoStack()->undo();
    QCOMPARE(model->comment(), QString());
    controller.undoStack()->undo();
    QCOMPARE(model->unit(), QString());
    controller.undoStack()->undo();
    QCOMPARE(model->initialEquation(), QString());
    controller.undoStack()->undo();
    QCOMPARE(model->displayName(), QString("Stock1"));
    controller.undoStack()->redo();
    QCOMPARE(model->displayName(), QString("Population"));
    controller.undoStack()->redo();
    QCOMPARE(model->initialEquation(), QString("100"));
    controller.undoStack()->redo();
    QCOMPARE(model->unit(), QString("people"));
    controller.undoStack()->redo();
    QCOMPARE(model->comment(), QString("Population stock"));
    scene.clearSelection();
    QVERIFY(!name->isEnabled() && !equation->isEnabled() && !unit->isEnabled() && !comment->isEnabled());
}

void TestEditor::fontStyleUndoRedo()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    PropertyPanel panel(&controller, &scene);
    const EntityId stock = controller.createVariable(ElementKind::Stock, QPointF());
    const EntityId comment = controller.createComment(QStringLiteral("Note"), QPointF(200.0, 0.0));
    scene.item(stock)->setSelected(true);
    auto *fontCombo = panel.findChild<QFontComboBox *>(QStringLiteral("fontCombo"));
    auto *fontSize = panel.findChild<QSpinBox *>(QStringLiteral("fontSize"));
    QVERIFY(fontCombo && fontSize && fontCombo->isEnabled() && fontSize->isEnabled());
    {
        const QSignalBlocker fontBlocker(fontCombo);
        const QSignalBlocker sizeBlocker(fontSize);
        fontCombo->setCurrentFont(QFont(QStringLiteral("Arial")));
        fontSize->setValue(18);
    }
    QVERIFY(panel.commit());
    const FontStyle expected {"Arial", 18};
    QCOMPARE(controller.document().diagram().fontStyle(stock), expected);
    QCOMPARE(scene.item(stock)->labelFont().family(), QStringLiteral("Arial"));
    QCOMPARE(scene.item(stock)->labelFont().pointSize(), 18);
    controller.undoStack()->undo();
    QCOMPARE(controller.document().diagram().fontStyle(stock), FontStyle {});
    controller.undoStack()->redo();
    QCOMPARE(controller.document().diagram().fontStyle(stock), expected);

    const ClipboardData clipboard = controller.copyElements({stock});
    const QVector<EntityId> pasted = controller.pasteClipboard(clipboard, QPointF(100.0, 100.0));
    QCOMPARE(pasted.size(), std::size_t(1));
    QCOMPARE(controller.document().diagram().fontStyle(pasted.front()), expected);
    QCOMPARE(scene.item(pasted.front())->labelFont().pointSize(), 18);
    controller.undoStack()->undo();

    scene.clearSelection();
    scene.item(comment)->setSelected(true);
    {
        const QSignalBlocker fontBlocker(fontCombo);
        const QSignalBlocker sizeBlocker(fontSize);
        fontCombo->setCurrentFont(QFont(QStringLiteral("Times New Roman")));
        fontSize->setValue(16);
    }
    QVERIFY(panel.commit());
    const FontStyle commentFont {"Times New Roman", 16};
    QCOMPARE(controller.document().diagram().fontStyle(comment), commentFont);
    QCOMPARE(scene.item(comment)->labelFont().family(), QStringLiteral("Times New Roman"));
    QCOMPARE(scene.item(comment)->labelFont().pointSize(), 16);
    controller.undoStack()->undo();
    QCOMPARE(controller.document().diagram().fontStyle(comment), FontStyle {});
    controller.undoStack()->redo();
    QCOMPARE(controller.document().diagram().fontStyle(comment), commentFont);
}

void TestEditor::moveUndoRedo()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    const EntityId first = controller.createVariable(ElementKind::Stock, QPointF(0.0, 0.0));
    const EntityId second = controller.createVariable(ElementKind::Variable, QPointF(10.0, 10.0));
    controller.moveElements({{first, QPointF(50.0, 60.0)}, {second, QPointF(70.0, 80.0)}});
    QCOMPARE(scene.item(first)->pos(), QPointF(50.0, 60.0));
    QCOMPARE(scene.item(second)->pos(), QPointF(70.0, 80.0));
    controller.undoStack()->undo();
    QCOMPARE(scene.item(first)->pos(), QPointF(0.0, 0.0));
    QCOMPARE(scene.item(second)->pos(), QPointF(10.0, 10.0));
    controller.undoStack()->redo();
    QCOMPARE(scene.item(first)->pos(), QPointF(50.0, 60.0));
}

void TestEditor::undoRedoCanvasRepaint()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    scene.setGridVisible(false);
    const EntityId sourceStock = controller.createVariable(ElementKind::Stock, QPointF());
    const EntityId oldTarget = controller.createVariable(ElementKind::Stock, QPointF(400.0, 0.0));
    const EntityId newTarget = controller.createVariable(ElementKind::Stock, QPointF(400.0, 400.0));
    const EntityId flow = controller.createFlow(sourceStock, oldTarget);
    const EntityId auxiliary = controller.createVariable(ElementKind::Variable, QPointF(200.0, 300.0));
    controller.createConnector(auxiliary, flow);
    scene.item(auxiliary)->setSelected(true);
    DiagramView view(&scene);
    view.resize(900, 800);
    view.centerOn(QPointF(100.0, 150.0));
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(20);
    const QImage initial = view.grab().toImage();

    controller.moveElements({{auxiliary, QPointF(200.0, 600.0)}});
    QTest::qWait(20);
    controller.undoStack()->undo();
    QTest::qWait(20);
    QCOMPARE(view.grab().toImage(), initial);
    controller.undoStack()->redo();
    QTest::qWait(20);
    controller.undoStack()->undo();
    QTest::qWait(20);
    QCOMPARE(view.grab().toImage(), initial);

    QVERIFY(controller.reconnectFlow(flow, sourceStock, newTarget));
    QTest::qWait(20);
    controller.undoStack()->undo();
    QTest::qWait(20);
    QCOMPARE(view.grab().toImage(), initial);
    controller.undoStack()->redo();
    QTest::qWait(20);
    controller.undoStack()->undo();
    QTest::qWait(20);
    QCOMPARE(view.grab().toImage(), initial);

    controller.removeElements({sourceStock});
    QTest::qWait(20);
    controller.undoStack()->undo();
    QTest::qWait(20);
    QCOMPARE(view.grab().toImage(), initial);
    controller.undoStack()->redo();
    QTest::qWait(20);
    controller.undoStack()->undo();
    QTest::qWait(20);
    QCOMPARE(view.grab().toImage(), initial);
}

void TestEditor::directConnectorMoveUndoRepaint()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    scene.setGridVisible(false);
    const EntityId stock = controller.createVariable(ElementKind::Stock, QPointF());
    const EntityId auxiliary = controller.createVariable(ElementKind::Variable, QPointF(200.0, 300.0));
    controller.createConnector(stock, auxiliary);
    DiagramItem *auxiliaryItem = scene.item(auxiliary);
    auxiliaryItem->setSelected(true);
    DiagramView view(&scene);
    view.resize(900, 800);
    view.centerOn(QPointF(100.0, 150.0));
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(20);
    const QImage initial = view.grab().toImage();
    auto sendMouse = [&view](QEvent::Type type, const QPoint &point, Qt::MouseButton button, Qt::MouseButtons buttons) {
        QMouseEvent event(type, point, view.viewport()->mapToGlobal(point), button, buttons, Qt::NoModifier);
        QApplication::sendEvent(view.viewport(), &event);
    };
    sendMouse(QEvent::MouseButtonPress, view.mapFromScene(auxiliaryItem->center()), Qt::LeftButton, Qt::LeftButton);
    sendMouse(QEvent::MouseMove, view.mapFromScene(QPointF(200.0, 600.0)), Qt::NoButton, Qt::LeftButton);
    sendMouse(QEvent::MouseButtonRelease, view.mapFromScene(QPointF(200.0, 600.0)), Qt::LeftButton, Qt::NoButton);
    QTest::qWait(20);
    controller.undoStack()->undo();
    QTest::qWait(20);
    QCOMPARE(view.grab().toImage(), initial);
    controller.undoStack()->redo();
    QTest::qWait(20);
    controller.undoStack()->undo();
    QTest::qWait(20);
    QCOMPARE(view.grab().toImage(), initial);
}

void TestEditor::multipleFlowConnectorsMoveUndoRepaint()
{
    Document document;
    qds::io::Workspace workspace;
    QString error;
    QVERIFY2(qds::io::DocumentFile::open(QStringLiteral(QDS_MOVE_UNDO_QDS_PATH), document, workspace, &error), qPrintable(error));
    MainWindow window;
    DocumentController &controller = *window.findChild<DocumentController *>();
    DiagramScene &scene = *window.findChild<DiagramScene *>();
    controller.replaceDocument(std::move(document));
    const ModelElement *contact = controller.document().findByName("contact_infectivity");
    const ModelElement *succumbing = controller.document().findByName("succumbing");
    QVERIFY(contact && succumbing);
    ArrowItem *contactArrow = nullptr;
    for (const ViewElement &element : controller.document().diagram().elements()) {
        if (element.kind() != ElementKind::Connector)
            continue;
        const auto &connector = static_cast<const Connector &>(element);
        if (connector.from() == contact->id() && connector.to() == succumbing->id())
            contactArrow = dynamic_cast<ArrowItem *>(scene.item(connector.id()));
    }
    QVERIFY(contactArrow);
    DiagramItem *contactItem = scene.item(contact->id());
    contactItem->setSelected(true);
    DiagramView &view = *window.findChild<DiagramView *>();
    const QList<QDockWidget *> docks = window.findChildren<QDockWidget *>();
    for (QDockWidget *dock : docks)
        dock->hide();
    window.resize(1000, 800);
    view.scale(0.66, 0.66);
    view.centerOn(scene.itemsBoundingRect().center());
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTest::qWait(20);
    const QImage initial = viewportImage(view);
    const QPointF initialPosition = contactItem->pos();
    auto sendMouse = [&view](QEvent::Type type, const QPoint &point, Qt::MouseButton button, Qt::MouseButtons buttons) {
        QMouseEvent event(type, point, view.viewport()->mapToGlobal(point), button, buttons, Qt::NoModifier);
        QApplication::sendEvent(view.viewport(), &event);
    };
    const QPointF destination = contactItem->center() + QPointF(0.0, 400.0);
    sendMouse(QEvent::MouseButtonPress, view.mapFromScene(contactItem->center()), Qt::LeftButton, Qt::LeftButton);
    for (int step = 1; step <= 4; ++step) {
        sendMouse(QEvent::MouseMove, view.mapFromScene(initialPosition + QPointF(0.0, step * 100.0)), Qt::NoButton, Qt::LeftButton);
        QTest::qWait(5);
    }
    sendMouse(QEvent::MouseButtonRelease, view.mapFromScene(destination), Qt::LeftButton, Qt::NoButton);
    QVERIFY(contactItem->pos().y() > initialPosition.y() + 300.0);
    const QRectF movedArrowPaintBounds = contactArrow->geometryPath().boundingRect().adjusted(-4.0, -4.0, 4.0, 4.0);
    PaintRegionRecorder recorder;
    view.viewport()->installEventFilter(&recorder);
    QTest::qWait(20);
    controller.undoStack()->undo();
    QTest::qWait(20);
    QCOMPARE(viewportImage(view), initial);
    if (QGuiApplication::platformName() == QStringLiteral("windows")) {
        const QRect movedViewBounds = view.mapFromScene(movedArrowPaintBounds).boundingRect();
        QVERIFY2(recorder.region.contains(movedViewBounds),
            qPrintable(QStringLiteral("moved=%1,%2 %3x%4 viewport=%5x%6")
                .arg(movedViewBounds.x()).arg(movedViewBounds.y()).arg(movedViewBounds.width()).arg(movedViewBounds.height())
                .arg(view.viewport()->width()).arg(view.viewport()->height())));
    }
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
    QCOMPARE(static_cast<const Flow *>(controller.document().find(flow))->endpoints().from, stock);
    controller.undoStack()->redo();
    QVERIFY(scene.item(flow) == nullptr);
}

void TestEditor::routeUndoRedo()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    const EntityId stock = controller.createVariable(ElementKind::Stock, QPointF());
    const EntityId auxiliary = controller.createVariable(ElementKind::Variable, QPointF(100.0, 0.0));
    const EntityId id = controller.createConnector(stock, auxiliary);
    const Route defaultRoute = defaultConnectorRoute();
    Route route;
    route.manual = true;
    route.controlPoints = {{0.5, 50.0}};
    controller.setRoute(id, route);
    QCOMPARE(controller.document().diagram().route(id), route);
    controller.undoStack()->undo();
    QCOMPARE(controller.document().diagram().route(id), defaultRoute);
    controller.undoStack()->redo();
    QCOMPARE(controller.document().diagram().route(id), route);
}

void TestEditor::replaceDocumentResetsScene()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    controller.createVariable(ElementKind::Stock, QPointF());
    Document replacement;
    const EntityId id = replacement.createVariable(ElementKind::Variable, {30.0, 40.0});
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
    qds::io::Workspace workspace;
    QString error;
    QVERIFY2(qds::io::DocumentFile::open(path, loaded, workspace, &error), qPrintable(error));

    DocumentController controller;
    DiagramScene scene(&controller);
    controller.replaceDocument(std::move(loaded));
    int modelCount = 0;
    for (const ModelElement &element : controller.document().model().elements()) {
        ++modelCount;
        QVERIFY2(scene.item(element.id()), qPrintable((element.displayName())));
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
    const EntityId auxiliary = controller.createVariable(ElementKind::Variable, QPointF(100.0, 0.0));
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
    const EntityId auxiliary = controller.createVariable(ElementKind::Variable, QPointF(100.0, 0.0));
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
    const EntityId stock = controller.createVariable(ElementKind::Stock, QPointF(100.0, 100.0));
    DiagramView view(&scene);
    view.zoomIn();
    QVERIFY(view.transform().m11() > 1.0);
    view.resetZoom();
    QCOMPARE(view.transform().m11(), 1.0);
    view.fitAll();
    QVERIFY(view.transform().m11() > 0.0);

    view.resize(640, 480);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    view.resetZoom();
    view.centerOn(scene.item(stock)->center());
    view.setFocus();
    QTest::keyPress(&view, Qt::Key_Space);
    QVERIFY(scene.spacePressed());
    const QPoint pressed = view.viewport()->rect().center();
    const int scrollX = view.horizontalScrollBar()->value();
    const int scrollY = view.verticalScrollBar()->value();
    QMouseEvent panPress(QEvent::MouseButtonPress, pressed, view.viewport()->mapToGlobal(pressed),
                         Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &panPress);
    QVERIFY(scene.spacePressed());
    const QPoint moved = pressed + QPoint(40, 30);
    QMouseEvent panMove(QEvent::MouseMove, moved, view.viewport()->mapToGlobal(moved),
                        Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &panMove);
    QVERIFY(view.horizontalScrollBar()->value() != scrollX);
    QVERIFY(view.verticalScrollBar()->value() != scrollY);
    QMouseEvent panRelease(QEvent::MouseButtonRelease, moved, view.viewport()->mapToGlobal(moved),
                           Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &panRelease);
    QTest::keyRelease(&view, Qt::Key_Space);
    QVERIFY(!scene.spacePressed());

    QTest::keyPress(&view, Qt::Key_Space);
    QVERIFY(scene.spacePressed());
    QFocusEvent focusOut(QEvent::FocusOut, Qt::ActiveWindowFocusReason);
    QApplication::sendEvent(&view, &focusOut);
    QVERIFY(!scene.spacePressed());
    QTest::keyPress(&view, Qt::Key_Space);
    QVERIFY(scene.spacePressed());
    QFocusEvent focusIn(QEvent::FocusIn, Qt::ActiveWindowFocusReason);
    QApplication::sendEvent(&view, &focusIn);
    QVERIFY(!scene.spacePressed());
    QTest::keyRelease(&view, Qt::Key_Space);
}

void TestEditor::cloudShapeAndBoundary()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    const EntityId stock = controller.createVariable(ElementKind::Stock, QPointF(-240.0, 0.0));
    const EntityId cloudId = controller.createCloud(QPointF());
    auto *cloud = dynamic_cast<ViewObjectItem *>(scene.item(cloudId));
    QVERIFY(cloud);
    const QPainterPath outline = cloud->shape();
    QVERIFY(outline.elementCount() > 2);
    QPainterPath unscaled;
    unscaled.moveTo(-35.0, 12.0);
    unscaled.cubicTo(-48.0, 12.0, -50.0, -4.0, -39.0, -10.0);
    unscaled.cubicTo(-40.0, -24.0, -20.0, -28.0, -10.0, -17.0);
    unscaled.cubicTo(0.0, -30.0, 20.0, -26.0, 25.0, -14.0);
    unscaled.cubicTo(41.0, -20.0, 52.0, -5.0, 43.0, 6.0);
    unscaled.cubicTo(48.0, 20.0, 25.0, 27.0, 12.0, 19.0);
    unscaled.cubicTo(-5.0, 28.0, -30.0, 25.0, -35.0, 12.0);
    unscaled.closeSubpath();
    QCOMPARE(outline.elementCount(), unscaled.elementCount());
    for (int index = 0; index < outline.elementCount(); ++index) {
        const auto actual = outline.elementAt(index);
        const auto original = unscaled.elementAt(index);
        QCOMPARE(actual.type, original.type);
        QVERIFY(std::abs(actual.x - original.x * 0.4) < 1e-9);
        QVERIFY(std::abs(actual.y - original.y * 0.4) < 1e-9);
    }
    QVERIFY(cloud->boundingRect().width() < 50.0);
    QVERIFY(outline.contains(QPointF()));
    QVERIFY(outline.contains(QPointF(-15.0, 0.0)));
    QVERIFY(!outline.contains(QPointF(-45.0, 0.0)));
    const QList<QPolygonF> polygons = outline.toSubpathPolygons();
    QCOMPARE(polygons.size(), 1);
    QVERIFY(polygons.front().size() > 8);
    QCOMPARE(polygons.front().first(), polygons.front().last());

    for (const QPointF &target : {QPointF(300.0, 0.0), QPointF(0.0, 300.0), QPointF(-300.0, 0.0),
                                  QPointF(0.0, -300.0), QPointF(240.0, 180.0), QPointF(-240.0, 180.0),
                                  QPointF(240.0, -180.0), QPointF(-240.0, -180.0)}) {
        const QPointF boundary = cloud->boundaryPointToward(target) - cloud->center();
        double nearest = std::numeric_limits<double>::max();
        for (int index = 1; index < polygons.front().size(); ++index) {
            const QPointF start = polygons.front()[index - 1];
            const QPointF segment = polygons.front()[index] - start;
            const double lengthSquared = QPointF::dotProduct(segment, segment);
            if (lengthSquared < 1e-12)
                continue;
            const double ratio = std::clamp(QPointF::dotProduct(boundary - start, segment) / lengthSquared, 0.0, 1.0);
            nearest = std::min(nearest, QLineF(boundary, start + ratio * segment).length());
        }
        QVERIFY2(nearest < 0.75, qPrintable(QStringLiteral("target=%1,%2 nearest=%3")
            .arg(target.x()).arg(target.y()).arg(nearest)));
        QVERIFY(std::abs(target.x() * boundary.y() - target.y() * boundary.x()) < 1e-6);
    }

    const EntityId flow = controller.createFlow(stock, cloudId);
    const QPainterPath flowPath = dynamic_cast<FlowItem *>(scene.item(flow))->geometryPath();
    QVERIFY(QLineF(flowPath.pointAtPercent(1.0), cloud->boundaryPointToward(scene.item(stock)->center())).length() < 0.75);
    const QImage rendered = scene.renderImage();
    QVERIFY(!rendered.isNull());
}

void TestEditor::routeModesAndGeometry()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    const EntityId stock = controller.createVariable(ElementKind::Stock, QPointF(0.0, 0.0));
    const EntityId auxiliary = controller.createVariable(ElementKind::Variable, QPointF(240.0, 0.0));
    const EntityId id = controller.createConnector(stock, auxiliary);
    const Route defaultRoute = defaultConnectorRoute();
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
    QCOMPARE(item->controlGuides().size(), 2);
    QCOMPARE(controller.document().diagram().route(id).mode, RouteMode::Bezier);
    scene.setSelectedRouteMode(RouteMode::Straight);
    QVERIFY(controller.document().diagram().route(id).controlPoints.empty());
    scene.resetSelectedRoute();
    QCOMPARE(controller.document().diagram().route(id), defaultRoute);
    scene.toggleSelectedRouteLock();
    QVERIFY(controller.document().diagram().route(id).locked);
    controller.undoStack()->undo();
    QVERIFY(!controller.document().diagram().route(id).locked);
}

void TestEditor::bezierControlGeometry()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    const EntityId stock = controller.createVariable(ElementKind::Stock, QPointF());
    const EntityId auxiliary = controller.createVariable(ElementKind::Variable, QPointF(240.0, 0.0));
    const EntityId connector = controller.createConnector(stock, auxiliary);
    auto *arrow = dynamic_cast<ArrowItem *>(scene.item(connector));
    arrow->setSelected(true);

    Route route = defaultConnectorRoute();
    route.manual = true;
    const auto defaultPoints = route.controlPoints;
    for (int controlCount : {1, 2}) {
        route.controlPoints = defaultPoints;
        route.controlPoints.resize(controlCount);
        controller.setRoute(connector, route);
        const QPointF source = scene.item(stock)->center();
        const QPointF target = scene.item(auxiliary)->center();
        const QPointF firstControl = arrow->scenePoint(route.controlPoints.front());
        const QPointF lastControl = arrow->scenePoint(route.controlPoints.back());
        const QVector<QLineF> guides = arrow->controlGuides();
        QCOMPARE(guides.size(), 2);
        QCOMPARE(guides[0].p1(), source);
        QCOMPARE(guides[0].p2(), firstControl);
        QCOMPARE(guides[1].p1(), lastControl);
        QCOMPARE(guides[1].p2(), target);

        QPainterPath original;
        original.moveTo(source);
        if (controlCount == 1)
            original.quadTo(firstControl, target);
        else
            original.cubicTo(firstControl, lastControl, target);
        const QPolygonF originalPolygon = original.toSubpathPolygons().front();
        const QPolygonF visiblePolygon = arrow->geometryPath().toSubpathPolygons().front();
        QVERIFY(visiblePolygon.size() > 2);
        for (const QPointF &point : visiblePolygon) {
            double nearest = std::numeric_limits<double>::max();
            for (int index = 1; index < originalPolygon.size(); ++index) {
                const QPointF start = originalPolygon[index - 1];
                const QPointF segment = originalPolygon[index] - start;
                const double lengthSquared = QPointF::dotProduct(segment, segment);
                if (lengthSquared < 1e-12)
                    continue;
                const double ratio = std::clamp(QPointF::dotProduct(point - start, segment) / lengthSquared, 0.0, 1.0);
                nearest = std::min(nearest, QLineF(point, start + ratio * segment).length());
            }
            QVERIFY2(nearest < 0.75, qPrintable(QStringLiteral("controls=%1 nearest=%2").arg(controlCount).arg(nearest)));
        }
        const auto distanceToOutline = [](const DiagramItem *item, const QPointF &scenePoint) {
            const QPointF point = item->mapFromScene(scenePoint);
            const QPolygonF polygon = item->shape().toSubpathPolygons().front();
            double nearest = std::numeric_limits<double>::max();
            for (int index = 1; index < polygon.size(); ++index) {
                const QPointF start = polygon[index - 1];
                const QPointF segment = polygon[index] - start;
                const double lengthSquared = QPointF::dotProduct(segment, segment);
                if (lengthSquared < 1e-12)
                    continue;
                const double ratio = std::clamp(QPointF::dotProduct(point - start, segment) / lengthSquared, 0.0, 1.0);
                nearest = std::min(nearest, QLineF(point, start + ratio * segment).length());
            }
            return nearest;
        };
        QVERIFY(distanceToOutline(scene.item(stock), visiblePolygon.first()) < 0.75);
        QVERIFY(distanceToOutline(scene.item(auxiliary), visiblePolygon.last()) < 0.75);
        QVERIFY(QLineF(visiblePolygon.first(), source).length() > 1.0);
        QVERIFY(QLineF(visiblePolygon.last(), target).length() > 1.0);
    }
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
    const EntityId shortCloud = shortController.createCloud(QPointF(70.0, 0.0));
    const EntityId shortFlow = shortController.createFlow(shortStock, shortCloud);
    QVERIFY(dynamic_cast<FlowItem *>(shortScene.item(shortFlow))->linePaths().isEmpty());

    DocumentController controller;
    DiagramScene scene(&controller);
    const EntityId detached = controller.createFlow(0, 0, QPointF(0.0, 0.0), QPointF(200.0, 0.0));
    QVERIFY(detached != 0);
    const auto *blankFlow = static_cast<const Flow *>(controller.document().find(detached));
    QCOMPARE(controller.document().find(blankFlow->endpoints().from)->kind(), ElementKind::Cloud);
    QCOMPARE(controller.document().find(blankFlow->endpoints().to)->kind(), ElementKind::Cloud);
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

void TestEditor::flowDefaultAndSelectionColors()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    scene.setGridVisible(false);
    const EntityId stock = controller.createVariable(ElementKind::Stock, QPointF(0.0, 0.0));
    const EntityId cloud = controller.createCloud(QPointF(400.0, 0.0));
    const EntityId flow = controller.createFlow(stock, cloud);
    auto *item = dynamic_cast<FlowItem *>(scene.item(flow));
    QVERIFY(item);

    DiagramView view(&scene);
    view.resize(640, 480);
    view.centerOn(200.0, 0.0);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(20);
    const QImage normal = view.grab().toImage();
    QVERIFY(itemColorCount(normal, view, item, diagram_palette::EntityOutlineColor) > 0);
    QCOMPARE(itemColorCount(normal, view, item, diagram_palette::SelectionColor), 0);

    item->setSelected(true);
    QTest::qWait(20);
    const QImage selected = view.grab().toImage();
    QVERIFY(itemColorCount(selected, view, item, diagram_palette::SelectionColor) > 0);

    item->setSelected(false);
    QTest::qWait(20);
    const QImage restored = view.grab().toImage();
    QVERIFY(itemColorCount(restored, view, item, diagram_palette::EntityOutlineColor) > 0);
    QCOMPARE(itemColorCount(restored, view, item, diagram_palette::SelectionColor), 0);
}

void TestEditor::bezierControlGuideRendering()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    scene.setGridVisible(false);
    const EntityId stock = controller.createVariable(ElementKind::Stock, QPointF(0.0, 0.0));
    const EntityId auxiliary = controller.createVariable(ElementKind::Variable, QPointF(300.0, 0.0));
    const EntityId id = controller.createConnector(stock, auxiliary);
    auto *item = dynamic_cast<ArrowItem *>(scene.item(id));
    item->setSelected(true);
    QVERIFY(item->zValue() > scene.item(stock)->zValue());
    Route route;
    route.mode = RouteMode::Bezier;
    route.manual = true;
    route.controlPoints = {{1.0 / 3.0, -100.0}, {2.0 / 3.0, -100.0}};
    controller.setRoute(id, route);
    for (const QLineF &guide : item->controlGuides()) {
        QVERIFY(item->boundingRect().contains(guide.p1()));
        QVERIFY(item->boundingRect().contains(guide.p2()));
    }

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
    int exteriorSamples = 0;
    for (const QPoint &sample : samples) {
        if (scene.item(stock)->containsScenePoint(view.mapToScene(sample)))
            continue;
        QCOMPARE(deselected.pixelColor(sample), background);
        ++exteriorSamples;
    }
    QVERIFY(exteriorSamples > 0);

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

void TestEditor::controlPointMouseRepaint()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    scene.setGridVisible(false);
    const EntityId susceptible = controller.createVariable(ElementKind::Stock, QPointF(-60.0, 160.0));
    const EntityId infectious = controller.createVariable(ElementKind::Stock, QPointF(240.0, 180.0));
    const EntityId flow = controller.createFlow(susceptible, infectious);
    const EntityId connector = controller.createConnector(infectious, flow);
    Route route = controller.document().diagram().route(connector);
    route.manual = true;
    route.controlPoints = {{0.407079646017699, 15.9645625257057}, {0.442477876106195, 95.7873751542345}};
    controller.setRoute(connector, route);
    auto *arrow = dynamic_cast<ArrowItem *>(scene.item(connector));
    arrow->setSelected(true);

    ControlPointHandle *handle = nullptr;
    for (QGraphicsItem *item : scene.items()) {
        auto *candidate = qgraphicsitem_cast<ControlPointHandle *>(item);
        if (candidate && candidate->routeId() == connector && candidate->index() == 0)
            handle = candidate;
    }
    QVERIFY(handle);

    DiagramView view(&scene);
    view.resize(640, 480);
    view.centerOn(120.0, 80.0);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(20);
    const QLineF oldGuide = arrow->controlGuides().front();
    QVector<QPoint> samples;
    for (int index = 4; index <= 16; ++index)
        samples.append(view.mapFromScene(oldGuide.pointAt(index / 20.0)));
    const QImage before = view.grab().toImage();
    const QColor background(250, 250, 250);
    int painted = 0;
    for (const QPoint &sample : samples)
        painted += before.pixelColor(sample) != background ? 1 : 0;
    QVERIFY(painted > 0);

    const QPoint start = view.mapFromScene(handle->pos());
    const QPoint end = view.mapFromScene(handle->pos() + QPointF(-100.0, -150.0));
    QCOMPARE(view.itemAt(start), handle);
    QMouseEvent press(QEvent::MouseButtonPress, start, view.viewport()->mapToGlobal(start), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &press);
    QCOMPARE(scene.mouseGrabberItem(), handle);
    QMouseEvent move(QEvent::MouseMove, end, view.viewport()->mapToGlobal(end), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &move);
    const QImage dragged = view.grab().toImage();
    view.viewport()->repaint();
    const QImage cleanlyRepainted = view.grab().toImage();
    QCOMPARE(dragged, cleanlyRepainted);

    QMouseEvent release(QEvent::MouseButtonRelease, end, view.viewport()->mapToGlobal(end), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &release);
    QTest::qWait(20);
    const QImage after = view.grab().toImage();
    view.viewport()->repaint();
    const QImage afterRepaint = view.grab().toImage();
    QCOMPARE(after, afterRepaint);
}

void TestEditor::automaticControlPointMouseEdit()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    const EntityId stock = controller.createVariable(ElementKind::Stock, QPointF(0.0, 0.0));
    const EntityId auxiliary = controller.createVariable(ElementKind::Variable, QPointF(300.0, 0.0));
    const EntityId connector = controller.createConnector(stock, auxiliary);
    auto *arrow = dynamic_cast<ArrowItem *>(scene.item(connector));
    Route route = defaultConnectorRoute();
    route.controlPoints = {{0.5, -50.0, true}, {0.5, -50.0, true}};
    controller.setRoute(connector, route);
    arrow->setSelected(true);

    DiagramView view(&scene);
    view.resize(640, 480);
    view.centerOn(150.0, 0.0);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(20);
    const QPointF overlap = arrow->scenePoint(route.controlPoints.front());
    const QPoint start = view.mapFromScene(overlap);
    auto *topHandle = qgraphicsitem_cast<ControlPointHandle *>(view.itemAt(start));
    QVERIFY(topHandle && topHandle->role() == ControlPointHandle::Role::ControlPoint);

    const QPoint end = view.mapFromScene(overlap + QPointF(80.0, 80.0));
    QMouseEvent press(QEvent::MouseButtonPress, start, view.viewport()->mapToGlobal(start), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &press);
    QCOMPARE(scene.mouseGrabberItem(), topHandle);
    QMouseEvent move(QEvent::MouseMove, end, view.viewport()->mapToGlobal(end), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &move);
    QMouseEvent release(QEvent::MouseButtonRelease, end, view.viewport()->mapToGlobal(end), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &release);

    const Route edited = controller.document().diagram().route(connector);
    QVERIFY(edited.manual);
    for (const RoutePoint &point : edited.controlPoints)
        QVERIFY(!point.automatic);
    QCOMPARE(edited.controlPoints.size(), std::size_t(2));
    QVERIFY(QLineF(arrow->scenePoint(edited.controlPoints[0]), arrow->scenePoint(edited.controlPoints[1])).length() > 1.0);

    const int remainingIndex = topHandle->index() == 0 ? 1 : 0;
    ControlPointHandle *remainingHandle = nullptr;
    for (QGraphicsItem *item : scene.items()) {
        auto *handle = qgraphicsitem_cast<ControlPointHandle *>(item);
        if (handle && handle->isVisible() && handle->routeId() == connector && handle->index() == remainingIndex)
            remainingHandle = handle;
    }
    QVERIFY(remainingHandle);
    QCOMPARE(view.itemAt(view.mapFromScene(remainingHandle->pos())), remainingHandle);
}

void TestEditor::controlPointDoubleClick()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    const EntityId stock = controller.createVariable(ElementKind::Stock, QPointF(0.0, 0.0));
    const EntityId auxiliary = controller.createVariable(ElementKind::Variable, QPointF(240.0, 0.0));
    const EntityId id = controller.createConnector(stock, auxiliary);
    controller.setRoute(id, Route());
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
    const EntityId auxiliary = controller.createVariable(ElementKind::Variable, QPointF(200.0, 0.0));
    const EntityId id = controller.createConnector(first, auxiliary);
    QVERIFY(controller.reconnectConnector(id, second, auxiliary));
    QCOMPARE(dynamic_cast<ArrowItem *>(scene.item(id))->from(), scene.item(second));
    controller.undoStack()->undo();
    QCOMPARE(dynamic_cast<ArrowItem *>(scene.item(id))->from(), scene.item(first));
    QVERIFY(!controller.reconnectConnector(id, 9999, auxiliary));

    const EntityId cloud = controller.createCloud(QPointF(400.0, 0.0));
    const EntityId flow = controller.createFlow(first, cloud);
    QVERIFY(controller.reconnectFlow(flow, second, cloud));
    QCOMPARE(static_cast<const Flow *>(controller.document().find(flow))->endpoints().from, second);
    controller.undoStack()->undo();
    QCOMPARE(static_cast<const Flow *>(controller.document().find(flow))->endpoints().from, first);
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

    const ModelElement *infectious = controller.document().findByName("infectious");
    QVERIFY(infectious);
    struct DependentPath {
        ArrowItem *arrow;
        FlowItem *flow;
        QPainterPath oldPath;
        QPointF oldCenter;
    };
    QVector<DependentPath> dependentPaths;
    for (const ViewElement &element : controller.document().diagram().elements()) {
        if (element.kind() != ElementKind::Connector)
            continue;
        auto *arrow = dynamic_cast<ArrowItem *>(scene.item(element.id()));
        auto *flow = dynamic_cast<FlowItem *>(arrow->from());
        if (!flow)
            flow = dynamic_cast<FlowItem *>(arrow->to());
        if (flow && flow->connects(infectious->id()))
            dependentPaths.push_back({arrow, flow, arrow->geometryPath(), flow->center()});
    }
    QVERIFY(!dependentPaths.empty());

    DiagramView view(&scene);
    view.resize(900, 700);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    view.fitInView(scene.itemsBoundingRect().adjusted(-40.0, -40.0, 40.0, 40.0), Qt::KeepAspectRatio);
    QTest::qWait(20);
    const Vec2 infectiousPosition = controller.document().diagram().position(infectious->id());
    controller.moveElements({{infectious->id(), QPointF(infectiousPosition.x + 80.0, infectiousPosition.y + 60.0)}});
    QTest::qWait(20);

    for (const DependentPath &dependent : dependentPaths) {
        QVERIFY(QLineF(dependent.flow->center(), dependent.oldCenter).length() > 1.0);
        const QPainterPath path = dependent.arrow->geometryPath();
        QVERIFY(path.elementCount() != dependent.oldPath.elementCount()
            || path.pointAtPercent(0.5) != dependent.oldPath.pointAtPercent(0.5));
        const bool flowIsSource = dependent.arrow->from() == dependent.flow;
        const int endpointIndex = flowIsSource ? 0 : path.elementCount() - 1;
        const int neighborIndex = flowIsSource ? 1 : path.elementCount() - 2;
        QVERIFY(endpointIndex >= 0 && neighborIndex >= 0);
        const QPointF endpoint(path.elementAt(endpointIndex));
        const QPointF localEndpoint = endpoint - dependent.flow->center();
        const bool onValveBoundary = std::abs(localEndpoint.x()) <= 60.1 && std::abs(localEndpoint.y()) <= 25.1
            && (std::abs(std::abs(localEndpoint.x()) - 60.0) < 0.75
                || std::abs(std::abs(localEndpoint.y()) - 25.0) < 0.75);
        QVERIFY(onValveBoundary || QLineF(endpoint, dependent.flow->center()).length() < 0.75);
        QVERIFY(QLineF(endpoint, dependent.flow->center()).length() <= 65.1);
    }

    controller.undoStack()->undo();
    for (const DependentPath &dependent : dependentPaths) {
        QCOMPARE(dependent.flow->center(), dependent.oldCenter);
        QCOMPARE(dependent.arrow->geometryPath().pointAtPercent(0.5), dependent.oldPath.pointAtPercent(0.5));
    }
    controller.undoStack()->redo();
    for (const DependentPath &dependent : dependentPaths) {
        QVERIFY(dependent.flow->center() != dependent.oldCenter);
        QVERIFY(dependent.arrow->geometryPath().pointAtPercent(0.5) != dependent.oldPath.pointAtPercent(0.5));
    }
}

void TestEditor::endpointHandleReconnect()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    const EntityId source = controller.createVariable(ElementKind::Variable, QPointF(0.0, 0.0));
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
    QVERIFY2(QLineF(targetHandle->pos(), destination).length() < 0.75,
        qPrintable(QStringLiteral("handle=%1,%2 destination=%3,%4")
            .arg(targetHandle->pos().x()).arg(targetHandle->pos().y())
            .arg(destination.x()).arg(destination.y())));
    QVERIFY(arrow->previewing());
    QVERIFY(scene.item(targetStock)->isEndpointTargetHighlighted());
    QVERIFY(!scene.item(targetStock)->isSelected());
    QVERIFY(!scene.item(flow)->isEndpointTargetHighlighted());
    QVERIFY(arrow->controlGuides().isEmpty());
    for (QGraphicsItem *item : scene.items()) {
        const auto *handle = qgraphicsitem_cast<ControlPointHandle *>(item);
        if (handle && handle->routeId() == connector && handle->role() == ControlPointHandle::Role::ControlPoint)
            QVERIFY(!handle->isVisible());
    }
    const QImage endpointDragged = view.grab().toImage();
    view.viewport()->repaint();
    const QImage cleanlyRepainted = view.grab().toImage();
    QCOMPARE(endpointDragged, cleanlyRepainted);
    QMouseEvent release(QEvent::MouseButtonRelease, end, view.viewport()->mapToGlobal(end), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &release);
    QVERIFY(!arrow->previewing());
    QVERIFY(!scene.item(targetStock)->isEndpointTargetHighlighted());
    QTest::qWait(20);
    const QImage reconnected = view.grab().toImage();
    view.viewport()->repaint();
    const QImage reconnectedRepaint = view.grab().toImage();
    QCOMPARE(reconnected, reconnectedRepaint);

    QCOMPARE(static_cast<const Connector *>(controller.document().find(connector))->to(), targetStock);
    controller.undoStack()->undo();
    QCOMPARE(static_cast<const Connector *>(controller.document().find(connector))->to(), flow);

    view.centerOn(targetHandle);
    const QPoint invalidStart = view.mapFromScene(targetHandle->pos());
    const QPoint invalidEnd = view.mapFromScene(QPointF(-250.0, -250.0));
    QCOMPARE(view.itemAt(invalidStart), targetHandle);
    QMouseEvent invalidPress(QEvent::MouseButtonPress, invalidStart, view.viewport()->mapToGlobal(invalidStart), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &invalidPress);
    QMouseEvent invalidMove(QEvent::MouseMove, invalidEnd, view.viewport()->mapToGlobal(invalidEnd), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &invalidMove);
    QVERIFY(!scene.item(targetStock)->isEndpointTargetHighlighted());
    QVERIFY(!scene.item(flow)->isEndpointTargetHighlighted());
    QMouseEvent invalidRelease(QEvent::MouseButtonRelease, invalidEnd, view.viewport()->mapToGlobal(invalidEnd), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &invalidRelease);
    QVERIFY(!arrow->previewing());
    QVERIFY(!scene.item(targetStock)->isEndpointTargetHighlighted());
    QVERIFY(!scene.item(flow)->isEndpointTargetHighlighted());
    QCOMPARE(static_cast<const Connector *>(controller.document().find(connector))->to(), flow);
    int restoredControlHandles = 0;
    for (QGraphicsItem *item : scene.items()) {
        const auto *handle = qgraphicsitem_cast<ControlPointHandle *>(item);
        if (handle && handle->isVisible() && handle->routeId() == connector
            && handle->role() == ControlPointHandle::Role::ControlPoint)
            ++restoredControlHandles;
    }
    QCOMPARE(restoredControlHandles, 2);
    QTest::qWait(20);
    const QImage cancelled = view.grab().toImage();
    view.viewport()->repaint();
    const QImage cancelledRepaint = view.grab().toImage();
    QCOMPARE(cancelled, cancelledRepaint);

    controller.undoStack()->redo();
    QCOMPARE(static_cast<const Connector *>(controller.document().find(connector))->to(), targetStock);
    view.centerOn(targetHandle);
    const QPoint flowStart = view.mapFromScene(targetHandle->pos());
    const QPoint flowEnd = view.mapFromScene(scene.item(flow)->center());
    QCOMPARE(view.itemAt(flowStart), targetHandle);
    QMouseEvent flowPress(QEvent::MouseButtonPress, flowStart, view.viewport()->mapToGlobal(flowStart), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &flowPress);
    QMouseEvent flowMove(QEvent::MouseMove, flowEnd, view.viewport()->mapToGlobal(flowEnd), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &flowMove);
    QVERIFY(scene.item(flow)->isEndpointTargetHighlighted());
    QVERIFY(!scene.item(flow)->isSelected());
    QVERIFY(!scene.item(targetStock)->isEndpointTargetHighlighted());
    QMouseEvent flowRelease(QEvent::MouseButtonRelease, flowEnd, view.viewport()->mapToGlobal(flowEnd), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &flowRelease);
    QVERIFY(!scene.item(flow)->isEndpointTargetHighlighted());
    QCOMPARE(static_cast<const Connector *>(controller.document().find(connector))->to(), flow);
    controller.undoStack()->undo();
    QCOMPARE(static_cast<const Connector *>(controller.document().find(connector))->to(), targetStock);
    controller.undoStack()->redo();
    QCOMPARE(static_cast<const Connector *>(controller.document().find(connector))->to(), flow);
}

void TestEditor::flowEndpointPreviewDependentsRepaint()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    scene.setGridVisible(false);
    const EntityId sourceStock = controller.createVariable(ElementKind::Stock, QPointF());
    const EntityId oldTarget = controller.createVariable(ElementKind::Stock, QPointF(400.0, 0.0));
    const EntityId newTarget = controller.createVariable(ElementKind::Stock, QPointF(400.0, 400.0));
    const EntityId flow = controller.createFlow(sourceStock, oldTarget);
    const EntityId auxiliary = controller.createVariable(ElementKind::Variable, QPointF(-400.0, 400.0));
    const EntityId connector = controller.createConnector(auxiliary, flow);
    auto *flowItem = dynamic_cast<FlowItem *>(scene.item(flow));
    auto *arrow = dynamic_cast<ArrowItem *>(scene.item(connector));
    flowItem->setSelected(true);

    ControlPointHandle *targetHandle = nullptr;
    for (QGraphicsItem *item : scene.items()) {
        auto *handle = qgraphicsitem_cast<ControlPointHandle *>(item);
        if (handle && handle->routeId() == flow && handle->index() == -2)
            targetHandle = handle;
    }
    QVERIFY(targetHandle);
    DiagramView view(&scene);
    view.resize(900, 800);
    view.centerOn(QPointF(0.0, 200.0));
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    const QPainterPath oldArrowPath = arrow->geometryPath();
    const QRectF oldArrowPaintBounds = oldArrowPath.boundingRect().adjusted(-4.0, -4.0, 4.0, 4.0);
    const QPoint start = view.mapFromScene(targetHandle->pos());
    const QPoint end = view.mapFromScene(scene.item(newTarget)->center());
    QCOMPARE(view.itemAt(start), targetHandle);
    QSignalSpy changedSpy(&scene, &QGraphicsScene::changed);
    QMouseEvent press(QEvent::MouseButtonPress, start, view.viewport()->mapToGlobal(start), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &press);
    QMouseEvent move(QEvent::MouseMove, end, view.viewport()->mapToGlobal(end), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &move);
    QVERIFY(flowItem->previewing());
    QTest::qWait(20);
    QRectF invalidated;
    for (const auto &notification : changedSpy) {
        for (const QRectF &rect : notification.at(0).value<QList<QRectF>>())
            invalidated = invalidated.united(rect);
    }
    QVERIFY2(invalidated.contains(oldArrowPaintBounds), qPrintable(QStringLiteral("invalidated=%1,%2 %3x%4 oldArrow=%5,%6 %7x%8")
        .arg(invalidated.x()).arg(invalidated.y()).arg(invalidated.width()).arg(invalidated.height())
        .arg(oldArrowPaintBounds.x()).arg(oldArrowPaintBounds.y()).arg(oldArrowPaintBounds.width()).arg(oldArrowPaintBounds.height())));
    const QRectF newArrowPaintBounds = arrow->geometryPath().boundingRect().adjusted(-4.0, -4.0, 4.0, 4.0);
    QVERIFY(invalidated.contains(newArrowPaintBounds));
    const QImage preview = view.grab().toImage();
    view.viewport()->repaint();
    QCOMPARE(preview, view.grab().toImage());
    QVERIFY(arrow->geometryPath().pointAtPercent(0.5) != oldArrowPath.pointAtPercent(0.5));
    const QPainterPath previewArrowPath = arrow->geometryPath();
    const QPointF actualEnd = previewArrowPath.pointAtPercent(1.0);
    const QPointF localEnd = actualEnd - flowItem->center();
    QVERIFY2(std::abs(localEnd.x()) <= 60.1 && std::abs(localEnd.y()) <= 25.1
            && (std::abs(std::abs(localEnd.x()) - 60.0) < 0.75 || std::abs(std::abs(localEnd.y()) - 25.0) < 0.75),
        qPrintable(QStringLiteral("actual=%1,%2 flowCenter=%3,%4")
            .arg(actualEnd.x()).arg(actualEnd.y()).arg(flowItem->center().x()).arg(flowItem->center().y())));

    QMouseEvent release(QEvent::MouseButtonRelease, end, view.viewport()->mapToGlobal(end), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &release);
    QVERIFY(!flowItem->previewing());
    QCOMPARE(static_cast<const Flow *>(controller.document().find(flow))->endpoints().to, newTarget);
    QTest::qWait(20);
    const QImage reconnected = view.grab().toImage();
    view.viewport()->repaint();
    QCOMPARE(reconnected, view.grab().toImage());
}

void TestEditor::connectorEndpointCrossingRepaint()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    scene.setGridVisible(false);
    const EntityId source = controller.createVariable(ElementKind::Variable, QPointF(-250.0, -250.0));
    const EntityId stock = controller.createVariable(ElementKind::Stock, QPointF());
    const EntityId newTarget = controller.createVariable(ElementKind::Stock, QPointF(250.0, 250.0));
    const EntityId connector = controller.createConnector(source, stock);
    auto *arrow = dynamic_cast<ArrowItem *>(scene.item(connector));
    arrow->setSelected(true);

    ControlPointHandle *targetHandle = nullptr;
    for (QGraphicsItem *item : scene.items()) {
        auto *handle = qgraphicsitem_cast<ControlPointHandle *>(item);
        if (handle && handle->routeId() == connector && handle->index() == -2)
            targetHandle = handle;
    }
    QVERIFY(targetHandle);
    DiagramView view(&scene);
    view.resize(800, 800);
    view.centerOn(QPointF());
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    const QPoint start = view.mapFromScene(targetHandle->pos());
    const QPoint end = view.mapFromScene(scene.item(newTarget)->center());
    QCOMPARE(view.itemAt(start), targetHandle);
    QMouseEvent press(QEvent::MouseButtonPress, start, view.viewport()->mapToGlobal(start), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &press);
    QMouseEvent move(QEvent::MouseMove, end, view.viewport()->mapToGlobal(end), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &move);
    QVERIFY(arrow->previewing());
    QTest::qWait(20);
    const QRectF previewBounds = arrow->sceneBoundingRect();
    QSignalSpy changedSpy(&scene, &QGraphicsScene::changed);

    QMouseEvent release(QEvent::MouseButtonRelease, end, view.viewport()->mapToGlobal(end), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &release);
    QVERIFY(!arrow->previewing());
    QCOMPARE(static_cast<const Connector *>(controller.document().find(connector))->to(), newTarget);
    QTest::qWait(20);
    QRectF invalidated;
    for (const auto &notification : changedSpy) {
        for (const QRectF &rect : notification.at(0).value<QList<QRectF>>())
            invalidated = invalidated.united(rect);
    }
    QVERIFY2(invalidated.contains(previewBounds), qPrintable(QStringLiteral("invalidated=%1,%2 %3x%4 preview=%5,%6 %7x%8")
        .arg(invalidated.x()).arg(invalidated.y()).arg(invalidated.width()).arg(invalidated.height())
        .arg(previewBounds.x()).arg(previewBounds.y()).arg(previewBounds.width()).arg(previewBounds.height())));
    const QImage restored = view.grab().toImage();
    view.viewport()->repaint();
    QCOMPARE(restored, view.grab().toImage());
}

void TestEditor::endpointTargetHighlight()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    scene.setGridVisible(false);
    const EntityId source = controller.createVariable(ElementKind::Stock, QPointF());
    const EntityId first = controller.createVariable(ElementKind::Variable, QPointF(300.0, 0.0));
    const EntityId second = controller.createVariable(ElementKind::Stock, QPointF(300.0, 300.0));
    DiagramItem *sourceItem = scene.item(source);
    DiagramItem *firstItem = scene.item(first);
    DiagramItem *secondItem = scene.item(second);

    DiagramView view(&scene);
    view.resize(900, 700);
    view.centerOn(QPointF(150.0, 100.0));
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    scene.setTool(DiagramScene::Tool::Connect);
    auto moveTo = [&view](const QPointF &scenePosition) {
        const QPoint point = view.mapFromScene(scenePosition);
        QMouseEvent move(QEvent::MouseMove, point, view.viewport()->mapToGlobal(point), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(view.viewport(), &move);
    };

    const QPoint start = view.mapFromScene(sourceItem->center());
    QMouseEvent press(QEvent::MouseButtonPress, start, view.viewport()->mapToGlobal(start), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &press);
    moveTo(firstItem->center());
    QVERIFY(firstItem->isEndpointTargetHighlighted());
    QVERIFY(!firstItem->isSelected());
    QVERIFY(sourceItem->isSelected());
    QVERIFY(!sourceItem->isEndpointTargetHighlighted());
    QVERIFY(!secondItem->isEndpointTargetHighlighted());
    QTest::qWait(20);
    const QImage highlighted = view.grab().toImage();
    QVERIFY(itemColorCount(highlighted, view, firstItem, diagram_palette::EndpointTargetColor) > 0);

    moveTo(secondItem->center());
    QVERIFY(!firstItem->isEndpointTargetHighlighted());
    QVERIFY(secondItem->isEndpointTargetHighlighted());
    QVERIFY(!secondItem->isSelected());
    moveTo(QPointF(100.0, 250.0));
    QVERIFY(!firstItem->isEndpointTargetHighlighted());
    QVERIFY(!secondItem->isEndpointTargetHighlighted());
    QTest::qWait(20);
    const QImage cleared = view.grab().toImage();
    view.viewport()->repaint();
    QCOMPARE(cleared, view.grab().toImage());

    moveTo(firstItem->center());
    const QPoint end = view.mapFromScene(firstItem->center());
    QMouseEvent release(QEvent::MouseButtonRelease, end, view.viewport()->mapToGlobal(end), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &release);
    QVERIFY(!firstItem->isEndpointTargetHighlighted());
    QVERIFY(!secondItem->isEndpointTargetHighlighted());

    scene.clearSelection();
    firstItem->setSelected(true);
    QTest::qWait(20);
    const QImage selected = view.grab().toImage();
    QCOMPARE(itemColorCount(selected, view, firstItem, diagram_palette::EndpointTargetColor), 0);
    QVERIFY(itemColorCount(selected, view, firstItem, diagram_palette::SelectionColor) > 0);
}

void TestEditor::connectorMoveRepaint()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    scene.setGridVisible(false);
    const EntityId stock = controller.createVariable(ElementKind::Stock, QPointF(0.0, 0.0));
    const EntityId cloud = controller.createCloud(QPointF(200.0, 0.0));
    const EntityId flow = controller.createFlow(stock, cloud);
    const EntityId auxiliary = controller.createVariable(ElementKind::Variable, QPointF(0.0, 300.0));
    const EntityId connector = controller.createConnector(auxiliary, flow);
    auto *arrow = dynamic_cast<ArrowItem *>(scene.item(connector));
    auto *flowItem = dynamic_cast<FlowItem *>(scene.item(flow));
    DiagramView view(&scene);
    view.resize(640, 480);
    view.centerOn(100.0, 100.0);
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

    const QPointF oldFlowCenter = flowItem->center();
    controller.moveElements({{stock, QPointF(-200.0, -200.0)}});
    QTest::qWait(20);
    QVERIFY(QLineF(flowItem->center(), oldFlowCenter).length() > 1.0);
    const QImage after = view.grab().toImage();
    for (const QPoint &sample : samples) {
        QVERIFY2(after.pixelColor(sample) == background,
            qPrintable(QStringLiteral("sample=%1,%2 color=%3").arg(sample.x()).arg(sample.y())
                .arg(after.pixelColor(sample).name())));
    }
}

void TestEditor::connectorDragSelectionRepaint()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    scene.setGridVisible(false);
    const EntityId susceptible = controller.createVariable(ElementKind::Stock, QPointF(-60.0, 160.0));
    const EntityId infectious = controller.createVariable(ElementKind::Stock, QPointF(240.0, 180.0));
    const EntityId flow = controller.createFlow(susceptible, infectious);
    const EntityId connector = controller.createConnector(infectious, flow);
    auto *arrow = dynamic_cast<ArrowItem *>(scene.item(connector));
    Route route = defaultConnectorRoute();
    route.manual = true;
    route.controlPoints = {{0.407079646017699, 15.9645625257057}, {0.442477876106195, 95.7873751542345}};
    controller.setRoute(connector, route);
    arrow->setSelected(true);
    QCOMPARE(arrow->controlGuides().size(), 2);

    DiagramView view(&scene);
    view.resize(640, 480);
    view.centerOn(120.0, 80.0);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::qWait(20);
    const QPoint start = view.mapFromScene(arrow->geometryPath().pointAtPercent(0.5));
    const QPoint middle = start + QPoint(140.0, 100.0);
    const QPoint end = start + QPoint(-100.0, 70.0);
    QMouseEvent press(QEvent::MouseButtonPress, start, view.viewport()->mapToGlobal(start), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &press);
    QMouseEvent move(QEvent::MouseMove, middle, view.viewport()->mapToGlobal(middle), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &move);
    QTest::qWait(20);
    const QImage dragged = view.grab().toImage();
    view.viewport()->repaint();
    const QImage cleanlyRepainted = view.grab().toImage();
    QCOMPARE(dragged, cleanlyRepainted);
    QVERIFY(arrow->controlGuides().isEmpty());
    for (QGraphicsItem *item : scene.items()) {
        const auto *handle = qgraphicsitem_cast<ControlPointHandle *>(item);
        if (handle && handle->routeId() == connector)
            QVERIFY(!handle->isVisible());
    }

    QMouseEvent reverseMove(QEvent::MouseMove, end, view.viewport()->mapToGlobal(end), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &reverseMove);
    QTest::qWait(20);
    QMouseEvent release(QEvent::MouseButtonRelease, end, view.viewport()->mapToGlobal(end), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &release);
    QTest::qWait(20);
    const QImage after = view.grab().toImage();
    view.viewport()->repaint();
    const QImage afterRepaint = view.grab().toImage();
    QCOMPARE(after, afterRepaint);
}

void TestEditor::rubberBandReverseRepaint()
{
    MainWindow window;
    auto *scene = window.findChild<DiagramScene *>();
    auto *viewPointer = window.findChild<DiagramView *>();
    QVERIFY(scene && viewPointer);
    DiagramView &view = *viewPointer;
    scene->setGridVisible(false);
    for (QDockWidget *dock : window.findChildren<QDockWidget *>())
        dock->hide();
    view.setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view.setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    window.resize(1000, 800);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTest::qWait(20);
    const QImage before = view.grab().toImage();
    PaintRegionRecorder recorder;
    view.viewport()->installEventFilter(&recorder);
    auto *selectionFrame = view.findChild<QFrame *>(QStringLiteral("selectionFrame"));
    QVERIFY(selectionFrame);
    auto sendMouse = [&view](QEvent::Type type, const QPoint &point, Qt::MouseButton button, Qt::MouseButtons buttons) {
        QMouseEvent event(type, point, view.viewport()->mapToGlobal(point), button, buttons, Qt::NoModifier);
        QApplication::sendEvent(view.viewport(), &event);
    };
    sendMouse(QEvent::MouseButtonPress, QPoint(400, 300), Qt::LeftButton, Qt::LeftButton);
    sendMouse(QEvent::MouseMove, QPoint(100, 100), Qt::NoButton, Qt::LeftButton);
    const QRect expandedBand = selectionFrame->geometry();
    QVERIFY(!expandedBand.isEmpty());
    QTest::qWait(20);
    const QImage expandedImage = view.grab().toImage();
    int selectionPixels = 0;
    const QRect expandedArea = expandedBand.intersected(expandedImage.rect());
    for (int y = expandedArea.top(); y <= expandedArea.bottom(); ++y) {
        for (int x = expandedArea.left(); x <= expandedArea.right(); ++x)
            selectionPixels += expandedImage.pixelColor(x, y) == diagram_palette::SelectionColor ? 1 : 0;
    }
    if (QGuiApplication::platformName() == QStringLiteral("windows"))
        QVERIFY(selectionPixels > 0);
    recorder.region = QRegion();
    sendMouse(QEvent::MouseMove, QPoint(200, 200), Qt::NoButton, Qt::LeftButton);
    QTest::qWait(20);
    QVERIFY(selectionFrame->isVisible());
    QCOMPARE(selectionFrame->geometry(), QRect(200, 200, 201, 101));
    if (QGuiApplication::platformName() == QStringLiteral("windows"))
        QVERIFY(recorder.region.contains(expandedBand));
    const QImage reversed = view.grab().toImage();
    view.viewport()->repaint();
    QCOMPARE(reversed, view.grab().toImage());
    sendMouse(QEvent::MouseButtonRelease, QPoint(200, 200), Qt::LeftButton, Qt::NoButton);
    QVERIFY(!selectionFrame->isVisible());
    const QImage after = view.grab().toImage();
    QCOMPARE(after, before);
}

void TestEditor::automaticAndRelativeRoutePoints()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    const EntityId stock = controller.createVariable(ElementKind::Stock, QPointF(0.0, 0.0));
    const EntityId auxiliary = controller.createVariable(ElementKind::Variable, QPointF(400.0, 0.0));
    const EntityId connector = controller.createConnector(stock, auxiliary);
    auto *defaultArrow = dynamic_cast<ArrowItem *>(scene.item(connector));
    defaultArrow->setSelected(true);
    QCOMPARE(defaultArrow->controlGuides().size(), 2);
    int automaticHandles = 0;
    for (QGraphicsItem *item : scene.items()) {
        const auto *handle = qgraphicsitem_cast<ControlPointHandle *>(item);
        if (handle && handle->isVisible() && handle->routeId() == connector
            && handle->role() == ControlPointHandle::Role::ControlPoint)
            ++automaticHandles;
    }
    QCOMPARE(automaticHandles, 2);
    const Route cShape = controller.document().diagram().route(connector);
    QCOMPARE(cShape.controlPoints.size(), std::size_t(2));
    QVERIFY(cShape.controlPoints[0].relative.y * cShape.controlPoints[1].relative.y > 0.0);
    for (QGraphicsItem *item : scene.items()) {
        auto *handle = qgraphicsitem_cast<ControlPointHandle *>(item);
        if (handle && handle->isVisible() && handle->routeId() == connector && handle->index() == 0) {
            handle->setPos(handle->pos() + QPointF(10.0, 10.0));
            break;
        }
    }
    for (const RoutePoint &point : defaultArrow->route().controlPoints)
        QVERIFY(!point.automatic);
    controller.setRoute(connector, cShape);
    defaultArrow->setSelected(false);

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
    const EntityId auxiliary = controller.createVariable(ElementKind::Variable, QPointF(100.0, 0.0));
    controller.setInitialEquation(stock, QStringLiteral("100"));
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
    QCOMPARE(palette.count(), 7);
    QCOMPARE(palette.item(1)->text(), QStringLiteral("Variable"));
    for (int index = 0; index < palette.count(); ++index) {
        if (index != 4)
            QVERIFY(!palette.item(index)->icon().isNull());
    }
    QVERIFY(!(palette.item(5)->flags() & Qt::ItemIsDragEnabled));
    QVERIFY(!(palette.item(6)->flags() & Qt::ItemIsDragEnabled));
    QVERIFY(palette.mimeData({palette.item(5)}) == nullptr);
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
    QCOMPARE(scene.selectedModelElement()->kind(), ElementKind::Variable);
    delete mime;
}

void TestEditor::rubberConnectionAndSelection()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    const EntityId source = controller.createVariable(ElementKind::Stock, QPointF(0.0, 0.0));
    const EntityId target = controller.createVariable(ElementKind::Variable, QPointF(240.0, 0.0));

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
    QCOMPARE(static_cast<const Flow *>(document.find(flow))->endpoints().from, source);
    QCOMPARE(static_cast<const Flow *>(document.find(flow))->endpoints().to, cloud);

    scene.setTool(DiagramScene::Tool::AddFlow);
    press.setScenePos(QPointF(600.0, 100.0));
    QApplication::sendEvent(&scene, &press);
    move.setScenePos(QPointF(700.0, 100.0));
    QApplication::sendEvent(&scene, &move);
    release.setScenePos(QPointF(800.0, 100.0));
    QApplication::sendEvent(&scene, &release);
    const EntityId blankFlow = document.findByName("Flow2")->id();
    const FlowEndpoints blankEndpoints = static_cast<const Flow *>(document.find(blankFlow))->endpoints();
    QCOMPARE(document.find(blankEndpoints.from)->kind(), ElementKind::Cloud);
    QCOMPARE(document.find(blankEndpoints.to)->kind(), ElementKind::Cloud);
    QCOMPARE(document.diagram().position(blankEndpoints.from), Vec2({600.0, 100.0}));
    QCOMPARE(document.diagram().position(blankEndpoints.to), Vec2({800.0, 100.0}));
    controller.undoStack()->undo();
    QVERIFY(!document.find(blankFlow) && !document.find(blankEndpoints.from) && !document.find(blankEndpoints.to));
    QVERIFY(!scene.item(blankFlow) && !scene.item(blankEndpoints.from) && !scene.item(blankEndpoints.to));
    controller.undoStack()->redo();
    QVERIFY(scene.item(blankFlow) && scene.item(blankEndpoints.from) && scene.item(blankEndpoints.to));
    QCOMPARE(static_cast<const Flow *>(document.find(blankFlow))->endpoints(), blankEndpoints);

    const auto beforeArrow = std::distance(document.elements().begin(), document.elements().end());
    scene.setTool(DiagramScene::Tool::Connect);
    press.setScenePos(QPointF(1000.0, 300.0));
    QApplication::sendEvent(&scene, &press);
    release.setScenePos(QPointF(1100.0, 300.0));
    QApplication::sendEvent(&scene, &release);
    QCOMPARE(std::distance(document.elements().begin(), document.elements().end()), beforeArrow);
}

void TestEditor::connectToolTargetsFlow()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    const EntityId source = controller.createVariable(ElementKind::Variable, QPointF(0.0, -200.0));
    const EntityId stock = controller.createVariable(ElementKind::Stock, QPointF(0.0, 0.0));
    const EntityId cloud = controller.createCloud(QPointF(400.0, 0.0));
    const EntityId flow = controller.createFlow(stock, cloud);
    auto *flowItem = dynamic_cast<FlowItem *>(scene.item(flow));
    QVERIFY(flowItem);

    DiagramView view(&scene);
    view.resize(800, 600);
    view.centerOn(QPointF(100.0, 0.0));
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    scene.setTool(DiagramScene::Tool::Connect);

    const QPoint start = view.mapFromScene(scene.item(source)->center());
    const QPoint end = view.mapFromScene(flowItem->center());
    QMouseEvent press(QEvent::MouseButtonPress, start, view.viewport()->mapToGlobal(start), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &press);
    QMouseEvent move(QEvent::MouseMove, end, view.viewport()->mapToGlobal(end), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &move);
    QVERIFY(flowItem->isEndpointTargetHighlighted());
    QVERIFY(!flowItem->isSelected());
    QTest::qWait(20);
    const QImage highlighted = view.grab().toImage();
    QVERIFY(itemColorCount(highlighted, view, flowItem, diagram_palette::EndpointTargetColor) > 0);
    QMouseEvent release(QEvent::MouseButtonRelease, end, view.viewport()->mapToGlobal(end), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &release);
    QVERIFY(!flowItem->isEndpointTargetHighlighted());

    const QVector<EntityId> selection = scene.selectedElementIds();
    QCOMPARE(selection.size(), std::size_t(1));
    const EntityId connector = selection.front();
    const auto *created = dynamic_cast<const Connector *>(controller.document().find(connector));
    QVERIFY(created);
    QCOMPARE(created->from(), source);
    QCOMPARE(created->to(), flow);
    const auto *arrow = dynamic_cast<ArrowItem *>(scene.item(connector));
    QVERIFY(arrow);
    QCOMPARE(arrow->from(), scene.item(source));
    QCOMPARE(arrow->to(), flowItem);

    controller.undoStack()->undo();
    QVERIFY(!controller.document().find(connector));
    QVERIFY(!scene.item(connector));
    controller.undoStack()->redo();
    created = dynamic_cast<const Connector *>(controller.document().find(connector));
    QVERIFY(created);
    QCOMPARE(created->from(), source);
    QCOMPARE(created->to(), flow);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QString error;
    const QString nativePath = directory.filePath(QStringLiteral("connector-to-flow.qds"));
    QVERIFY2(qds::io::DocumentFile::save(nativePath, controller.document(), qds::io::Workspace {}, &error), qPrintable(error));
    Document nativeLoaded;
    qds::io::Workspace workspace;
    QVERIFY2(qds::io::DocumentFile::open(nativePath, nativeLoaded, workspace, &error), qPrintable(error));
    const auto *nativeConnector = dynamic_cast<const Connector *>(nativeLoaded.find(connector));
    QVERIFY(nativeConnector);
    QCOMPARE(nativeConnector->from(), source);
    QCOMPARE(nativeConnector->to(), flow);

    const QString xmilePath = directory.filePath(QStringLiteral("connector-to-flow.xmile"));
    QVERIFY2(qds::io::XmileFile::exportFile(xmilePath, controller.document(), &error), qPrintable(error));
    Document xmileLoaded;
    QVERIFY2(qds::io::XmileFile::importFile(xmilePath, xmileLoaded, &error), qPrintable(error));
    const ModelElement *xmileSource = xmileLoaded.findByName("Variable1");
    const ModelElement *xmileFlow = xmileLoaded.findByName("Flow1");
    QVERIFY(xmileSource && xmileFlow);
    int matchingConnectors = 0;
    for (const DocumentElement &element : xmileLoaded.elements()) {
        if (element.kind() != ElementKind::Connector)
            continue;
        const auto &candidate = static_cast<const Connector &>(element);
        if (candidate.from() == xmileSource->id() && candidate.to() == xmileFlow->id())
            ++matchingConnectors;
    }
    QCOMPARE(matchingConnectors, 1);

    const EntityId arrowTarget = controller.createVariable(ElementKind::Variable, QPointF(400.0, -200.0));
    const EntityId existingArrow = controller.createConnector(source, arrowTarget);
    QVERIFY(existingArrow != 0);
    const QPointF arrowMiddle = dynamic_cast<ArrowItem *>(scene.item(existingArrow))->geometryPath().pointAtPercent(0.5);
    const auto beforeInvalidTarget = std::distance(controller.document().elements().begin(), controller.document().elements().end());
    const QPoint invalidStart = view.mapFromScene(scene.item(stock)->center());
    const QPoint invalidEnd = view.mapFromScene(arrowMiddle);
    QMouseEvent invalidPress(QEvent::MouseButtonPress, invalidStart, view.viewport()->mapToGlobal(invalidStart), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &invalidPress);
    QMouseEvent invalidMove(QEvent::MouseMove, invalidEnd, view.viewport()->mapToGlobal(invalidEnd), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &invalidMove);
    QMouseEvent invalidRelease(QEvent::MouseButtonRelease, invalidEnd, view.viewport()->mapToGlobal(invalidEnd), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &invalidRelease);
    QCOMPARE(std::distance(controller.document().elements().begin(), controller.document().elements().end()), beforeInvalidTarget);
}

void TestEditor::connectionContextMenu()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    const EntityId stock = controller.createVariable(ElementKind::Stock, QPointF(0.0, 0.0));
    const EntityId auxiliary = controller.createVariable(ElementKind::Variable, QPointF(240.0, 0.0));
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
        QAction *straightAction = nullptr;
        for (QAction *action : actions) {
            state->valid = state->valid && action->isCheckable() && !action->icon().isNull();
            if (action->text() == QStringLiteral("Bezier"))
                state->valid = state->valid && action->isChecked();
            if (action->text() == QStringLiteral("Straight"))
                straightAction = action;
        }
        straightAction->trigger();
        menu->close();
    });

    QGraphicsSceneContextMenuEvent event(QEvent::GraphicsSceneContextMenu);
    event.setReason(QGraphicsSceneContextMenuEvent::Mouse);
    event.setScenePos(dynamic_cast<ArrowItem *>(scene.item(id))->geometryPath().pointAtPercent(0.5));
    event.setScreenPos(QPoint(100, 100));
    QApplication::sendEvent(&scene, &event);
    QVERIFY(state->executed);
    QVERIFY(state->valid);
    QCOMPARE(controller.document().diagram().route(id).mode, RouteMode::Straight);
}

void TestEditor::orthogonalArrowDirections()
{
    auto arrowFor = [](const QPointF &sourcePosition) {
        DocumentController controller;
        DiagramScene scene(&controller);
        const EntityId source = controller.createVariable(ElementKind::Variable, sourcePosition);
        const EntityId target = controller.createVariable(ElementKind::Stock, QPointF());
        const EntityId id = controller.createConnector(source, target);
        Route route;
        route.mode = RouteMode::Orthogonal;
        route.manual = true;
        controller.setRoute(id, route);
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
    const bool horizontal = std::abs(direction(diagonal).y()) == 0.0
        && std::abs(direction(diagonal).x()) > 0.0;
    const bool vertical = std::abs(direction(diagonal).x()) == 0.0
        && std::abs(direction(diagonal).y()) > 0.0;
    QVERIFY(horizontal || vertical);
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
    QCOMPARE(toolBars.size(), 3);
    for (QToolBar *toolBar : toolBars) {
        for (QAction *action : toolBar->actions()) {
            if (!action->isSeparator() && !action->text().isEmpty())
                QVERIFY2(!action->icon().isNull(), qPrintable(action->text()));
        }
    }
    const EntityPalette *palette = window.findChild<EntityPalette *>();
    QVERIFY(palette);
    for (int index = 0; index < palette->count(); ++index) {
        if (index != 4)
            QVERIFY(!palette->item(index)->icon().isNull());
    }
}

void TestEditor::unifiedIdUndoRedo()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    const EntityId stock = controller.createVariable(ElementKind::Stock, QPointF());
    const EntityId cloud = controller.createCloud(QPointF(200.0, 0.0));
    const EntityId auxiliary = controller.createVariable(ElementKind::Variable, QPointF(100.0, 100.0));
    const EntityId flow = controller.createFlow(stock, cloud);
    const EntityId connector = controller.createConnector(auxiliary, flow);
    QCOMPARE(QVector<EntityId>({stock, cloud, auxiliary, flow, connector}), QVector<EntityId>({1, 2, 3, 4, 5}));
    controller.removeElements({stock});
    QVERIFY(!scene.item(flow) && !scene.item(connector));
    controller.undoStack()->undo();
    QVERIFY(scene.item(stock) && scene.item(flow) && scene.item(connector));
    QCOMPARE(static_cast<const Flow *>(controller.document().find(flow))->endpoints().from, stock);
    QCOMPARE(static_cast<const Connector *>(controller.document().find(connector))->to(), flow);
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
    const QVector<EntityId> selected = scene.selectedElementIds();
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
    const QVector<EntityId> secondSelection = scene.selectedElementIds();
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
    QVERIFY2(qds::io::DocumentFile::save(path, document, qds::io::Workspace {}, &error), qPrintable(error));

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
    QVERIFY(window.windowTitle().startsWith(QStringLiteral("\u62D6\u653E\u6A21\u578B[*]")));
    QVERIFY(!window.windowTitle().contains(QStringLiteral(".qds")));
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
    QVERIFY(window.windowTitle().startsWith(QStringLiteral("plain[*]")));
    QCOMPARE(scene->findElements(QStringLiteral("Plain")).size(), std::size_t(1));

    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
    QTimer::singleShot(0, &window, [&window]() {
        auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
        QVERIFY(dialog);
        QCOMPARE(QFileInfo(dialog->selectedFiles().value(0)).fileName(), QStringLiteral("plain.qds"));
        dialog->reject();
    });
    window.findChild<QAction *>(QStringLiteral("saveAsAction"))->trigger();
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, false);

    QMimeData invalid;
    invalid.setUrls({QUrl::fromLocalFile(directory.filePath(QStringLiteral("not-model.txt")))});
    QDragEnterEvent rejected(QPoint(20, 20), Qt::CopyAction, &invalid, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view->viewport(), &rejected);
    QVERIFY(!rejected.isAccepted());
}

void TestEditor::mdlOpenAndDrop()
{
    MainWindow opened;
    QVERIFY(opened.openFile(QStringLiteral(QDS_TEACUP_MDL_PATH)));
    QVERIFY(opened.windowTitle().startsWith(QStringLiteral("teacup[*]")));
    QVERIFY(!opened.windowTitle().contains(QStringLiteral(".mdl")));
    auto *openedScene = opened.findChild<DiagramScene *>();
    QVERIFY(openedScene);
    QCOMPARE(openedScene->findElements(QStringLiteral("Teacup Temperature")).size(), std::size_t(1));
    QCOMPARE(openedScene->findElements(QStringLiteral("Heat Loss to Room")).size(), std::size_t(1));

    MainWindow dropped;
    auto *view = dropped.findChild<DiagramView *>();
    auto *scene = dropped.findChild<DiagramScene *>();
    QVERIFY(view && scene);
    QMimeData data;
    data.setUrls({QUrl::fromLocalFile(QStringLiteral(QDS_TEACUP_MDL_PATH))});
    QDragEnterEvent enter(QPoint(20, 20), Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view->viewport(), &enter);
    QVERIFY(enter.isAccepted());
    QDropEvent drop(QPoint(20, 20), Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view->viewport(), &drop);
    QVERIFY(drop.isAccepted());
    QVERIFY(dropped.windowTitle().startsWith(QStringLiteral("teacup[*]")));
    QCOMPARE(scene->findElements(QStringLiteral("Teacup Temperature")).size(), std::size_t(1));
}

void TestEditor::documentTitlesAndSaveNames()
{
    MainWindow first;
    MainWindow second;
    const QRegularExpression pattern(QStringLiteral("^Untitled (\\d+)\\[\\*\\]"));
    const QRegularExpressionMatch firstMatch = pattern.match(first.windowTitle());
    const QRegularExpressionMatch secondMatch = pattern.match(second.windowTitle());
    QVERIFY(firstMatch.hasMatch() && secondMatch.hasMatch());
    const int firstNumber = firstMatch.captured(1).toInt();
    QCOMPARE(secondMatch.captured(1).toInt(), firstNumber + 1);
    second.findChild<QAction *>(QStringLiteral("newAction"))->trigger();
    const QRegularExpressionMatch newMatch = pattern.match(second.windowTitle());
    QVERIFY(newMatch.hasMatch());
    QCOMPARE(newMatch.captured(1).toInt(), firstNumber + 2);

    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
    QTimer::singleShot(0, &second, [&second, firstNumber]() {
        auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
        QVERIFY(dialog);
        QCOMPARE(QFileInfo(dialog->selectedFiles().value(0)).fileName(),
                 QStringLiteral("Untitled %1.qds").arg(firstNumber + 2));
        dialog->reject();
    });
    second.findChild<QAction *>(QStringLiteral("saveAsAction"))->trigger();
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, false);
}

void TestEditor::pngExportAndClipboard()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    const EntityId stock = controller.createVariable(ElementKind::Stock, QPointF());
    const EntityId auxiliary = controller.createVariable(ElementKind::Variable, QPointF(300.0, 0.0));
    const EntityId connector = controller.createConnector(stock, auxiliary);
    auto *arrow = dynamic_cast<ArrowItem *>(scene.item(connector));
    arrow->setSelected(true);
    auto visibleHandleCount = [&scene]() {
        int count = 0;
        for (QGraphicsItem *item : scene.items()) {
            const auto *handle = qgraphicsitem_cast<ControlPointHandle *>(item);
            if (handle && handle->isVisible())
                ++count;
        }
        return count;
    };
    const int handlesBefore = visibleHandleCount();
    QVERIFY(handlesBefore > 0);
    const QImage rendered = scene.renderImage();
    QVERIFY(!rendered.isNull());
    QCOMPARE(rendered.pixelColor(0, 0), diagram_palette::CanvasBackgroundColor);
    QCOMPARE(rendered.width(), qCeil(scene.contentRect().width() * 2.0));
    QCOMPARE(rendered.height(), qCeil(scene.contentRect().height() * 2.0));
    int selectionPixels = 0;
    int contentPixels = 0;
    for (int y = 0; y < rendered.height(); ++y) {
        for (int x = 0; x < rendered.width(); ++x) {
            const QColor color = rendered.pixelColor(x, y);
            selectionPixels += color == diagram_palette::SelectionColor ? 1 : 0;
            contentPixels += color != diagram_palette::CanvasBackgroundColor ? 1 : 0;
        }
    }
    QCOMPARE(selectionPixels, 0);
    QVERIFY(contentPixels > 0);
    QVERIFY(scene.gridVisible());
    QVERIFY(arrow->isSelected());
    QCOMPARE(visibleHandleCount(), handlesBefore);

    MainWindow window;
    auto *windowController = window.findChild<DocumentController *>();
    auto *windowScene = window.findChild<DiagramScene *>();
    QVERIFY(windowController && windowScene);
    const EntityId copiedStock = windowController->createVariable(ElementKind::Stock, QPointF());
    windowController->createVariable(ElementKind::Stock, QPointF(1000.0, 0.0));
    const EntityId overlappingAuxiliary = windowController->createVariable(ElementKind::Variable, QPointF(65.0, 35.0));
    windowScene->item(copiedStock)->setSelected(true);
    QVERIFY(window.findChild<QAction *>(QStringLiteral("exportPngAction")));
    QVERIFY(window.findChild<QAction *>(QStringLiteral("copyPngAction")));
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("diagram.png"));
    QVERIFY(window.exportPng(path));
    QImageReader reader(path);
    QCOMPARE(reader.format(), QByteArray("png"));
    const QImage exported = reader.read();
    QVERIFY(!exported.isNull());
    QCOMPARE(exported.size(), windowScene->renderImage().size());

    QMimeData previousClipboard;
    const QMimeData *clipboardData = QApplication::clipboard()->mimeData();
    if (clipboardData) {
        for (const QString &format : clipboardData->formats())
            previousClipboard.setData(format, clipboardData->data(format));
    }
    window.copyPng();
    const QImage copied = QApplication::clipboard()->image();
    QVERIFY(!copied.isNull());
    QCOMPARE(copied.size(), windowScene->renderImage(true).size());
    QVERIFY(copied.width() < windowScene->renderImage().width());
    const auto imagePoint = [](const QImage &image, const QRectF &bounds, const QPointF &scenePosition) {
        const QPointF local = scenePosition - bounds.topLeft();
        return QPoint(qFloor(local.x() * image.width() / bounds.width()),
                      qFloor(local.y() * image.height() / bounds.height()));
    };
    const QPointF overlappingCenter = windowScene->item(overlappingAuxiliary)->center();
    QCOMPARE(copied.pixelColor(imagePoint(copied, windowScene->contentRect(true), overlappingCenter)),
             diagram_palette::CanvasBackgroundColor);
    QVERIFY(exported.pixelColor(imagePoint(exported, windowScene->contentRect(), overlappingCenter))
            != diagram_palette::CanvasBackgroundColor);
    windowScene->clearSelection();
    window.copyPng();
    QCOMPARE(QApplication::clipboard()->image(), copied);
    auto *restoreClipboard = new QMimeData;
    for (const QString &format : previousClipboard.formats())
        restoreClipboard->setData(format, previousClipboard.data(format));
    QApplication::clipboard()->setMimeData(restoreClipboard);
}

void TestEditor::workspaceRestore()
{
    MainWindow window;
    auto *controller = window.findChild<DocumentController *>();
    auto *view = window.findChild<DiagramView *>();
    QVERIFY(controller && view);
    controller->createVariable(ElementKind::Stock, QPointF());
    view->zoomIn();
    view->centerOn(QPointF(123.5, 456.25));
    const double expectedZoom = view->transform().m11();
    const QPointF expectedCenter = view->mapToScene(view->viewport()->rect().center());
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("workspace.qds"));
    QVERIFY(window.saveFile(path));

    Document document;
    qds::io::Workspace workspace;
    QString error;
    QVERIFY2(qds::io::DocumentFile::open(path, document, workspace, &error), qPrintable(error));
    QVERIFY(workspace.hasViewport);
    QCOMPARE(workspace.zoom, expectedZoom);
    QVERIFY(qAbs(workspace.center.x - expectedCenter.x()) < 1e-9);
    QVERIFY(qAbs(workspace.center.y - expectedCenter.y()) < 1e-9);

    MainWindow restored;
    auto *restoredView = restored.findChild<DiagramView *>();
    QVERIFY(restoredView);
    QVERIFY(restored.openFile(path));
    QCOMPARE(restoredView->transform().m11(), expectedZoom);
    const QPointF center = restoredView->mapToScene(restoredView->viewport()->rect().center());
    QVERIFY2(QLineF(center, expectedCenter).length() < 0.001,
        qPrintable(QStringLiteral("actual=%1,%2 expected=%3,%4")
            .arg(center.x()).arg(center.y()).arg(expectedCenter.x()).arg(expectedCenter.y())));
    QVERIFY(restoredView->horizontalScrollBar()->value() == view->horizontalScrollBar()->value());
    QVERIFY(restoredView->verticalScrollBar()->value() == view->verticalScrollBar()->value());
}

void TestEditor::simulationControls()
{
    MainWindow window;
    auto *simulation = window.findChild<SimulationController *>();
    auto *runName = window.findChild<QComboBox *>("runNameCombo");
    auto *runAction = window.findChild<QAction *>("runAction");
    auto *cancelAction = window.findChild<QAction *>("cancelAction");
    QVERIFY(simulation && runName && runAction && cancelAction);
    QVERIFY(runName->isEditable());
    QCOMPARE(runName->currentText(), QStringLiteral("Run1"));
    QVERIFY(runAction->isEnabled());
    QVERIFY(!cancelAction->isEnabled());

    runName->setCurrentText("equilib");
    QTest::keyClick(runName->lineEdit(), Qt::Key_Return);
    QTRY_COMPARE(simulation->status(), SimulationSessionStatus::Completed);
    QTRY_VERIFY(!simulation->isBusy());
    QCOMPARE(simulation->lastRunName(), QStringLiteral("equilib"));
    QCOMPARE(simulation->runs().size(), 1);
    QVERIFY(runName->findText(QStringLiteral("equilib")) >= 0);
    QCOMPARE(runName->currentText(), QStringLiteral("equilib"));
    runName->setCurrentText(QStringLiteral("equilib"));
    QCOMPARE(runName->currentText(), QStringLiteral("equilib"));
    QVERIFY(runAction->isEnabled());
    QVERIFY(!cancelAction->isEnabled());

    runName->setCurrentText("growth");
    runAction->trigger();
    QTRY_COMPARE(simulation->status(), SimulationSessionStatus::Completed);
    QTRY_VERIFY(!simulation->isBusy());
    QCOMPARE(simulation->runs().size(), 2);
    QCOMPARE(runName->currentText(), QStringLiteral("growth"));
    runName->setCurrentText(QStringLiteral("growth"));

    QTimer::singleShot(0, &window, []() {
        if (auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
            message->button(QMessageBox::No)->click();
    });
    runAction->trigger();
    QApplication::processEvents();
    QCOMPARE(simulation->runs().size(), 2);

    const RunId previousId = simulation->runs().front().id();
    QTimer::singleShot(0, &window, []() {
        if (auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
            message->button(QMessageBox::Yes)->click();
    });
    runAction->trigger();
    QTRY_COMPARE(simulation->status(), SimulationSessionStatus::Completed);
    QTRY_VERIFY(!simulation->isBusy());
    QCOMPARE(simulation->runs().size(), 2);
    QVERIFY(simulation->runs().front().id() != previousId);

    auto *documents = window.findChild<DocumentController *>();
    QVERIFY(documents);
    QVERIFY(documents->setSimulationSettings({0.0, 100000.0, 1.0, 1.0}));
    runName->setCurrentText("long");
    runAction->trigger();
    QVERIFY(cancelAction->isEnabled());
    cancelAction->trigger();
    QTRY_COMPARE(simulation->status(), SimulationSessionStatus::Cancelled);
    QTRY_VERIFY(!simulation->isBusy());
    QVERIFY(runAction->isEnabled());
    QVERIFY(!cancelAction->isEnabled());

    documents->createVariable(ElementKind::Stock, QPointF());
    runName->setCurrentText("invalid");
    runAction->trigger();
    QCOMPARE(simulation->status(), SimulationSessionStatus::Failed);
    auto *issues = window.findChild<QListWidget *>();
    QVERIFY(issues && issues->count() > 0);
}

void TestEditor::resultsDock()
{
    MainWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto *controller = window.findChild<DocumentController *>();
    auto *simulation = window.findChild<SimulationController *>();
    auto *scene = window.findChild<DiagramScene *>();
    auto *dock = window.findChild<QDockWidget *>(QStringLiteral("resultsDock"));
    QVERIFY(controller && simulation && scene && dock);
    QVERIFY(!dock->isVisible());

    const EntityId stock = controller->createVariable(ElementKind::Stock, QPointF());
    const EntityId auxiliary = controller->createVariable(ElementKind::Variable, QPointF(200.0, 0.0));
    controller->renameElement(stock, QStringLiteral("Population"));
    controller->setInitialEquation(stock, QStringLiteral("1000"));
    controller->setUnit(stock, QStringLiteral("rabbit"));
    controller->renameElement(auxiliary, QStringLiteral("birth rate"));
    controller->setEquation(auxiliary, QStringLiteral("Population * 0.125"));
    controller->setUnit(auxiliary, QStringLiteral("fraction/Year"));
    const EntityId constant = controller->createVariable(ElementKind::Variable, QPointF(400.0, 0.0));
    controller->renameElement(constant, QStringLiteral("constant"));
    controller->setEquation(constant, QStringLiteral("5"));
    controller->setSimulationSettings({0.0, 1.0, 1.0, 1.0});
    scene->item(stock)->setSelected(true);
    QCOMPARE(simulation->workbenchVariable(), EntityId(0));

    QVERIFY(simulation->start(QStringLiteral("equilib")));
    QTRY_COMPARE(simulation->status(), SimulationSessionStatus::Completed);
    QTRY_VERIFY(!simulation->isBusy());
    QVERIFY(dock->isVisible());
    auto *variables = window.findChild<QComboBox *>(QStringLiteral("workbenchVariableCombo"));
    auto *runs = window.findChild<QComboBox *>(QStringLiteral("resultsRunCombo"));
    auto *datasets = window.findChild<QListWidget *>(QStringLiteral("datasetsList"));
    auto *table = window.findChild<QTableView *>(QStringLiteral("resultsTable"));
    auto *status = window.findChild<QLabel *>(QStringLiteral("runStatusLabel"));
    auto *model = qobject_cast<SimulationTableModel *>(table->model());
    auto *chart = window.findChild<TimeSeriesChartWidget *>();
    auto *tabs = window.findChild<QTabWidget *>();
    QVERIFY(variables && runs && datasets && table && status && model && chart && tabs);
    QCOMPARE(tabs->currentWidget(), chart);
    QCOMPARE(variables->currentData().toString(), QStringLiteral("population"));
    QCOMPARE(variables->findData(QStringLiteral("constant")), -1);
    QVERIFY(variables->findData(QStringLiteral("birth_rate")) < variables->findData(QStringLiteral("population")));
    QCOMPARE(runs->currentText(), QStringLiteral("equilib"));
    QCOMPARE(datasets->count(), 1);
    QCOMPARE(datasets->item(0)->checkState(), Qt::Checked);
    QCOMPARE(model->columnCount(), 2);
    QCOMPARE(model->rowCount(), simulation->runs().front().times().size());
    QCOMPARE(model->headerData(1, Qt::Horizontal).toString(), QStringLiteral("equilib"));
    const QModelIndex last = model->index(model->rowCount() - 1, 1);
    QCOMPARE(model->data(last).toString(), QStringLiteral("1000"));
    window.findChild<QAction *>(QStringLiteral("tableAction"))->trigger();
    table->scrollTo(last);
    QTRY_VERIFY(table->viewport()->rect().intersects(table->visualRect(last)));
    QCOMPARE(table->verticalScrollBar()->value(), table->verticalScrollBar()->maximum());
    QCOMPARE(chart->chart()->series().size(), 1);
    window.findChild<QAction *>(QStringLiteral("graphAction"))->trigger();
    QCOMPARE(tabs->currentWidget(), chart);
    window.findChild<QAction *>(QStringLiteral("tableAction"))->trigger();
    QVERIFY(tabs->currentWidget() != chart);
    window.findChild<QAction *>(QStringLiteral("graphAction"))->trigger();
    QVERIFY(!chart->grab().isNull());
    auto *timeAxis = qobject_cast<QtCharts::QValueAxis *>(chart->chart()->axes(Qt::Horizontal).front());
    auto *valueAxis = qobject_cast<QtCharts::QValueAxis *>(chart->chart()->axes(Qt::Vertical).front());
    QVERIFY(valueAxis->min() < 1000.0 && valueAxis->max() > 1000.0);
    const double initialMin = timeAxis->min();
    const double initialMax = timeAxis->max();
    const QPoint wheelPosition = chart->rect().center();
    QWheelEvent chartWheel(wheelPosition, chart->mapToGlobal(wheelPosition), QPoint(), QPoint(0, 120),
                           Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(chart->viewport(), &chartWheel);
    QTRY_VERIFY(timeAxis->max() - timeAxis->min() < initialMax - initialMin);
    const double zoomedMin = timeAxis->min();
    const QPoint panStart = chart->rect().center();
    const QPoint panEnd = panStart + QPoint(30, 20);
    QMouseEvent chartPress(QEvent::MouseButtonPress, panStart, chart->mapToGlobal(panStart), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(chart->viewport(), &chartPress);
    QMouseEvent chartMove(QEvent::MouseMove, panEnd, chart->mapToGlobal(panEnd), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(chart->viewport(), &chartMove);
    QMouseEvent chartRelease(QEvent::MouseButtonRelease, panEnd, chart->mapToGlobal(panEnd), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(chart->viewport(), &chartRelease);
    QVERIFY(timeAxis->min() != zoomedMin);
    QMouseEvent chartDoubleClick(QEvent::MouseButtonDblClick, panStart, chart->mapToGlobal(panStart), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(chart->viewport(), &chartDoubleClick);
    QApplication::sendEvent(chart->viewport(), &chartRelease);
    QCOMPARE(timeAxis->min(), initialMin);
    QCOMPARE(timeAxis->max(), initialMax);

    scene->clearSelection();
    scene->item(auxiliary)->setSelected(true);
    QCOMPARE(variables->currentData().toString(), QStringLiteral("population"));
    window.findChild<QAction *>(QStringLiteral("graphAction"))->trigger();
    QCOMPARE(variables->currentData().toString(), QStringLiteral("birth_rate"));
    QCOMPARE(model->headerData(1, Qt::Horizontal).toString(), QStringLiteral("equilib"));
    QCOMPARE(model->data(model->index(model->rowCount() - 1, 1)).toString(), QStringLiteral("125"));
    QCOMPARE(chart->chart()->series().size(), 1);
    QCOMPARE(chart->chart()->series().front()->name(), QStringLiteral("equilib"));
    scene->clearSelection();
    scene->item(constant)->setSelected(true);
    QCOMPARE(simulation->workbenchVariable(), auxiliary);
    window.findChild<QAction *>(QStringLiteral("graphAction"))->trigger();
    QVERIFY(scene->item(constant)->isSelected());
    QCOMPARE(simulation->workbenchVariable(), constant);
    QCOMPARE(variables->currentIndex(), -1);
    scene->clearSelection();
    scene->item(auxiliary)->setSelected(true);
    window.findChild<QAction *>(QStringLiteral("graphAction"))->trigger();
    QCOMPARE(variables->currentData().toString(), QStringLiteral("birth_rate"));
    auto *constantAxis = qobject_cast<QtCharts::QValueAxis *>(chart->chart()->axes(Qt::Vertical).front());
    QVERIFY(constantAxis->min() < 125.0 && constantAxis->max() > 125.0);
    const int populationIndex = variables->findData(QStringLiteral("population"));
    QVERIFY(populationIndex >= 0);
    variables->setCurrentIndex(populationIndex);
    QCOMPARE(simulation->workbenchVariable(), stock);
    QVERIFY(scene->item(auxiliary)->isSelected());
    const EntityId cloud = controller->createCloud(QPointF(600.0, 0.0));
    scene->clearSelection();
    scene->item(cloud)->setSelected(true);
    window.findChild<QAction *>(QStringLiteral("graphAction"))->trigger();
    QVERIFY(scene->item(cloud)->isSelected());
    QCOMPARE(simulation->workbenchVariable(), stock);

    controller->setEquation(auxiliary, QStringLiteral("Population * 0.25"));
    controller->setSimulationSettings({0.0, 1.0, 0.5, 0.5});
    QVERIFY(simulation->start(QStringLiteral("growth")));
    QTRY_COMPARE(simulation->status(), SimulationSessionStatus::Completed);
    QTRY_VERIFY(!simulation->isBusy());
    QCOMPARE(datasets->count(), 2);
    QCOMPARE(datasets->item(0)->text(), QStringLiteral("growth"));
    QCOMPARE(runs->currentText(), QStringLiteral("growth"));
    QCOMPARE(model->columnCount(), 3);
    QCOMPARE(model->headerData(1, Qt::Horizontal).toString(), QStringLiteral("growth"));
    QCOMPARE(model->headerData(2, Qt::Horizontal).toString(), QStringLiteral("equilib"));
    QCOMPARE(model->rowCount(), 3);
    QCOMPARE(model->data(model->index(1, 0)).toString(), QStringLiteral("0.5"));
    QVERIFY(model->data(model->index(1, 1)).isValid());
    QVERIFY(!model->data(model->index(1, 2)).isValid());
    QCOMPARE(chart->chart()->series().size(), 2);
    auto *firstSeries = qobject_cast<QtCharts::QLineSeries *>(chart->chart()->series().front());
    const auto markers = chart->chart()->legend()->markers(firstSeries);
    QVERIFY(!markers.isEmpty());
    QMetaObject::invokeMethod(markers.front(), "clicked");
    QVERIFY(!firstSeries->isVisible());
    QMetaObject::invokeMethod(markers.front(), "clicked");
    QVERIFY(firstSeries->isVisible());
    controller->renameElement(stock, QStringLiteral("Rabbit Population"));
    simulation->setWorkbenchVariableName(QStringLiteral("population"));
    datasets->item(1)->setCheckState(Qt::Unchecked);
    QVERIFY(!simulation->isRunLoaded(datasets->item(1)->data(Qt::UserRole).toULongLong()));
    QCOMPARE(simulation->runs().size(), 2);
    QCOMPARE(chart->chart()->series().size(), 1);
    QCOMPARE(model->columnCount(), 2);
    QCOMPARE(model->headerData(1, Qt::Horizontal).toString(), QStringLiteral("growth"));

    runs->setCurrentIndex(1);
    QCOMPARE(variables->currentText(), QStringLiteral("Population"));
    QVERIFY(status->text().contains(QStringLiteral("older model")));
    QCOMPARE(model->data(model->index(model->rowCount() - 1, 1)).toString(), QStringLiteral("1000"));
    QVERIFY(!model->data(QModelIndex()).isValid());

    controller->replaceDocument(Document());
    QVERIFY(simulation->runs().isEmpty());
    QCOMPARE(simulation->workbenchVariable(), EntityId(0));
    QVERIFY(!dock->isVisible());
    QCOMPARE(model->rowCount(), 0);
    QCOMPARE(chart->chart()->series().size(), 0);

    controller->createVariable(ElementKind::Stock, QPointF());
    controller->setInitialEquation(controller->document().findByName("Stock1")->id(), QStringLiteral("10"));
    QVERIFY(simulation->start(QStringLiteral("fallback")));
    QTRY_COMPARE(simulation->status(), SimulationSessionStatus::Completed);
    QTRY_VERIFY(!simulation->isBusy());
    QCOMPARE(variables->currentData().toString(), QStringLiteral("stock1"));
    QCOMPARE(model->rowCount(), simulation->runs().front().times().size());
    QCOMPARE(chart->chart()->series().size(), 1);
}

void TestEditor::causesStripGraph()
{
    MainWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto *controller = window.findChild<DocumentController *>();
    auto *simulation = window.findChild<SimulationController *>();
    auto *scene = window.findChild<DiagramScene *>();
    auto *strip = window.findChild<CausesStripWidget *>();
    QVERIFY(controller && simulation && scene && strip);

    window.findChild<QAction *>(QStringLiteral("causesStripAction"))->trigger();
    QVERIFY(strip->charts().isEmpty());

    const EntityId population = controller->createVariable(ElementKind::Stock, QPointF(0.0, 0.0));
    controller->renameElement(population, QStringLiteral("Population"));
    controller->setInitialEquation(population, QStringLiteral("1000"));
    const EntityId source = controller->createCloud(QPointF(-300.0, 0.0));
    const EntityId sink = controller->createCloud(QPointF(300.0, 0.0));
    const EntityId births = controller->createFlow(source, population);
    controller->renameElement(births, QStringLiteral("births"));
    controller->setEquation(births, QStringLiteral("Population * birth_rate"));
    const EntityId deaths = controller->createFlow(population, sink);
    controller->renameElement(deaths, QStringLiteral("deaths"));
    controller->setEquation(deaths, QStringLiteral("Population * death_rate"));
    const EntityId birthRate = controller->createVariable(ElementKind::Variable, QPointF(0.0, 150.0));
    controller->renameElement(birthRate, QStringLiteral("birth rate"));
    controller->setEquation(birthRate, QStringLiteral("0.125"));
    const EntityId deathRate = controller->createVariable(ElementKind::Variable, QPointF(0.0, 300.0));
    controller->renameElement(deathRate, QStringLiteral("death rate"));
    controller->setEquation(deathRate, QStringLiteral("0.05"));
    controller->setSimulationSettings({0.0, 1.0, 1.0, 1.0});
    scene->item(population)->setSelected(true);

    QVERIFY(simulation->start(QStringLiteral("base")));
    QTRY_COMPARE(simulation->status(), SimulationSessionStatus::Completed);
    const SimulationRun base = simulation->runs().front();
    QCOMPARE(base.causes(QStringLiteral("population")).size(), 2);
    QCOMPARE(base.causes(QStringLiteral("births")).size(), 2);

    window.findChild<QAction *>(QStringLiteral("causesStripAction"))->trigger();
    auto charts = strip->charts();
    QCOMPARE(charts.size(), 3);
    const QStringList expectedTitles {QStringLiteral("Population"), QStringLiteral("births"), QStringLiteral("deaths")};
    for (int index = 0; index < charts.size(); ++index) {
        QCOMPARE(charts[index]->chart()->title(), expectedTitles[index]);
        QCOMPARE(charts[index]->chart()->series().size(), 1);
    }

    const EntityId migrationRate = controller->createVariable(ElementKind::Variable, QPointF(0.0, 450.0));
    controller->renameElement(migrationRate, QStringLiteral("migration rate"));
    controller->setEquation(migrationRate, QStringLiteral("0.01"));
    const EntityId migrationSource = controller->createCloud(QPointF(-300.0, 200.0));
    const EntityId migration = controller->createFlow(migrationSource, population);
    controller->renameElement(migration, QStringLiteral("migration"));
    controller->setEquation(migration, QStringLiteral("Population * migration_rate"));
    QVERIFY(simulation->start(QStringLiteral("expanded")));
    QTRY_COMPARE(simulation->status(), SimulationSessionStatus::Completed);
    QCOMPARE(simulation->runs().size(), 2);
    QCOMPARE(simulation->runs().at(0).causes(QStringLiteral("population")).size(), 3);
    QCOMPARE(simulation->runs().at(1).causes(QStringLiteral("population")).size(), 2);
    QTRY_VERIFY(strip->charts().size() == 4);
    charts = strip->charts();
    const QStringList expandedTitles {QStringLiteral("Population"), QStringLiteral("births"),
        QStringLiteral("deaths"), QStringLiteral("migration")};
    for (int index = 0; index < charts.size(); ++index)
        QCOMPARE(charts[index]->chart()->title(), expandedTitles[index]);
    QCOMPARE(charts[0]->chart()->series().size(), 2);
    QCOMPARE(charts[3]->chart()->series().size(), 1);

    window.findChild<QAction *>(QStringLiteral("causesStripAction"))->trigger();
    auto *migrationAxis = qobject_cast<QtCharts::QValueAxis *>(charts[3]->chart()->axes(Qt::Horizontal).front());
    const double initialMin = migrationAxis->min();
    const double initialMax = migrationAxis->max();
    QVERIFY(strip->isVisible());
    QVERIFY(charts[0]->isVisible());
    const QPoint wheelPosition = charts[0]->rect().center();
    QWheelEvent wheel(wheelPosition, charts[0]->mapToGlobal(wheelPosition), QPoint(), QPoint(0, 120),
        Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(charts[0]->viewport(), &wheel);
    QTRY_VERIFY(migrationAxis->max() - migrationAxis->min() < initialMax - initialMin);
    QMouseEvent doubleClick(QEvent::MouseButtonDblClick, wheelPosition, charts[0]->mapToGlobal(wheelPosition),
        Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(charts[0]->viewport(), &doubleClick);
    QTRY_VERIFY(migrationAxis->min() == initialMin && migrationAxis->max() == initialMax);

    scene->clearSelection();
    scene->item(births)->setSelected(true);
    window.findChild<QAction *>(QStringLiteral("causesStripAction"))->trigger();
    QTRY_COMPARE(strip->charts().size(), 3);
    charts = strip->charts();
    QCOMPARE(charts[0]->chart()->title(), QStringLiteral("births"));
    QCOMPARE(charts[1]->chart()->title(), QStringLiteral("birth rate"));
    QCOMPARE(charts[2]->chart()->title(), QStringLiteral("Population"));
}

void TestEditor::runsCompare()
{
    MainWindow window;
    auto *controller = window.findChild<DocumentController *>();
    auto *simulation = window.findChild<SimulationController *>();
    QVERIFY(controller && simulation);
    const EntityId stock = controller->createVariable(ElementKind::Stock, QPointF());
    controller->setInitialEquation(stock, QStringLiteral("100"));
    const EntityId alpha = controller->createVariable(ElementKind::Variable, QPointF());
    controller->renameElement(alpha, QStringLiteral("alpha"));
    controller->setEquation(alpha, QStringLiteral("2"));
    controller->setUnit(alpha, QStringLiteral("unit"));
    const EntityId legacy = controller->createVariable(ElementKind::Variable, QPointF());
    controller->renameElement(legacy, QStringLiteral("legacy"));
    controller->setEquation(legacy, QStringLiteral("5"));
    const EntityId shared = controller->createVariable(ElementKind::Variable, QPointF());
    controller->renameElement(shared, QStringLiteral("shared"));
    controller->setEquation(shared, QStringLiteral("1"));
    controller->setSimulationSettings({0.0, 1.0, 1.0, 1.0});

    QVERIFY(simulation->start(QStringLiteral("base")));
    QTRY_COMPARE(simulation->status(), SimulationSessionStatus::Completed);
    const SimulationRun base = simulation->runs().front();
    auto *table = window.findChild<QTableView *>(QStringLiteral("runsCompareTable"));
    auto *model = qobject_cast<RunsCompareModel *>(table->model());
    QVERIFY(table && model);
    window.findChild<QAction *>(QStringLiteral("runsCompareAction"))->trigger();
    QCOMPARE(model->rowCount(), 0);

    controller->setEquation(alpha, QStringLiteral("3"));
    controller->removeElements({legacy});
    const EntityId added = controller->createVariable(ElementKind::Variable, QPointF());
    controller->renameElement(added, QStringLiteral("new constant"));
    controller->setEquation(added, QStringLiteral("4"));
    QVERIFY(simulation->start(QStringLiteral("expanded")));
    QTRY_COMPARE(simulation->status(), SimulationSessionStatus::Completed);
    QCOMPARE(simulation->runs().size(), 2);
    QTRY_COMPARE(model->rowCount(), 4);
    QCOMPARE(model->columnCount(), 5);
    QCOMPARE(model->headerData(2, Qt::Horizontal).toString(), QStringLiteral("base"));
    QCOMPARE(model->headerData(3, Qt::Horizontal).toString(), QStringLiteral("expanded"));
    QCOMPARE(model->headerData(4, Qt::Horizontal).toString(), QStringLiteral("Difference"));
    const auto row = [model](const QString &name) {
        for (int index = 0; index < model->rowCount(); ++index) {
            if (model->data(model->index(index, 0)).toString() == name)
                return index;
        }
        return -1;
    };
    const int alphaRow = row(QStringLiteral("alpha"));
    const int legacyRow = row(QStringLiteral("legacy"));
    const int addedRow = row(QStringLiteral("new constant"));
    const int sharedRow = row(QStringLiteral("shared"));
    QVERIFY(alphaRow >= 0 && legacyRow >= 0 && addedRow >= 0 && sharedRow >= 0);
    QCOMPARE(row(QStringLiteral("Stock1")), -1);
    QCOMPARE(model->data(model->index(alphaRow, 1)).toString(), QStringLiteral("unit"));
    QCOMPARE(model->data(model->index(alphaRow, 2)).toString(), QStringLiteral("2"));
    QCOMPARE(model->data(model->index(alphaRow, 3)).toString(), QStringLiteral("3"));
    QCOMPARE(model->data(model->index(alphaRow, 4)).toString(), QStringLiteral("1"));
    QCOMPARE(model->data(model->index(legacyRow, 2)).toString(), QStringLiteral("5"));
    QVERIFY(model->data(model->index(legacyRow, 3)).isNull());
    QVERIFY(model->data(model->index(legacyRow, 4)).isNull());
    QVERIFY(model->data(model->index(addedRow, 2)).isNull());
    QCOMPARE(model->data(model->index(addedRow, 3)).toString(), QStringLiteral("4"));
    QVERIFY(model->data(model->index(addedRow, 4)).isNull());
    QCOMPARE(model->data(model->index(sharedRow, 4)).toString(), QStringLiteral("0"));

    simulation->setRunLoaded(base.id(), false);
    QTRY_COMPARE(model->rowCount(), 0);
    simulation->setRunLoaded(base.id(), true);
    QTRY_COMPARE(model->rowCount(), 4);
}

void TestEditor::resultsAutoOpenCurrentRun()
{
    MainWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto *controller = window.findChild<DocumentController *>();
    auto *simulation = window.findChild<SimulationController *>();
    auto *dock = window.findChild<QDockWidget *>(QStringLiteral("resultsDock"));
    auto *runs = window.findChild<QComboBox *>(QStringLiteral("resultsRunCombo"));
    auto *tabs = window.findChild<QTabWidget *>();
    auto *chart = window.findChild<TimeSeriesChartWidget *>();
    QVERIFY(controller && simulation && dock && runs && tabs && chart);
    const EntityId stock = controller->createVariable(ElementKind::Stock, QPointF());
    controller->setInitialEquation(stock, QStringLiteral("1000"));

    QVERIFY(simulation->start(QStringLiteral("current")));
    QTRY_COMPARE(simulation->status(), SimulationSessionStatus::Completed);
    QTRY_VERIFY(!simulation->isBusy());
    QVERIFY(dock->isVisible());
    QCOMPARE(runs->currentText(), QStringLiteral("current"));
    QCOMPARE(tabs->currentWidget(), chart);

    dock->hide();
    QVERIFY(simulation->start(QStringLiteral("unchanged")));
    QTRY_COMPARE(simulation->status(), SimulationSessionStatus::Completed);
    QTRY_VERIFY(!simulation->isBusy());
    QVERIFY(dock->isVisible());
    QCOMPARE(runs->currentText(), QStringLiteral("unchanged"));

    dock->hide();
    QVERIFY(controller->setSimulationSettings({0.0, 100000.0, 1.0, 1.0}));
    QVERIFY(simulation->start(QStringLiteral("outdated")));
    controller->setInitialEquation(stock, QStringLiteral("1001"));
    QTRY_COMPARE(simulation->status(), SimulationSessionStatus::Completed);
    QTRY_VERIFY(!simulation->isBusy());
    QVERIFY(!dock->isVisible());
    QCOMPARE(simulation->lastRunName(), QStringLiteral("outdated"));
    const auto &completed = simulation->runs().front();
    QVERIFY(simulation->isOutdated(completed));
}

void TestEditor::simulationSettingsAction()
{
    MainWindow window;
    auto *action = window.findChild<QAction *>("simulationSettingsAction");
    auto *documents = window.findChild<DocumentController *>();
    QVERIFY(action && documents);
    QTimer::singleShot(0, &window, []() {
        auto *dialog = qobject_cast<SimulationSettingsDialog *>(QApplication::activeModalWidget());
        if (!dialog)
            return;
        dialog->findChild<QDoubleSpinBox *>("dt")->setValue(0.5);
        dialog->findChild<QDoubleSpinBox *>("stopTime")->setValue(10.0);
        dialog->accept();
    });
    action->trigger();
    QCOMPARE(documents->document().simulationSettings().dt(), 0.5);
    QCOMPARE(documents->document().simulationSettings().saveInterval(), 0.5);
    QCOMPARE(documents->document().simulationSettings().stopTime(), 10.0);
    documents->undoStack()->undo();
    QCOMPARE(documents->document().simulationSettings(), SimulationSettings {});
}

void TestEditor::stockInitialValuePresentation()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    const EntityId stock = controller.createVariable(ElementKind::Stock, QPointF());
    const EntityId cloud = controller.createCloud(QPointF(200.0, 0.0));
    const EntityId births = controller.createFlow(cloud, stock);
    const EntityId deaths = controller.createFlow(stock, cloud);
    controller.renameElement(stock, "Population");
    controller.renameElement(births, "births");
    controller.renameElement(deaths, "deaths");
    controller.setInitialEquation(stock, "1000");
    scene.item(stock)->setSelected(true);
    PropertyPanel panel(&controller, &scene);
    auto *label = panel.findChild<QLabel *>("equationLabel");
    auto *equation = panel.findChild<EquationEditor *>("equationEdit");
    auto *flows = panel.findChild<QLineEdit *>("flowsEdit");
    QVERIFY(label && equation && flows);
    QCOMPARE(label->text(), QStringLiteral("Initial value"));
    QCOMPARE(equation->text(), QStringLiteral("1000"));
    QVERIFY(flows->isVisibleTo(&panel));
    QVERIFY(flows->text().contains("births"));
    QVERIFY(flows->text().contains("deaths"));

    scene.clearSelection();
    scene.item(births)->setSelected(true);
    QCOMPARE(label->text(), QStringLiteral("Equation"));
    QVERIFY(!flows->isVisibleTo(&panel));
}

void TestEditor::recoveryRestoresWorkspace()
{
    QStandardPaths::setTestModeEnabled(true);
    const QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/recovery.qds");
    QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
    QFile::remove(path);
    Document document;
    document.createVariable(ElementKind::Stock);
    MainWindow window;
    auto *view = window.findChild<DiagramView *>();
    QVERIFY(view);
    view->setTransform(QTransform::fromScale(0.66, 0.66));
    view->centerOn(QPointF(123.5, 456.25));
    qds::io::Workspace expected;
    expected.hasViewport = true;
    expected.zoom = view->transform().m11();
    const QPointF expectedCenter = view->mapToScene(view->viewport()->rect().center());
    expected.center = {expectedCenter.x(), expectedCenter.y()};
    expected.hasScrollValues = true;
    expected.scrollX = view->horizontalScrollBar()->value();
    expected.scrollY = view->verticalScrollBar()->value();
    QString error;
    QVERIFY2(qds::io::DocumentFile::save(path, document, expected, &error), qPrintable(error));

    view->resetTransform();
    view->centerOn(QPointF());
    QTimer::singleShot(0, &window, []() {
        auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (message)
            message->button(QMessageBox::Yes)->click();
    });
    window.offerRecovery();
    QCOMPARE(view->transform().m11(), expected.zoom);
    const QPointF center = view->mapToScene(view->viewport()->rect().center());
    QVERIFY(QLineF(center, expectedCenter).length() < 0.001);
    QCOMPARE(view->horizontalScrollBar()->value(), expected.scrollX);
    QCOMPARE(view->verticalScrollBar()->value(), expected.scrollY);
    QFile::remove(path);
    QStandardPaths::setTestModeEnabled(false);
}

void TestEditor::cursorPositionStatus()
{
    MainWindow window;
    auto *view = window.findChild<DiagramView *>();
    auto *label = window.findChild<QLabel *>(QStringLiteral("cursorPosition"));
    QVERIFY(view && label);
    QVERIFY(label->text().contains(QStringLiteral("Zoom: 100%")));
    auto expectedText = [&view](const QPointF &cursor) {
        const QPointF origin = view->mapToScene(0, 0);
        return QStringLiteral("Cursor: %1, %2  View: %3, %4  Zoom: %5%")
            .arg(QString::number(cursor.x(), 'f', 1), QString::number(cursor.y(), 'f', 1),
                 QString::number(origin.x(), 'f', 1), QString::number(origin.y(), 'f', 1))
            .arg(qRound(view->transform().m11() * 100.0));
    };
    const QPoint point(123, 234);
    QMouseEvent move(QEvent::MouseMove, point, view->viewport()->mapToGlobal(point), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(view->viewport(), &move);
    const QPointF sceneCursor = view->mapToScene(point);
    QCOMPARE(label->text(), expectedText(sceneCursor));
    view->zoomIn();
    QCOMPARE(label->text(), expectedText(sceneCursor));
    view->resetZoom();
    QCOMPARE(label->text(), expectedText(sceneCursor));
    view->horizontalScrollBar()->setValue(view->horizontalScrollBar()->value() + 20);
    QCOMPARE(label->text(), expectedText(sceneCursor));
}

void TestEditor::windowLayoutPersistence()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
    QCoreApplication::setOrganizationName(QStringLiteral("QDSTest"));
    QCoreApplication::setApplicationName(QStringLiteral("WindowLayout"));

    MainWindow window;
    QCOMPARE(window.corner(Qt::BottomLeftCorner), Qt::LeftDockWidgetArea);
    QCOMPARE(window.corner(Qt::BottomRightCorner), Qt::RightDockWidgetArea);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.resize(977, 633);
    window.move(41, 61);
    auto *propertyDock = window.findChild<QDockWidget *>(QStringLiteral("propertyDock"));
    auto *navigationDock = window.findChild<QDockWidget *>(QStringLiteral("navigationDock"));
    QVERIFY(propertyDock && navigationDock);
    propertyDock->hide();
    navigationDock->setFloating(true);
    const QList<QDockWidget *> docks = window.findChildren<QDockWidget *>();
    for (const QDockWidget *dock : docks)
        QVERIFY(!dock->objectName().isEmpty());
    const QSize expectedSize = window.size();
    QVERIFY(window.close());

    QSettings settings;
    QVERIFY(!settings.value(QStringLiteral("mainWindow/geometry")).toByteArray().isEmpty());
    QVERIFY(!settings.value(QStringLiteral("mainWindow/state")).toByteArray().isEmpty());

    MainWindow restored;
    QCOMPARE(restored.size(), expectedSize);
    QCOMPARE(restored.corner(Qt::BottomLeftCorner), Qt::LeftDockWidgetArea);
    QCOMPARE(restored.corner(Qt::BottomRightCorner), Qt::RightDockWidgetArea);
    auto *restoredProperties = restored.findChild<QDockWidget *>(QStringLiteral("propertyDock"));
    auto *restoredNavigation = restored.findChild<QDockWidget *>(QStringLiteral("navigationDock"));
    QVERIFY(restoredProperties && restoredNavigation);
    QVERIFY(restoredProperties->isHidden());
    QVERIFY(restoredNavigation->isFloating());
}

void TestEditor::simplifiedChineseTranslation()
{
    Q_INIT_RESOURCE(i18n);
    QTranslator translator;
    QVERIFY(translator.load(QStringLiteral(":/i18n/qds_zh_CN.qm")));
    QApplication::installTranslator(&translator);
    MainWindow window;
    QVERIFY(window.windowTitle().startsWith(QStringLiteral("\u672A\u547D\u540D ")));
    const QList<QAction *> menus = window.menuBar()->actions();
    QVERIFY(menus.size() >= 5);
    QCOMPARE(menus[0]->text(), QStringLiteral("\u6587\u4EF6(&F)"));
    QCOMPARE(menus[1]->text(), QStringLiteral("\u7F16\u8F91(&E)"));
    QCOMPARE(menus[2]->text(), QStringLiteral("\u6A21\u62DF(&S)"));
    QCOMPARE(menus[3]->text(), QStringLiteral("\u89C6\u56FE(&V)"));
    QCOMPARE(menus[4]->text(), QStringLiteral("\u5E03\u7EBF(&R)"));
    bool hasStock = false;
    bool hasVariable = false;
    for (QAction *action : window.findChildren<QAction *>()) {
        hasStock = hasStock || action->text() == QStringLiteral("\u5B58\u91CF");
        hasVariable = hasVariable || action->text() == QStringLiteral("\u53D8\u91CF");
    }
    QVERIFY(hasStock && hasVariable);
    QCOMPARE(window.findChild<QAction *>(QStringLiteral("graphAction"))->text(), QStringLiteral("\u6298\u7EBF\u56FE"));
    QCOMPARE(window.findChild<QAction *>(QStringLiteral("tableAction"))->text(), QStringLiteral("\u8868\u683C"));
    auto *resultsDock = window.findChild<QDockWidget *>(QStringLiteral("resultsDock"));
    auto *results = window.findChild<SimulationResultsWidget *>();
    QVERIFY(resultsDock && results);
    QCOMPARE(resultsDock->windowTitle(), QStringLiteral("\u8FD0\u884C\u7ED3\u679C"));
    QCOMPARE(results->findChild<QLabel *>(QStringLiteral("runStatusLabel"))->text(),
             QStringLiteral("\u5C1A\u65E0\u5DF2\u5B8C\u6210\u7684\u8FD0\u884C"));
    auto *table = results->findChild<QTableView *>(QStringLiteral("resultsTable"));
    QCOMPARE(table->model()->headerData(0, Qt::Horizontal).toString(), QStringLiteral("\u65F6\u95F4"));
    QApplication::removeTranslator(&translator);

    QFile source(QStringLiteral(QDS_ZH_CN_TS_PATH));
    QVERIFY(source.open(QIODevice::ReadOnly));
    QVERIFY(!source.readAll().contains("type=\"unfinished\""));
}

QTEST_MAIN(TestEditor)
#include "TestEditor.moc"
