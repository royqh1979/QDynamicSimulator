#include "MainWindow.h"

#include "DiagramItems.h"
#include "DiagramView.h"
#include "DocumentController.h"
#include "EntityPalette.h"
#include "MiniMapView.h"
#include "PropertyPanel.h"
#include "SimulationController.h"
#include "SimulationSettingsDialog.h"
#include <io/DocumentFile.h>
#include <io/XmileFile.h>
#include <core/Validation.h>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDir>
#include <QDialog>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QIcon>
#include <QImage>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QScrollBar>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <QUrl>

namespace {

ClipboardData clipboard;

QIcon actionIcon(const char *name)
{
    return QIcon(QStringLiteral(":/icons/") + QLatin1String(name) + QStringLiteral(".svg"));
}

bool isDocumentFileDrop(const QMimeData *mime)
{
    if (!mime->hasUrls() || mime->urls().size() != 1)
        return false;
    const QUrl url = mime->urls().front();
    if (!url.isLocalFile())
        return false;
    const QString suffix = QFileInfo(url.toLocalFile()).suffix();
    return suffix.compare(QStringLiteral("qds"), Qt::CaseInsensitive) == 0
        || suffix.compare(QStringLiteral("xmile"), Qt::CaseInsensitive) == 0
        || suffix.compare(QStringLiteral("xml"), Qt::CaseInsensitive) == 0;
}

}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_controller(new DocumentController(this))
    , m_simulation(new SimulationController(m_controller))
    , m_scene(new DiagramScene(m_controller, this))
    , m_view(new DiagramView(m_scene, this))
{
    Q_INIT_RESOURCE(icons);
    setAcceptDrops(true);
    setCentralWidget(m_view);
    connect(m_view, &DiagramView::fileOpenRequested, this, [this](const QString &fileName) {
        if (maybeSave())
            openFile(fileName);
    });
    createActions();
    createPropertyPanel();

    connect(m_controller->undoStack(), &QUndoStack::cleanChanged, this, [this](bool clean) {
        setDirty(!clean);
    });
    connect(m_controller, &DocumentController::documentChanged, this, &MainWindow::refreshValidation);
    connect(m_simulation, &SimulationController::statusChanged, this,
        [this](SimulationSessionStatus status) {
            QString text;
            switch (status) {
            case SimulationSessionStatus::Idle: text = tr("Ready"); break;
            case SimulationSessionStatus::Compiling: text = tr("Compiling"); break;
            case SimulationSessionStatus::Running: text = tr("Running"); break;
            case SimulationSessionStatus::Cancelling: text = tr("Cancelling"); break;
            case SimulationSessionStatus::Completed: text = tr("Completed"); break;
            case SimulationSessionStatus::Cancelled: text = tr("Cancelled"); break;
            case SimulationSessionStatus::Failed: text = tr("Failed"); break;
            }
            statusBar()->showMessage(text, 5000);
        });
    connect(m_simulation, &SimulationController::busyChanged, this, [this](bool) {
        updateSimulationControls();
    });
    connect(m_simulation, &SimulationController::runTerminated, this,
        [this](const std::shared_ptr<const qds::core::SimulationRun> &run) {
            if (run->status() == qds::core::SimulationRunStatus::Completed) {
                statusBar()->showMessage(tr("Completed %1: %2 samples")
                    .arg(m_simulation->lastRunName()).arg(run->times().size()), 10000);
            }
        });
    connect(m_simulation, &SimulationController::compilationFailed, this, &MainWindow::showCompilationIssues);
    connect(m_simulation, &SimulationController::errorOccurred, this, [this](const QString &message) {
        statusBar()->showMessage(message, 10000);
    });
    connect(m_scene, &DiagramScene::editingFinished, this, [this]() {
        setTool(DiagramScene::Tool::Select);
        m_selectAction->setChecked(true);
    });

    QSettings settings;
    const QStringList recent = settings.value(QStringLiteral("recentFiles")).toStringList();
    for (int index = recent.size() - 1; index >= 0; --index)
        m_recentFiles.add(recent[index]);
    updateRecentFiles();
    m_autoSaveTimer = new QTimer(this);
    connect(m_autoSaveTimer, &QTimer::timeout, this, &MainWindow::autoSave);
    m_autoSaveTimer->start(10000);

    setCurrentFile(QString());
    statusBar()->showMessage(tr("Drag elements with Select; zoom with the mouse wheel"), 5000);
    m_cursorPosition = new QLabel(this);
    m_cursorPosition->setObjectName(QStringLiteral("cursorPosition"));
    statusBar()->addPermanentWidget(m_cursorPosition);
    connect(m_view, &DiagramView::cursorScenePositionChanged, this, [this](const QPointF &position) {
        m_cursorScenePosition = position;
        updateCursorStatus();
    });
    connect(m_view, &DiagramView::viewportChanged, this, &MainWindow::updateCursorStatus);
    connect(m_view->horizontalScrollBar(), &QScrollBar::valueChanged, this, &MainWindow::updateCursorStatus);
    connect(m_view->verticalScrollBar(), &QScrollBar::valueChanged, this, &MainWindow::updateCursorStatus);
    updateCursorStatus();
    resize(1200, 800);
    restoreWindowLayout();
}

