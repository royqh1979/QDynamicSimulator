#include "MainWindow.h"

#include "DiagramItems.h"
#include "DiagramView.h"
#include "DocumentController.h"
#include "EntityPalette.h"
#include "EquationEditor.h"
#include "MiniMapView.h"
#include <qds/io/XmileFile.h>
#include <qds/core/Validation.h>

#include <QAction>
#include <QActionGroup>
#include <QCloseEvent>
#include <QDir>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QIcon>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
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

bool isXmileFileDrop(const QMimeData *mime)
{
    if (!mime->hasUrls() || mime->urls().size() != 1)
        return false;
    const QUrl url = mime->urls().front();
    return url.isLocalFile() && QFileInfo(url.toLocalFile()).suffix().compare(QStringLiteral("xmile"), Qt::CaseInsensitive) == 0;
}

}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_controller(new DocumentController(this))
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
    connect(m_scene, &QGraphicsScene::selectionChanged, this, &MainWindow::updatePropertyEditor);
    connect(m_controller, &DocumentController::documentChanged, this, &MainWindow::refreshValidation);
    connect(m_controller, &DocumentController::documentChanged, this, &MainWindow::updatePropertyEditor);
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
    statusBar()->showMessage(tr("选择工具可拖动元素；滚轮缩放画布"), 5000);
    resize(1200, 800);
}

bool MainWindow::openFile(const QString &fileName)
{
    qds::core::Document document;
    QString error;
    if (!qds::io::loadXmile(fileName, document, &error)) {
        QMessageBox::warning(this, tr("打开失败"), error);
        return false;
    }

    m_controller->replaceDocument(std::move(document));
    setCurrentFile(fileName);
    setDirty(false);
    m_recentFiles.add(fileName);
    updateRecentFiles();
    QFile::remove(recoveryFileName());
    if (!m_scene->items().isEmpty())
        m_view->centerOn(m_scene->itemsBoundingRect().center());
    updatePropertyEditor();
    return true;
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (maybeSave()) {
        if (!m_dirty)
            QFile::remove(recoveryFileName());
        event->accept();
    } else {
        event->ignore();
    }
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (isXmileFileDrop(event->mimeData()))
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event)
{
    if (!isXmileFileDrop(event->mimeData()))
        return;
    if (maybeSave())
        openFile(event->mimeData()->urls().front().toLocalFile());
    event->acceptProposedAction();
}