bool MainWindow::openFile(const QString &fileName)
{
    qds::core::Document document;
    qds::io::Workspace workspace;
    QString error;
    bool nativeFormat = false;
    if (!qds::io::DocumentFile::open(fileName, document, workspace, &error, &nativeFormat)) {
        QMessageBox::warning(this, tr("Open failed"), error);
        return false;
    }

    m_controller->replaceDocument(std::move(document));
    setCurrentFile(nativeFormat ? fileName : QString());
    setDirty(!nativeFormat);
    m_recentFiles.add(fileName);
    updateRecentFiles();
    QFile::remove(recoveryFileName());
    m_propertyPanel->refresh();
    applyWorkspace(workspace);
    updateCursorStatus();
    return true;
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (maybeSave()) {
        m_simulation->reset();
        saveWindowLayout();
        if (!m_dirty)
            QFile::remove(recoveryFileName());
        event->accept();
    } else {
        event->ignore();
    }
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (isDocumentFileDrop(event->mimeData()))
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event)
{
    if (!isDocumentFileDrop(event->mimeData()))
        return;
    if (maybeSave())
        openFile(event->mimeData()->urls().front().toLocalFile());
    event->acceptProposedAction();
}

void MainWindow::createActions()
{
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    QToolBar *fileToolBar = addToolBar(tr("File"));
    fileToolBar->setObjectName(QStringLiteral("fileToolBar"));

    QAction *newAction = new QAction(actionIcon("new"), tr("&New"), this);
    newAction->setShortcut(QKeySequence::New);
    connect(newAction, &QAction::triggered, this, &MainWindow::newFile);
    fileMenu->addAction(newAction);
    fileToolBar->addAction(newAction);

    QAction *openAction = new QAction(actionIcon("open"), tr("&Open..."), this);
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::open);
    fileMenu->addAction(openAction);
    fileToolBar->addAction(openAction);
    m_recentMenu = fileMenu->addMenu(actionIcon("open"), tr("Recent files"));

    QAction *saveAction = new QAction(actionIcon("save"), tr("&Save"), this);
    saveAction->setShortcut(QKeySequence::Save);
    connect(saveAction, &QAction::triggered, this, [this]() {
        save();
    });
    fileMenu->addAction(saveAction);
    fileToolBar->addAction(saveAction);

    QAction *saveAsAction = new QAction(actionIcon("save-as"), tr("Save &As..."), this);
    saveAsAction->setShortcut(QKeySequence::SaveAs);
    connect(saveAsAction, &QAction::triggered, this, [this]() {
        saveAs();
    });
    fileMenu->addAction(saveAsAction);

    QAction *exportAction = new QAction(tr("Export XMILE..."), this);
    connect(exportAction, &QAction::triggered, this, &MainWindow::exportXmile);
    fileMenu->addAction(exportAction);
    QAction *exportPngAction = new QAction(tr("Export PNG..."), this);
    exportPngAction->setObjectName(QStringLiteral("exportPngAction"));
    connect(exportPngAction, &QAction::triggered, this, [this]() {
        exportPng();
    });
    fileMenu->addAction(exportPngAction);

    fileMenu->addSeparator();
    QAction *exitAction = new QAction(actionIcon("exit"), tr("E&xit"), this);
    exitAction->setShortcut(QKeySequence::Quit);
    connect(exitAction, &QAction::triggered, this, &QWidget::close);
    fileMenu->addAction(exitAction);

    QMenu *editMenu = menuBar()->addMenu(tr("&Edit"));
    QToolBar *editToolBar = addToolBar(tr("Edit"));
    editToolBar->setObjectName(QStringLiteral("editToolBar"));
    QAction *undoAction = m_controller->undoStack()->createUndoAction(this, tr("&Undo"));
    undoAction->setIcon(actionIcon("undo"));
    undoAction->setShortcut(QKeySequence::Undo);
    QAction *redoAction = m_controller->undoStack()->createRedoAction(this, tr("&Redo"));
    redoAction->setIcon(actionIcon("redo"));
    redoAction->setShortcut(QKeySequence::Redo);
    editMenu->addAction(undoAction);
    editMenu->addAction(redoAction);
    editMenu->addSeparator();
    editToolBar->addAction(undoAction);
    editToolBar->addAction(redoAction);
    editToolBar->addSeparator();
    QActionGroup *toolGroup = new QActionGroup(this);

    auto addToolAction = [this, toolGroup, editMenu, editToolBar](
                             const QString &text, DiagramScene::Tool tool, const char *iconName) {
        QAction *action = new QAction(actionIcon(iconName), text, this);
        action->setCheckable(true);
        toolGroup->addAction(action);
        editMenu->addAction(action);
        editToolBar->addAction(action);
        connect(action, &QAction::triggered, this, [this, tool]() {
            setTool(tool);
        });
        return action;
    };

    m_selectAction = addToolAction(tr("Select"), DiagramScene::Tool::Select, "select");
    m_selectAction->setChecked(true);
    m_selectAction->setShortcut(QKeySequence(Qt::Key_Escape));
    QAction *stockAction = addToolAction(tr("Stock"), DiagramScene::Tool::AddStock, "stock");
    stockAction->setShortcut(QKeySequence(Qt::Key_1));
    QAction *flowAction = addToolAction(tr("Flow"), DiagramScene::Tool::AddFlow, "flow");
    flowAction->setShortcut(QKeySequence(Qt::Key_2));
    QAction *auxiliaryAction = addToolAction(tr("Auxiliary"), DiagramScene::Tool::AddAuxiliary, "auxiliary");
    auxiliaryAction->setShortcut(QKeySequence(Qt::Key_3));
    QAction *constantAction = addToolAction(tr("Constant"), DiagramScene::Tool::AddConstant, "constant");
    constantAction->setShortcut(QKeySequence(Qt::Key_5));
    QAction *dataAction = addToolAction(tr("Data"), DiagramScene::Tool::AddData, "data");
    dataAction->setShortcut(QKeySequence(Qt::Key_6));
    QAction *cloudAction = addToolAction(tr("Cloud"), DiagramScene::Tool::AddCloud, "cloud");
    cloudAction->setShortcut(QKeySequence(Qt::Key_7));
    QAction *annotationAction = addToolAction(tr("Comment"), DiagramScene::Tool::AddComment, "annotation");
    annotationAction->setShortcut(QKeySequence(Qt::Key_8));
    QAction *connectAction = addToolAction(tr("Arrow"), DiagramScene::Tool::Connect, "connect");
    connectAction->setShortcut(QKeySequence(Qt::Key_4));

    editToolBar->addSeparator();
    QAction *deleteAction = new QAction(actionIcon("delete"), tr("Delete"), this);
    deleteAction->setShortcut(QKeySequence(Qt::Key_Delete));
    connect(deleteAction, &QAction::triggered, m_scene, &DiagramScene::removeSelected);
    editMenu->addSeparator();
    editMenu->addAction(deleteAction);
    editToolBar->addAction(deleteAction);

    QAction *selectAllAction = new QAction(actionIcon("select-all"), tr("Select all"), this);
    selectAllAction->setShortcut(QKeySequence::SelectAll);
    connect(selectAllAction, &QAction::triggered, m_scene, &DiagramScene::selectAllItems);
    editMenu->addAction(selectAllAction);

    auto copySelection = [this]() {
        clipboard = m_controller->copyElements(m_scene->selectedElementIds());
    };
    auto pasteSelection = [this]() {
        const auto ids = m_controller->pasteClipboard(clipboard, QPointF(20.0, 20.0));
        m_scene->clearSelection();
        for (qds::core::EntityId id : ids) {
            if (DiagramItem *item = m_scene->item(id))
                item->setSelected(true);
        }
    };
    QAction *copyAction = new QAction(actionIcon("copy"), tr("Copy"), this);
    copyAction->setShortcut(QKeySequence::Copy);
    connect(copyAction, &QAction::triggered, this, copySelection);
    editMenu->addAction(copyAction);
    QAction *copyPngAction = new QAction(tr("Copy as PNG"), this);
    copyPngAction->setObjectName(QStringLiteral("copyPngAction"));
    connect(copyPngAction, &QAction::triggered, this, &MainWindow::copyPng);
    editMenu->addAction(copyPngAction);
    QAction *cutAction = new QAction(actionIcon("cut"), tr("Cut"), this);
    cutAction->setShortcut(QKeySequence::Cut);
    connect(cutAction, &QAction::triggered, this, [this, copySelection]() {
        copySelection();
        m_scene->removeSelected();
    });
    editMenu->addAction(cutAction);
    QAction *pasteAction = new QAction(actionIcon("paste"), tr("Paste"), this);
    pasteAction->setShortcut(QKeySequence::Paste);
    connect(pasteAction, &QAction::triggered, this, pasteSelection);
    editMenu->addAction(pasteAction);
    QAction *duplicateAction = new QAction(actionIcon("duplicate"), tr("Duplicate"), this);
    duplicateAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
    connect(duplicateAction, &QAction::triggered, this, [this, copySelection, pasteSelection]() {
        copySelection();
        pasteSelection();
    });
    editMenu->addAction(duplicateAction);

    auto addAlignAction = [this, editMenu](const QString &text, Qt::Alignment alignment, const char *iconName) {
        QAction *action = new QAction(actionIcon(iconName), text, this);
        connect(action, &QAction::triggered, this, [this, alignment]() {
            m_scene->alignSelected(alignment);
        });
        editMenu->addAction(action);
    };
    addAlignAction(tr("Align left"), Qt::AlignLeft, "align-left");
    addAlignAction(tr("Align horizontal centers"), Qt::AlignHCenter, "align-hcenter");
    addAlignAction(tr("Align right"), Qt::AlignRight, "align-right");
    addAlignAction(tr("Align top"), Qt::AlignTop, "align-top");
    addAlignAction(tr("Align vertical centers"), Qt::AlignVCenter, "align-vcenter");
    addAlignAction(tr("Align bottom"), Qt::AlignBottom, "align-bottom");

    QAction *distributeHorizontal = new QAction(actionIcon("distribute-horizontal"), tr("Distribute horizontally"), this);
    connect(distributeHorizontal, &QAction::triggered, this, [this]() {
        m_scene->distributeSelected(true);
    });
    editMenu->addAction(distributeHorizontal);
    QAction *distributeVertical = new QAction(actionIcon("distribute-vertical"), tr("Distribute vertically"), this);
    connect(distributeVertical, &QAction::triggered, this, [this]() {
        m_scene->distributeSelected(false);
    });
    editMenu->addAction(distributeVertical);

    QMenu *simulationMenu = menuBar()->addMenu(tr("&Simulation"));
    QToolBar *simulationToolBar = addToolBar(tr("Simulation"));
    simulationToolBar->setObjectName(QStringLiteral("simulationToolBar"));
    QLabel *runNameLabel = new QLabel(tr("Run name:"), simulationToolBar);
    simulationToolBar->addWidget(runNameLabel);
    m_runName = new QLineEdit(simulationToolBar);
    m_runName->setObjectName(QStringLiteral("runNameEdit"));
    m_runName->setText(QStringLiteral("Run1"));
    m_runName->setMaximumWidth(180);
    simulationToolBar->addWidget(m_runName);
    m_runAction = new QAction(actionIcon("run"), tr("Run"), this);
    m_runAction->setObjectName(QStringLiteral("runAction"));
    m_runAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));
    m_cancelAction = new QAction(actionIcon("cancel"), tr("Cancel"), this);
    m_cancelAction->setObjectName(QStringLiteral("cancelAction"));
    m_cancelAction->setShortcut(QKeySequence(Qt::Key_Escape));
    m_simulationSettingsAction = new QAction(actionIcon("simulation-settings"), tr("Simulation settings"), this);
    m_simulationSettingsAction->setObjectName(QStringLiteral("simulationSettingsAction"));
    for (QAction *action : {m_runAction, m_cancelAction, m_simulationSettingsAction}) {
        simulationMenu->addAction(action);
        simulationToolBar->addAction(action);
    }
    connect(m_runAction, &QAction::triggered, this, &MainWindow::runSimulation);
    connect(m_runName, &QLineEdit::returnPressed, this, &MainWindow::runSimulation);
    connect(m_cancelAction, &QAction::triggered, m_simulation, &SimulationController::cancel);
    connect(m_simulationSettingsAction, &QAction::triggered, this, &MainWindow::openSimulationSettings);
    updateSimulationControls();

    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));
    auto addViewAction = [this, viewMenu](const QString &text, const QKeySequence &shortcut,
                                          void (DiagramView::*method)(), const char *iconName) {
        QAction *action = new QAction(actionIcon(iconName), text, this);
        action->setShortcut(shortcut);
        connect(action, &QAction::triggered, m_view, method);
        viewMenu->addAction(action);
    };
    addViewAction(tr("Zoom in"), QKeySequence::ZoomIn, &DiagramView::zoomIn, "zoom-in");
    addViewAction(tr("Zoom out"), QKeySequence::ZoomOut, &DiagramView::zoomOut, "zoom-out");
    addViewAction(tr("Actual size"), QKeySequence(Qt::CTRL | Qt::Key_0), &DiagramView::resetZoom, "zoom-actual");
    addViewAction(tr("Fit all"), QKeySequence(Qt::Key_F), &DiagramView::fitAll, "fit-all");
    addViewAction(tr("Fit selection"), QKeySequence(Qt::SHIFT | Qt::Key_F), &DiagramView::fitSelection, "fit-selection");

    QAction *gridAction = new QAction(actionIcon("grid"), tr("Show grid"), this);
    gridAction->setCheckable(true);
    gridAction->setChecked(true);
    connect(gridAction, &QAction::toggled, m_scene, &DiagramScene::setGridVisible);
    viewMenu->addAction(gridAction);
    QAction *snapAction = new QAction(actionIcon("snap"), tr("Snap to grid"), this);
    snapAction->setCheckable(true);
    snapAction->setChecked(true);
    connect(snapAction, &QAction::toggled, m_scene, &DiagramScene::setSnapEnabled);
    viewMenu->addAction(snapAction);

    QMenu *routeMenu = menuBar()->addMenu(tr("&Routing"));
    auto addRouteMode = [this, routeMenu](const QString &text, qds::core::RouteMode mode, const char *iconName) {
        QAction *action = new QAction(actionIcon(iconName), text, this);
        connect(action, &QAction::triggered, this, [this, mode]() {
            m_scene->setSelectedRouteMode(mode);
        });
        routeMenu->addAction(action);
    };
    addRouteMode(tr("Straight"), qds::core::RouteMode::Straight, "route-straight");
    addRouteMode(tr("Orthogonal"), qds::core::RouteMode::Orthogonal, "route-orthogonal");
    addRouteMode(tr("Bezier"), qds::core::RouteMode::Bezier, "route-bezier");
    QAction *resetRoute = new QAction(actionIcon("route-reset"), tr("Reset automatic route"), this);
    connect(resetRoute, &QAction::triggered, m_scene, &DiagramScene::resetSelectedRoute);
    routeMenu->addAction(resetRoute);
    QAction *toggleLock = new QAction(actionIcon("route-lock"), tr("Lock/unlock route"), this);
    connect(toggleLock, &QAction::triggered, m_scene, &DiagramScene::toggleSelectedRouteLock);
    routeMenu->addAction(toggleLock);
    QAction *rerouteAll = new QAction(actionIcon("route-reroute"), tr("Reroute all"), this);
    connect(rerouteAll, &QAction::triggered, m_scene, &DiagramScene::rerouteAll);
    routeMenu->addAction(rerouteAll);
}