void MainWindow::createActions()
{
    QMenu *fileMenu = menuBar()->addMenu(actionIcon("open"), tr("文件(&F)"));
    QToolBar *fileToolBar = addToolBar(tr("文件"));

    QAction *newAction = new QAction(actionIcon("new"), tr("新建(&N)"), this);
    newAction->setShortcut(QKeySequence::New);
    connect(newAction, &QAction::triggered, this, &MainWindow::newFile);
    fileMenu->addAction(newAction);
    fileToolBar->addAction(newAction);

    QAction *openAction = new QAction(actionIcon("open"), tr("打开(&O)..."), this);
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::open);
    fileMenu->addAction(openAction);
    fileToolBar->addAction(openAction);
    m_recentMenu = fileMenu->addMenu(actionIcon("open"), tr("最近文件"));

    QAction *saveAction = new QAction(actionIcon("save"), tr("保存(&S)"), this);
    saveAction->setShortcut(QKeySequence::Save);
    connect(saveAction, &QAction::triggered, this, [this]() {
        save();
    });
    fileMenu->addAction(saveAction);
    fileToolBar->addAction(saveAction);

    QAction *saveAsAction = new QAction(actionIcon("save-as"), tr("另存为(&A)..."), this);
    saveAsAction->setShortcut(QKeySequence::SaveAs);
    connect(saveAsAction, &QAction::triggered, this, [this]() {
        saveAs();
    });
    fileMenu->addAction(saveAsAction);

    fileMenu->addSeparator();
    QAction *exitAction = new QAction(actionIcon("exit"), tr("退出(&X)"), this);
    exitAction->setShortcut(QKeySequence::Quit);
    connect(exitAction, &QAction::triggered, this, &QWidget::close);
    fileMenu->addAction(exitAction);

    QMenu *editMenu = menuBar()->addMenu(actionIcon("copy"), tr("编辑(&E)"));
    QToolBar *editToolBar = addToolBar(tr("编辑"));
    QAction *undoAction = m_controller->undoStack()->createUndoAction(this, tr("撤销(&U)"));
    undoAction->setIcon(actionIcon("undo"));
    undoAction->setShortcut(QKeySequence::Undo);
    QAction *redoAction = m_controller->undoStack()->createRedoAction(this, tr("重做(&R)"));
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

    m_selectAction = addToolAction(tr("选择"), DiagramScene::Tool::Select, "select");
    m_selectAction->setChecked(true);
    m_selectAction->setShortcut(QKeySequence(Qt::Key_Escape));
    QAction *stockAction = addToolAction(tr("存量"), DiagramScene::Tool::AddStock, "stock");
    stockAction->setShortcut(QKeySequence(Qt::Key_1));
    QAction *flowAction = addToolAction(tr("流量"), DiagramScene::Tool::AddFlow, "flow");
    flowAction->setShortcut(QKeySequence(Qt::Key_2));
    QAction *auxiliaryAction = addToolAction(tr("辅助变量"), DiagramScene::Tool::AddAuxiliary, "auxiliary");
    auxiliaryAction->setShortcut(QKeySequence(Qt::Key_3));
    QAction *constantAction = addToolAction(tr("常量"), DiagramScene::Tool::AddConstant, "constant");
    constantAction->setShortcut(QKeySequence(Qt::Key_5));
    QAction *exogenousAction = addToolAction(tr("外生变量"), DiagramScene::Tool::AddData, "exogenous");
    exogenousAction->setShortcut(QKeySequence(Qt::Key_6));
    QAction *cloudAction = addToolAction(tr("云团"), DiagramScene::Tool::AddCloud, "cloud");
    cloudAction->setShortcut(QKeySequence(Qt::Key_7));
    QAction *annotationAction = addToolAction(tr("注释"), DiagramScene::Tool::AddComment, "annotation");
    annotationAction->setShortcut(QKeySequence(Qt::Key_8));
    QAction *connectAction = addToolAction(tr("箭头"), DiagramScene::Tool::Connect, "connect");
    connectAction->setShortcut(QKeySequence(Qt::Key_4));

    editToolBar->addSeparator();
    QAction *deleteAction = new QAction(actionIcon("delete"), tr("删除"), this);
    deleteAction->setShortcut(QKeySequence(Qt::Key_Delete));
    connect(deleteAction, &QAction::triggered, m_scene, &DiagramScene::removeSelected);
    editMenu->addSeparator();
    editMenu->addAction(deleteAction);
    editToolBar->addAction(deleteAction);

    QAction *selectAllAction = new QAction(actionIcon("select-all"), tr("全选"), this);
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
    QAction *copyAction = new QAction(actionIcon("copy"), tr("复制"), this);
    copyAction->setShortcut(QKeySequence::Copy);
    connect(copyAction, &QAction::triggered, this, copySelection);
    editMenu->addAction(copyAction);
    QAction *cutAction = new QAction(actionIcon("cut"), tr("剪切"), this);
    cutAction->setShortcut(QKeySequence::Cut);
    connect(cutAction, &QAction::triggered, this, [this, copySelection]() {
        copySelection();
        m_scene->removeSelected();
    });
    editMenu->addAction(cutAction);
    QAction *pasteAction = new QAction(actionIcon("paste"), tr("粘贴"), this);
    pasteAction->setShortcut(QKeySequence::Paste);
    connect(pasteAction, &QAction::triggered, this, pasteSelection);
    editMenu->addAction(pasteAction);
    QAction *duplicateAction = new QAction(actionIcon("duplicate"), tr("重复创建"), this);
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
    addAlignAction(tr("左对齐"), Qt::AlignLeft, "align-left");
    addAlignAction(tr("水平居中"), Qt::AlignHCenter, "align-hcenter");
    addAlignAction(tr("右对齐"), Qt::AlignRight, "align-right");
    addAlignAction(tr("顶端对齐"), Qt::AlignTop, "align-top");
    addAlignAction(tr("垂直居中"), Qt::AlignVCenter, "align-vcenter");
    addAlignAction(tr("底端对齐"), Qt::AlignBottom, "align-bottom");

    QAction *distributeHorizontal = new QAction(actionIcon("distribute-horizontal"), tr("水平等距分布"), this);
    connect(distributeHorizontal, &QAction::triggered, this, [this]() {
        m_scene->distributeSelected(true);
    });
    editMenu->addAction(distributeHorizontal);
    QAction *distributeVertical = new QAction(actionIcon("distribute-vertical"), tr("垂直等距分布"), this);
    connect(distributeVertical, &QAction::triggered, this, [this]() {
        m_scene->distributeSelected(false);
    });
    editMenu->addAction(distributeVertical);

    QMenu *viewMenu = menuBar()->addMenu(actionIcon("fit-all"), tr("视图(&V)"));
    auto addViewAction = [this, viewMenu](const QString &text, const QKeySequence &shortcut,
                                          void (DiagramView::*method)(), const char *iconName) {
        QAction *action = new QAction(actionIcon(iconName), text, this);
        action->setShortcut(shortcut);
        connect(action, &QAction::triggered, m_view, method);
        viewMenu->addAction(action);
    };
    addViewAction(tr("放大"), QKeySequence::ZoomIn, &DiagramView::zoomIn, "zoom-in");
    addViewAction(tr("缩小"), QKeySequence::ZoomOut, &DiagramView::zoomOut, "zoom-out");
    addViewAction(tr("实际大小"), QKeySequence(Qt::CTRL | Qt::Key_0), &DiagramView::resetZoom, "zoom-actual");
    addViewAction(tr("适应全部"), QKeySequence(Qt::Key_F), &DiagramView::fitAll, "fit-all");
    addViewAction(tr("适应选中"), QKeySequence(Qt::SHIFT | Qt::Key_F), &DiagramView::fitSelection, "fit-selection");

    QAction *gridAction = new QAction(actionIcon("grid"), tr("显示网格"), this);
    gridAction->setCheckable(true);
    gridAction->setChecked(true);
    connect(gridAction, &QAction::toggled, m_scene, &DiagramScene::setGridVisible);
    viewMenu->addAction(gridAction);
    QAction *snapAction = new QAction(actionIcon("snap"), tr("网格吸附"), this);
    snapAction->setCheckable(true);
    snapAction->setChecked(true);
    connect(snapAction, &QAction::toggled, m_scene, &DiagramScene::setSnapEnabled);
    viewMenu->addAction(snapAction);

    QMenu *routeMenu = menuBar()->addMenu(actionIcon("route-orthogonal"), tr("布线(&R)"));
    auto addRouteMode = [this, routeMenu](const QString &text, qds::core::RouteMode mode, const char *iconName) {
        QAction *action = new QAction(actionIcon(iconName), text, this);
        connect(action, &QAction::triggered, this, [this, mode]() {
            m_scene->setSelectedRouteMode(mode);
        });
        routeMenu->addAction(action);
    };
    addRouteMode(tr("直线"), qds::core::RouteMode::Straight, "route-straight");
    addRouteMode(tr("直角线"), qds::core::RouteMode::Orthogonal, "route-orthogonal");
    addRouteMode(tr("贝塞尔曲线"), qds::core::RouteMode::Bezier, "route-bezier");
    QAction *resetRoute = new QAction(actionIcon("route-reset"), tr("重置自动路径"), this);
    connect(resetRoute, &QAction::triggered, m_scene, &DiagramScene::resetSelectedRoute);
    routeMenu->addAction(resetRoute);
    QAction *toggleLock = new QAction(actionIcon("route-lock"), tr("锁定/解锁路径"), this);
    connect(toggleLock, &QAction::triggered, m_scene, &DiagramScene::toggleSelectedRouteLock);
    routeMenu->addAction(toggleLock);
    QAction *rerouteAll = new QAction(actionIcon("route-reroute"), tr("全图重新布线"), this);
    connect(rerouteAll, &QAction::triggered, m_scene, &DiagramScene::rerouteAll);
    routeMenu->addAction(rerouteAll);
}