void MainWindow::createPropertyPanel()
{
    QDockWidget *paletteDock = new QDockWidget(tr("Drawing elements"), this);
    paletteDock->setObjectName(QStringLiteral("paletteDock"));
    EntityPalette *palette = new EntityPalette(paletteDock);
    connect(palette, &EntityPalette::toolRequested, this, [this](DiagramScene::Tool tool) {
        setTool(tool);
    });
    paletteDock->setWidget(palette);
    addDockWidget(Qt::LeftDockWidgetArea, paletteDock);

    QDockWidget *dock = new QDockWidget(tr("Properties"), this);
    dock->setObjectName(QStringLiteral("propertyDock"));
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_propertyPanel = new PropertyPanel(m_controller, m_scene, dock);
    dock->setWidget(m_propertyPanel);
    addDockWidget(Qt::RightDockWidgetArea, dock);

    QDockWidget *issuesDock = new QDockWidget(tr("Model validation"), this);
    issuesDock->setObjectName(QStringLiteral("issuesDock"));
    m_issueList = new QListWidget(issuesDock);
    issuesDock->setWidget(m_issueList);
    addDockWidget(Qt::BottomDockWidgetArea, issuesDock);
    connect(m_issueList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        const qds::core::EntityId id = item->data(Qt::UserRole).toULongLong();
        if (DiagramItem *variable = m_scene->item(id)) {
            m_scene->clearSelection();
            variable->setSelected(true);
            m_view->centerOn(variable);
        }
    });

    QDockWidget *navigationDock = new QDockWidget(tr("Navigation"), this);
    navigationDock->setObjectName(QStringLiteral("navigationDock"));
    QWidget *navigation = new QWidget(navigationDock);
    QVBoxLayout *navigationLayout = new QVBoxLayout(navigation);
    QLineEdit *searchEdit = new QLineEdit(navigation);
    searchEdit->setPlaceholderText(tr("Search variable names"));
    navigationLayout->addWidget(searchEdit);
    MiniMapView *miniMap = new MiniMapView(m_scene, navigation);
    navigationLayout->addWidget(miniMap);
    navigationDock->setWidget(navigation);
    addDockWidget(Qt::LeftDockWidgetArea, navigationDock);
    connect(searchEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_scene->clearSelection();
        const auto matches = m_scene->findElements(text);
        bool first = true;
        for (qds::core::EntityId id : matches) {
            DiagramItem *item = m_scene->item(id);
            if (!item)
                continue;
            item->setSelected(true);
            if (first) {
                m_view->centerOn(item);
                first = false;
            }
        }
    });
    m_propertyPanel->refresh();
    refreshValidation();
}