void MainWindow::createPropertyPanel()
{
    QDockWidget *paletteDock = new QDockWidget(tr("绘图元素"), this);
    EntityPalette *palette = new EntityPalette(paletteDock);
    connect(palette, &EntityPalette::toolRequested, this, [this](DiagramScene::Tool tool) {
        setTool(tool);
    });
    paletteDock->setWidget(palette);
    addDockWidget(Qt::LeftDockWidgetArea, paletteDock);

    QDockWidget *dock = new QDockWidget(tr("属性"), this);
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    QWidget *panel = new QWidget(dock);
    QFormLayout *layout = new QFormLayout(panel);
    m_nameEdit = new QLineEdit(panel);
    m_equationEdit = new EquationEditor(panel);
    m_unitEdit = new QLineEdit(panel);
    m_commentEdit = new QLineEdit(panel);
    m_commentEdit->setObjectName(QStringLiteral("commentEdit"));
    m_commentLabel = new QLabel(tr("说明"), panel);
    m_commentLabel->setObjectName(QStringLiteral("commentLabel"));
    layout->addRow(tr("名称"), m_nameEdit);
    layout->addRow(tr("方程"), m_equationEdit);
    layout->addRow(tr("单位"), m_unitEdit);
    layout->addRow(m_commentLabel, m_commentEdit);
    dock->setWidget(panel);
    addDockWidget(Qt::RightDockWidgetArea, dock);

    connect(m_nameEdit, &QLineEdit::editingFinished, this, [this]() {
        if (!m_scene->selectedModelElement())
            return;
        if (!m_scene->renameSelected(m_nameEdit->text())) {
            QMessageBox::warning(this, tr("无效名称"), tr("名称不能为空，也不能与其他变量重复。"));
            updatePropertyEditor();
        }
    });
    connect(m_equationEdit, &EquationEditor::editingFinished, this, [this]() {
        m_scene->setSelectedEquation(m_equationEdit->text());
    });
    connect(m_unitEdit, &QLineEdit::editingFinished, this, [this]() {
        if (m_scene->selectedModelElement())
            m_controller->setUnit(m_scene->selectedModelElement()->id(), m_unitEdit->text());
    });
    connect(m_commentEdit, &QLineEdit::editingFinished, this, [this]() {
        if (m_scene->selectedModelElement())
            m_controller->setComment(m_scene->selectedModelElement()->id(), m_commentEdit->text());
        else if (m_scene->selectedComment())
            m_controller->setCommentContent(m_scene->selectedComment()->id(), m_commentEdit->text());
    });

    QDockWidget *issuesDock = new QDockWidget(tr("模型检查"), this);
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

    QDockWidget *navigationDock = new QDockWidget(tr("导航"), this);
    QWidget *navigation = new QWidget(navigationDock);
    QVBoxLayout *navigationLayout = new QVBoxLayout(navigation);
    QLineEdit *searchEdit = new QLineEdit(navigation);
    searchEdit->setPlaceholderText(tr("搜索变量名称"));
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
    updatePropertyEditor();
    refreshValidation();
}