void MainWindow::restoreWindowLayout()
{
    QSettings settings;
    restoreGeometry(settings.value(QStringLiteral("mainWindow/geometry")).toByteArray());
    restoreState(settings.value(QStringLiteral("mainWindow/state")).toByteArray());
}

void MainWindow::saveWindowLayout()
{
    QSettings settings;
    settings.setValue(QStringLiteral("mainWindow/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("mainWindow/state"), saveState());
}

void MainWindow::updateCursorStatus()
{
    const QPointF viewOrigin = m_view->mapToScene(0, 0);
    m_cursorPosition->setText(tr("Cursor: %1, %2  View: %3, %4  Zoom: %5%")
        .arg(QString::number(m_cursorScenePosition.x(), 'f', 1), QString::number(m_cursorScenePosition.y(), 'f', 1),
             QString::number(viewOrigin.x(), 'f', 1), QString::number(viewOrigin.y(), 'f', 1))
        .arg(qRound(m_view->transform().m11() * 100.0)));
}

qds::io::Workspace MainWindow::currentWorkspace() const
{
    qds::io::Workspace workspace;
    workspace.hasViewport = true;
    workspace.zoom = m_view->transform().m11();
    const QPointF center = m_view->mapToScene(m_view->viewport()->rect().center());
    workspace.center = {center.x(), center.y()};
    workspace.hasScrollValues = true;
    workspace.scrollX = m_view->horizontalScrollBar()->value();
    workspace.scrollY = m_view->verticalScrollBar()->value();
    return workspace;
}

void MainWindow::applyWorkspace(const qds::io::Workspace &workspace)
{
    if (workspace.hasViewport) {
        m_view->setTransform(QTransform::fromScale(workspace.zoom, workspace.zoom));
        if (workspace.hasScrollValues) {
            m_view->horizontalScrollBar()->setValue(workspace.scrollX);
            m_view->verticalScrollBar()->setValue(workspace.scrollY);
        } else {
            m_view->centerOn(QPointF(workspace.center.x, workspace.center.y));
        }
    } else if (!m_scene->items().isEmpty()) {
        m_view->centerOn(m_scene->itemsBoundingRect().center());
    }
}

void MainWindow::runSimulation()
{
    const QString name = m_runName->text().trimmed();
    const bool overwrite = m_simulation->hasRunName(name);
    if (overwrite) {
        const auto choice = QMessageBox::question(this, tr("Overwrite run"),
            tr("Run '%1' already exists. Overwrite its dataset?").arg(name));
        if (choice != QMessageBox::Yes)
            return;
    }
    m_simulation->start(name, overwrite);
}

void MainWindow::openSimulationSettings()
{
    SimulationSettingsDialog dialog(m_controller->document().simulationSettings(), this);
    if (dialog.exec() == QDialog::Accepted)
        m_controller->setSimulationSettings(dialog.settings());
}

void MainWindow::updateSimulationControls()
{
    const bool busy = m_simulation->isBusy();
    if (m_runAction)
        m_runAction->setEnabled(!busy);
    if (m_cancelAction)
        m_cancelAction->setEnabled(busy);
}

void MainWindow::showCompilationIssues(const QVector<qds::core::ModelIssue> &issues)
{
    if (!m_issueList)
        return;
    m_issueList->clear();
    for (const qds::core::ModelIssue &issue : issues) {
        const auto *element = dynamic_cast<const qds::core::ModelElement *>(m_controller->document().find(issue.entityId));
        const QString name = element ? element->displayName() : tr("Model");
        auto *item = new QListWidgetItem(tr("%1: %2").arg(name, issue.message), m_issueList);
        item->setData(Qt::UserRole, issue.entityId);
    }
    if (!issues.isEmpty()) {
        m_issueList->setCurrentRow(0);
        if (QDockWidget *dock = qobject_cast<QDockWidget *>(m_issueList->parentWidget()))
            dock->show();
    }
    statusBar()->showMessage(tr("Simulation compilation failed"), 10000);
}

void MainWindow::newFile()
{
    if (!maybeSave())
        return;
    m_controller->replaceDocument(qds::core::Document());
    setCurrentFile(QString());
    setDirty(false);
    m_propertyPanel->refresh();
}

void MainWindow::open()
{
    if (!maybeSave())
        return;
    const QString fileName = QFileDialog::getOpenFileName(
        this,
        tr("Open model"),
        QString(),
        tr("QDS documents (*.qds);;XMILE models (*.xmile *.xml);;All files (*)"));
    if (!fileName.isEmpty())
        openFile(fileName);
}

bool MainWindow::save()
{
    if (!m_propertyPanel->commit())
        return false;
    return m_fileName.isEmpty() ? saveAs() : saveFile(m_fileName);
}

bool MainWindow::saveAs()
{
    QString fileName = QFileDialog::getSaveFileName(
        this,
        tr("Save QDS document"),
        m_fileName,
        tr("QDS documents (*.qds);;All files (*)"));
    if (fileName.isEmpty())
        return false;
    if (QFileInfo(fileName).suffix().isEmpty())
        fileName += QStringLiteral(".qds");
    return saveFile(fileName);
}

bool MainWindow::exportXmile()
{
    if (!m_propertyPanel->commit())
        return false;
    QString fileName = QFileDialog::getSaveFileName(
        this,
        tr("Export XMILE model"),
        QString(),
        tr("XMILE models (*.xmile);;All files (*)"));
    if (fileName.isEmpty())
        return false;
    if (QFileInfo(fileName).suffix().isEmpty())
        fileName += QStringLiteral(".xmile");
    QString error;
    if (!qds::io::XmileFile::exportFile(fileName, m_controller->document(), &error)) {
        QMessageBox::warning(this, tr("Export failed"), error);
        return false;
    }
    statusBar()->showMessage(tr("Exported %1").arg(QFileInfo(fileName).fileName()), 5000);
    return true;
}

bool MainWindow::exportPng()
{
    QString fileName = QFileDialog::getSaveFileName(
        this,
        tr("Export PNG image"),
        QString(),
        tr("PNG images (*.png);;All files (*)"));
    if (fileName.isEmpty())
        return false;
    if (QFileInfo(fileName).suffix().isEmpty())
        fileName += QStringLiteral(".png");
    return exportPng(fileName);
}

bool MainWindow::exportPng(const QString &fileName)
{
    if (!m_propertyPanel->commit())
        return false;
    if (!m_scene->renderImage().save(fileName, "PNG")) {
        QMessageBox::warning(this, tr("Export failed"), tr("Could not write the PNG image."));
        return false;
    }
    statusBar()->showMessage(tr("Exported %1").arg(QFileInfo(fileName).fileName()), 5000);
    return true;
}

void MainWindow::copyPng()
{
    if (!m_propertyPanel->commit())
        return;
    const QImage image = m_scene->renderImage(true);
    if (image.isNull()) {
        statusBar()->showMessage(tr("Select one or more elements to copy as PNG"), 5000);
        return;
    }
    QApplication::clipboard()->setImage(image);
}

bool MainWindow::saveFile(const QString &fileName)
{
    QString error;
    if (!qds::io::DocumentFile::save(fileName, m_controller->document(), currentWorkspace(), &error)) {
        QMessageBox::warning(this, tr("Save failed"), error);
        return false;
    }
    setCurrentFile(fileName);
    m_controller->undoStack()->setClean();
    setDirty(false);
    m_recentFiles.add(fileName);
    updateRecentFiles();
    QFile::remove(recoveryFileName());
    statusBar()->showMessage(tr("Saved %1").arg(QFileInfo(fileName).fileName()), 5000);
    return true;
}

bool MainWindow::maybeSave()
{
    if (!m_dirty)
        return true;
    const QMessageBox::StandardButton choice = QMessageBox::warning(
        this,
        tr("Unsaved changes"),
        tr("The current model has been modified. Save it?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);
    if (choice == QMessageBox::Save)
        return save();
    return choice == QMessageBox::Discard;
}

void MainWindow::setCurrentFile(const QString &fileName)
{
    m_fileName = fileName;
    const QString displayName = fileName.isEmpty() ? tr("Untitled.qds") : QFileInfo(fileName).fileName();
    setWindowTitle(tr("%1[*] - QDynamicSimulator").arg(displayName));
    setWindowModified(false);
}

void MainWindow::setDirty(bool dirty)
{
    m_dirty = dirty;
    setWindowModified(dirty);
}

void MainWindow::setTool(DiagramScene::Tool tool)
{
    m_scene->setTool(tool);
    m_view->setDragMode(tool == DiagramScene::Tool::Select ? QGraphicsView::RubberBandDrag : QGraphicsView::NoDrag);
    switch (tool) {
    case DiagramScene::Tool::Select:
        statusBar()->showMessage(tr("Drag to move, rubber-band to select, Delete to remove"));
        break;
    case DiagramScene::Tool::AddStock:
        statusBar()->showMessage(tr("Click on the canvas to create a stock"));
        break;
    case DiagramScene::Tool::AddFlow:
        statusBar()->showMessage(tr("Drag from a stock, cloud, or blank point to create a flow"));
        break;
    case DiagramScene::Tool::AddAuxiliary:
        statusBar()->showMessage(tr("Click on the canvas to create an auxiliary"));
        break;
    case DiagramScene::Tool::AddConstant:
        statusBar()->showMessage(tr("Click on the canvas to create a constant"));
        break;
    case DiagramScene::Tool::AddData:
        statusBar()->showMessage(tr("Click on the canvas to create data"));
        break;
    case DiagramScene::Tool::AddCloud:
        statusBar()->showMessage(tr("Click on the canvas to create a source/sink cloud"));
        break;
    case DiagramScene::Tool::AddComment:
        statusBar()->showMessage(tr("Click on the canvas to create a comment"));
        break;
    case DiagramScene::Tool::Connect:
        statusBar()->showMessage(tr("Drag from source to target to create an arrow; right-click to cancel"));
        break;
    }
}

void MainWindow::updateRecentFiles()
{
    if (!m_recentMenu)
        return;
    m_recentMenu->clear();
    for (const QString &fileName : m_recentFiles.files()) {
        QAction *action = new QAction(actionIcon("open"), QFileInfo(fileName).fileName(), m_recentMenu);
        action->setToolTip(fileName);
        connect(action, &QAction::triggered, this, [this, fileName]() {
            if (maybeSave())
                openFile(fileName);
        });
        m_recentMenu->addAction(action);
    }
    m_recentMenu->setEnabled(!m_recentFiles.files().isEmpty());
    QSettings settings;
    settings.setValue(QStringLiteral("recentFiles"), m_recentFiles.files());
}

QString MainWindow::recoveryFileName() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/recovery.qds");
}

void MainWindow::autoSave()
{
    if (!m_dirty)
        return;
    const QString path = recoveryFileName();
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return;
    QString error;
    if (!qds::io::DocumentFile::save(path, m_controller->document(), currentWorkspace(), &error))
        statusBar()->showMessage(tr("Auto-save failed: %1").arg(error), 5000);
}

void MainWindow::offerRecovery()
{
    const QString path = recoveryFileName();
    if (!QFile::exists(path))
        return;
    const auto choice = QMessageBox::question(
        this, tr("Restore model"), tr("An auto-saved copy from the previous session was found. Restore it?"));
    if (choice != QMessageBox::Yes)
        return;
    qds::core::Document document;
    qds::io::Workspace workspace;
    QString error;
    if (!qds::io::DocumentFile::open(path, document, workspace, &error)) {
        QMessageBox::warning(this, tr("Restore failed"), error);
        return;
    }
    m_controller->replaceDocument(std::move(document));
    applyWorkspace(workspace);
    setCurrentFile(QString());
    setDirty(true);
}


void MainWindow::refreshValidation()
{
    if (!m_issueList)
        return;
    m_issueList->clear();
    const auto issues = qds::core::validateDocument(m_controller->document());
    for (const auto &issue : issues) {
        const auto *variable = dynamic_cast<const qds::core::ModelElement *>(m_controller->document().find(issue.entityId));
        const QString name = variable ? (variable->displayName()) : tr("Model");
        const QString severity = issue.severity == qds::core::IssueSeverity::Error ? tr("Error") : tr("Warning");
        QListWidgetItem *item = new QListWidgetItem(
            tr("%1 [%2]: %3").arg(name, severity, (issue.message)), m_issueList);
        item->setData(Qt::UserRole, static_cast<qulonglong>(issue.entityId));
    }
}