void MainWindow::newFile()
{
    if (!maybeSave())
        return;
    m_controller->replaceDocument(qds::core::Document());
    setCurrentFile(QString());
    setDirty(false);
    updatePropertyEditor();
}

void MainWindow::open()
{
    if (!maybeSave())
        return;
    const QString fileName = QFileDialog::getOpenFileName(
        this,
        tr("打开 XMILE 模型"),
        QString(),
        tr("XMILE 模型 (*.xmile);;所有文件 (*)"));
    if (!fileName.isEmpty())
        openFile(fileName);
}

bool MainWindow::save()
{
    if (m_scene->selectedModelElement()) {
        if (!m_scene->renameSelected(m_nameEdit->text())) {
            QMessageBox::warning(this, tr("无效名称"), tr("名称不能为空，也不能与其他变量重复。"));
            updatePropertyEditor();
            return false;
        }
        m_scene->setSelectedEquation(m_equationEdit->text());
        m_controller->setUnit(m_scene->selectedModelElement()->id(), m_unitEdit->text());
        m_controller->setComment(m_scene->selectedModelElement()->id(), m_commentEdit->text());
    } else if (m_scene->selectedComment()) {
        m_controller->setCommentContent(m_scene->selectedComment()->id(), m_commentEdit->text());
    }
    return m_fileName.isEmpty() ? saveAs() : saveFile(m_fileName);
}

bool MainWindow::saveAs()
{
    QString fileName = QFileDialog::getSaveFileName(
        this,
        tr("保存 XMILE 模型"),
        m_fileName,
        tr("XMILE 模型 (*.xmile);;所有文件 (*)"));
    if (fileName.isEmpty())
        return false;
    if (QFileInfo(fileName).suffix().isEmpty())
        fileName += QStringLiteral(".xmile");
    return saveFile(fileName);
}

bool MainWindow::saveFile(const QString &fileName)
{
    QString error;
    if (!qds::io::saveXmile(fileName, m_controller->document(), &error)) {
        QMessageBox::warning(this, tr("保存失败"), error);
        return false;
    }
    setCurrentFile(fileName);
    m_controller->undoStack()->setClean();
    setDirty(false);
    m_recentFiles.add(fileName);
    updateRecentFiles();
    QFile::remove(recoveryFileName());
    statusBar()->showMessage(tr("已保存 %1").arg(QFileInfo(fileName).fileName()), 5000);
    return true;
}

bool MainWindow::maybeSave()
{
    if (!m_dirty)
        return true;
    const QMessageBox::StandardButton choice = QMessageBox::warning(
        this,
        tr("未保存的修改"),
        tr("当前模型已修改，是否保存？"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);
    if (choice == QMessageBox::Save)
        return save();
    return choice == QMessageBox::Discard;
}

void MainWindow::setCurrentFile(const QString &fileName)
{
    m_fileName = fileName;
    const QString displayName = fileName.isEmpty() ? tr("未命名.xmile") : QFileInfo(fileName).fileName();
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
        statusBar()->showMessage(tr("拖动移动元素，框选可多选，Delete 删除"));
        break;
    case DiagramScene::Tool::AddStock:
        statusBar()->showMessage(tr("在画布上单击以创建存量"));
        break;
    case DiagramScene::Tool::AddFlow:
        statusBar()->showMessage(tr("从存量或云团拖动到目标以创建流量"));
        break;
    case DiagramScene::Tool::AddAuxiliary:
        statusBar()->showMessage(tr("在画布上单击以创建辅助变量"));
        break;
    case DiagramScene::Tool::AddConstant:
        statusBar()->showMessage(tr("在画布上单击以创建常量"));
        break;
    case DiagramScene::Tool::AddData:
        statusBar()->showMessage(tr("在画布上单击以创建外生变量"));
        break;
    case DiagramScene::Tool::AddCloud:
        statusBar()->showMessage(tr("在画布上单击以创建源/汇云团"));
        break;
    case DiagramScene::Tool::AddComment:
        statusBar()->showMessage(tr("在画布上单击以创建注释"));
        break;
    case DiagramScene::Tool::Connect:
        statusBar()->showMessage(tr("拖动起点到终点以创建箭头；右键取消"));
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
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/recovery.xmile");
}

void MainWindow::autoSave()
{
    if (!m_dirty)
        return;
    const QString path = recoveryFileName();
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return;
    QString error;
    if (!qds::io::saveXmile(path, m_controller->document(), &error))
        statusBar()->showMessage(tr("自动保存失败：%1").arg(error), 5000);
}

void MainWindow::offerRecovery()
{
    const QString path = recoveryFileName();
    if (!QFile::exists(path))
        return;
    const auto choice = QMessageBox::question(
        this, tr("恢复模型"), tr("检测到上次会话的自动保存副本，是否恢复？"));
    if (choice != QMessageBox::Yes)
        return;
    qds::core::Document document;
    QString error;
    if (!qds::io::loadXmile(path, document, &error)) {
        QMessageBox::warning(this, tr("恢复失败"), error);
        return;
    }
    m_controller->replaceDocument(std::move(document));
    setCurrentFile(QString());
    setDirty(true);
}

void MainWindow::updatePropertyEditor()
{
    const qds::core::ModelElement *variable = m_scene->selectedModelElement();
    const qds::core::Comment *comment = m_scene->selectedComment();
    m_nameEdit->setEnabled(variable != nullptr);
    m_equationEdit->setEnabled(variable != nullptr);
    m_unitEdit->setEnabled(variable != nullptr);
    m_commentEdit->setEnabled(variable != nullptr || comment != nullptr);
    m_commentLabel->setText(comment ? tr("正文") : tr("说明"));
    m_nameEdit->setText(variable ? QString::fromStdString(variable->displayName) : QString());
    m_equationEdit->setText(variable ? QString::fromStdString(variable->equation) : QString());
    QStringList completionWords;
    for (const qds::core::ModelElement &entry : m_controller->document().model().elements())
        completionWords.append(QString::fromStdString(entry.displayName));
    m_equationEdit->setCompletionWords(completionWords);
    m_unitEdit->setText(variable ? QString::fromStdString(variable->unit) : QString());
    m_commentEdit->setText(variable ? QString::fromStdString(variable->comment)
        : comment ? QString::fromStdString(comment->content) : QString());
}

void MainWindow::refreshValidation()
{
    if (!m_issueList)
        return;
    m_issueList->clear();
    const auto issues = qds::core::validateDocument(m_controller->document());
    for (const auto &issue : issues) {
        const auto *variable = dynamic_cast<const qds::core::ModelElement *>(m_controller->document().find(issue.entityId));
        const QString name = variable ? QString::fromStdString(variable->displayName) : tr("模型");
        const QString severity = issue.severity == qds::core::IssueSeverity::Error ? tr("错误") : tr("警告");
        QListWidgetItem *item = new QListWidgetItem(
            tr("%1 [%2]: %3").arg(name, severity, QString::fromStdString(issue.message)), m_issueList);
        item->setData(Qt::UserRole, static_cast<qulonglong>(issue.entityId));
    }
}
