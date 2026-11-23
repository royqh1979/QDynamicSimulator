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
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QKeySequence>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

namespace {

ClipboardData clipboard;

}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_controller(new DocumentController(this))
    , m_scene(new DiagramScene(m_controller, this))
    , m_view(new DiagramView(m_scene, this))
{
    setCentralWidget(m_view);
    createActions();
    createPropertyPanel();

    connect(m_controller->undoStack(), &QUndoStack::cleanChanged, this, [this](bool clean) {
        setDirty(!clean);
    });
    connect(m_scene, &QGraphicsScene::selectionChanged, this, &MainWindow::updatePropertyEditor);
    connect(m_controller, &DocumentController::documentChanged, this, &MainWindow::refreshValidation);
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

void MainWindow::createActions()
{
    QMenu *fileMenu = menuBar()->addMenu(tr("文件(&F)"));
    QToolBar *fileToolBar = addToolBar(tr("文件"));

    QAction *newAction = new QAction(tr("新建(&N)"), this);
    newAction->setShortcut(QKeySequence::New);
    connect(newAction, &QAction::triggered, this, &MainWindow::newFile);
    fileMenu->addAction(newAction);
    fileToolBar->addAction(newAction);

    QAction *openAction = new QAction(tr("打开(&O)..."), this);
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::open);
    fileMenu->addAction(openAction);
    fileToolBar->addAction(openAction);
    m_recentMenu = fileMenu->addMenu(tr("最近文件"));

    QAction *saveAction = new QAction(tr("保存(&S)"), this);
    saveAction->setShortcut(QKeySequence::Save);
    connect(saveAction, &QAction::triggered, this, [this]() {
        save();
    });
    fileMenu->addAction(saveAction);
    fileToolBar->addAction(saveAction);

    QAction *saveAsAction = new QAction(tr("另存为(&A)..."), this);
    saveAsAction->setShortcut(QKeySequence::SaveAs);
    connect(saveAsAction, &QAction::triggered, this, [this]() {
        saveAs();
    });
    fileMenu->addAction(saveAsAction);

    fileMenu->addSeparator();
    QAction *exitAction = new QAction(tr("退出(&X)"), this);
    exitAction->setShortcut(QKeySequence::Quit);
    connect(exitAction, &QAction::triggered, this, &QWidget::close);
    fileMenu->addAction(exitAction);

    QMenu *editMenu = menuBar()->addMenu(tr("编辑(&E)"));
    QToolBar *editToolBar = addToolBar(tr("编辑"));
    QAction *undoAction = m_controller->undoStack()->createUndoAction(this, tr("撤销(&U)"));
    undoAction->setShortcut(QKeySequence::Undo);
    QAction *redoAction = m_controller->undoStack()->createRedoAction(this, tr("重做(&R)"));
    redoAction->setShortcut(QKeySequence::Redo);
    editMenu->addAction(undoAction);
    editMenu->addAction(redoAction);
    editMenu->addSeparator();
    editToolBar->addAction(undoAction);
    editToolBar->addAction(redoAction);
    editToolBar->addSeparator();
    QActionGroup *toolGroup = new QActionGroup(this);

    auto addToolAction = [this, toolGroup, editMenu, editToolBar](const QString &text, DiagramScene::Tool tool) {
        QAction *action = new QAction(text, this);
        action->setCheckable(true);
        toolGroup->addAction(action);
        editMenu->addAction(action);
        editToolBar->addAction(action);
        connect(action, &QAction::triggered, this, [this, tool]() {
            setTool(tool);
        });
        return action;
    };

    m_selectAction = addToolAction(tr("选择"), DiagramScene::Tool::Select);
    m_selectAction->setChecked(true);
    m_selectAction->setShortcut(QKeySequence(Qt::Key_Escape));
    QAction *stockAction = addToolAction(tr("存量"), DiagramScene::Tool::AddStock);
    stockAction->setShortcut(QKeySequence(Qt::Key_1));
    QAction *flowAction = addToolAction(tr("流量"), DiagramScene::Tool::AddFlow);
    flowAction->setShortcut(QKeySequence(Qt::Key_2));
    QAction *auxiliaryAction = addToolAction(tr("辅助变量"), DiagramScene::Tool::AddAuxiliary);
    auxiliaryAction->setShortcut(QKeySequence(Qt::Key_3));
    QAction *constantAction = addToolAction(tr("常量"), DiagramScene::Tool::AddConstant);
    constantAction->setShortcut(QKeySequence(Qt::Key_5));
    QAction *exogenousAction = addToolAction(tr("外生变量"), DiagramScene::Tool::AddExogenous);
    exogenousAction->setShortcut(QKeySequence(Qt::Key_6));
    QAction *cloudAction = addToolAction(tr("云团"), DiagramScene::Tool::AddCloud);
    cloudAction->setShortcut(QKeySequence(Qt::Key_7));
    QAction *annotationAction = addToolAction(tr("注释"), DiagramScene::Tool::AddAnnotation);
    annotationAction->setShortcut(QKeySequence(Qt::Key_8));
    QAction *connectAction = addToolAction(tr("依赖线"), DiagramScene::Tool::Connect);
    connectAction->setShortcut(QKeySequence(Qt::Key_4));
    QAction *materialAction = addToolAction(tr("物料流"), DiagramScene::Tool::ConnectMaterial);
    materialAction->setShortcut(QKeySequence(Qt::Key_9));
    addToolAction(tr("注释箭头"), DiagramScene::Tool::ConnectAnnotation);

    editToolBar->addSeparator();
    QAction *deleteAction = new QAction(tr("删除"), this);
    deleteAction->setShortcut(QKeySequence(Qt::Key_Delete));
    connect(deleteAction, &QAction::triggered, m_scene, &DiagramScene::removeSelected);
    editMenu->addSeparator();
    editMenu->addAction(deleteAction);
    editToolBar->addAction(deleteAction);

    QAction *selectAllAction = new QAction(tr("全选"), this);
    selectAllAction->setShortcut(QKeySequence::SelectAll);
    connect(selectAllAction, &QAction::triggered, m_scene, &DiagramScene::selectAllItems);
    editMenu->addAction(selectAllAction);

    auto copySelection = [this]() {
        clipboard = m_controller->copyVariables(m_scene->selectedVariableIds());
    };
    auto pasteSelection = [this]() {
        const auto ids = m_controller->pasteClipboard(clipboard, QPointF(20.0, 20.0));
        m_scene->clearSelection();
        for (qds::core::EntityId id : ids) {
            if (VariableItem *item = m_scene->variableItem(id))
                item->setSelected(true);
        }
    };
    QAction *copyAction = new QAction(tr("复制"), this);
    copyAction->setShortcut(QKeySequence::Copy);
    connect(copyAction, &QAction::triggered, this, copySelection);
    editMenu->addAction(copyAction);
    QAction *cutAction = new QAction(tr("剪切"), this);
    cutAction->setShortcut(QKeySequence::Cut);
    connect(cutAction, &QAction::triggered, this, [this, copySelection]() {
        copySelection();
        m_scene->removeSelected();
    });
    editMenu->addAction(cutAction);
    QAction *pasteAction = new QAction(tr("粘贴"), this);
    pasteAction->setShortcut(QKeySequence::Paste);
    connect(pasteAction, &QAction::triggered, this, pasteSelection);
    editMenu->addAction(pasteAction);
    QAction *duplicateAction = new QAction(tr("重复创建"), this);
    duplicateAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
    connect(duplicateAction, &QAction::triggered, this, [this, copySelection, pasteSelection]() {
        copySelection();
        pasteSelection();
    });
    editMenu->addAction(duplicateAction);

    auto addAlignAction = [this, editMenu](const QString &text, Qt::Alignment alignment) {
        QAction *action = new QAction(text, this);
        connect(action, &QAction::triggered, this, [this, alignment]() {
            m_scene->alignSelected(alignment);
        });
        editMenu->addAction(action);
    };
    addAlignAction(tr("左对齐"), Qt::AlignLeft);
    addAlignAction(tr("水平居中"), Qt::AlignHCenter);
    addAlignAction(tr("右对齐"), Qt::AlignRight);
    addAlignAction(tr("顶端对齐"), Qt::AlignTop);
    addAlignAction(tr("垂直居中"), Qt::AlignVCenter);
    addAlignAction(tr("底端对齐"), Qt::AlignBottom);

    QAction *distributeHorizontal = new QAction(tr("水平等距分布"), this);
    connect(distributeHorizontal, &QAction::triggered, this, [this]() {
        m_scene->distributeSelected(true);
    });
    editMenu->addAction(distributeHorizontal);
    QAction *distributeVertical = new QAction(tr("垂直等距分布"), this);
    connect(distributeVertical, &QAction::triggered, this, [this]() {
        m_scene->distributeSelected(false);
    });
    editMenu->addAction(distributeVertical);

    QMenu *viewMenu = menuBar()->addMenu(tr("视图(&V)"));
    auto addViewAction = [this, viewMenu](const QString &text, const QKeySequence &shortcut, void (DiagramView::*method)()) {
        QAction *action = new QAction(text, this);
        action->setShortcut(shortcut);
        connect(action, &QAction::triggered, m_view, method);
        viewMenu->addAction(action);
    };
    addViewAction(tr("放大"), QKeySequence::ZoomIn, &DiagramView::zoomIn);
    addViewAction(tr("缩小"), QKeySequence::ZoomOut, &DiagramView::zoomOut);
    addViewAction(tr("实际大小"), QKeySequence(Qt::CTRL | Qt::Key_0), &DiagramView::resetZoom);
    addViewAction(tr("适应全部"), QKeySequence(Qt::Key_F), &DiagramView::fitAll);
    addViewAction(tr("适应选中"), QKeySequence(Qt::SHIFT | Qt::Key_F), &DiagramView::fitSelection);

    QAction *gridAction = new QAction(tr("显示网格"), this);
    gridAction->setCheckable(true);
    gridAction->setChecked(true);
    connect(gridAction, &QAction::toggled, m_scene, &DiagramScene::setGridVisible);
    viewMenu->addAction(gridAction);
    QAction *snapAction = new QAction(tr("网格吸附"), this);
    snapAction->setCheckable(true);
    snapAction->setChecked(true);
    connect(snapAction, &QAction::toggled, m_scene, &DiagramScene::setSnapEnabled);
    viewMenu->addAction(snapAction);

    QMenu *routeMenu = menuBar()->addMenu(tr("布线(&R)"));
    auto addRouteMode = [this, routeMenu](const QString &text, qds::core::RouteMode mode) {
        QAction *action = new QAction(text, this);
        connect(action, &QAction::triggered, this, [this, mode]() {
            m_scene->setSelectedRouteMode(mode);
        });
        routeMenu->addAction(action);
    };
    addRouteMode(tr("直线"), qds::core::RouteMode::Straight);
    addRouteMode(tr("直角线"), qds::core::RouteMode::Orthogonal);
    addRouteMode(tr("贝塞尔曲线"), qds::core::RouteMode::Bezier);
    QAction *resetRoute = new QAction(tr("重置自动路径"), this);
    connect(resetRoute, &QAction::triggered, m_scene, &DiagramScene::resetSelectedRoute);
    routeMenu->addAction(resetRoute);
    QAction *toggleLock = new QAction(tr("锁定/解锁路径"), this);
    connect(toggleLock, &QAction::triggered, m_scene, &DiagramScene::toggleSelectedRouteLock);
    routeMenu->addAction(toggleLock);
    QAction *rerouteAll = new QAction(tr("全图重新布线"), this);
    connect(rerouteAll, &QAction::triggered, m_scene, &DiagramScene::rerouteAll);
    routeMenu->addAction(rerouteAll);
}

void MainWindow::createPropertyPanel()
{
    QDockWidget *paletteDock = new QDockWidget(tr("绘图元素"), this);
    EntityPalette *palette = new EntityPalette(paletteDock);
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
    layout->addRow(tr("名称"), m_nameEdit);
    layout->addRow(tr("方程"), m_equationEdit);
    layout->addRow(tr("单位"), m_unitEdit);
    layout->addRow(tr("说明"), m_commentEdit);
    dock->setWidget(panel);
    addDockWidget(Qt::RightDockWidgetArea, dock);

    connect(m_nameEdit, &QLineEdit::editingFinished, this, [this]() {
        if (!m_scene->selectedVariable())
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
        if (m_scene->selectedVariable())
            m_controller->setUnit(m_scene->selectedVariable()->id, m_unitEdit->text());
    });
    connect(m_commentEdit, &QLineEdit::editingFinished, this, [this]() {
        if (m_scene->selectedVariable())
            m_controller->setComment(m_scene->selectedVariable()->id, m_commentEdit->text());
    });

    QDockWidget *issuesDock = new QDockWidget(tr("模型检查"), this);
    m_issueList = new QListWidget(issuesDock);
    issuesDock->setWidget(m_issueList);
    addDockWidget(Qt::BottomDockWidgetArea, issuesDock);
    connect(m_issueList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        const qds::core::EntityId id = item->data(Qt::UserRole).toULongLong();
        if (VariableItem *variable = m_scene->variableItem(id)) {
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
        const auto matches = m_scene->findVariables(text);
        bool first = true;
        for (qds::core::EntityId id : matches) {
            VariableItem *item = m_scene->variableItem(id);
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
    if (m_scene->selectedVariable()) {
        if (!m_scene->renameSelected(m_nameEdit->text())) {
            QMessageBox::warning(this, tr("无效名称"), tr("名称不能为空，也不能与其他变量重复。"));
            updatePropertyEditor();
            return false;
        }
        m_scene->setSelectedEquation(m_equationEdit->text());
        m_controller->setUnit(m_scene->selectedVariable()->id, m_unitEdit->text());
        m_controller->setComment(m_scene->selectedVariable()->id, m_commentEdit->text());
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
        statusBar()->showMessage(tr("在画布上单击以创建流量"));
        break;
    case DiagramScene::Tool::AddAuxiliary:
        statusBar()->showMessage(tr("在画布上单击以创建辅助变量"));
        break;
    case DiagramScene::Tool::AddConstant:
        statusBar()->showMessage(tr("在画布上单击以创建常量"));
        break;
    case DiagramScene::Tool::AddExogenous:
        statusBar()->showMessage(tr("在画布上单击以创建外生变量"));
        break;
    case DiagramScene::Tool::AddCloud:
        statusBar()->showMessage(tr("在画布上单击以创建源/汇云团"));
        break;
    case DiagramScene::Tool::AddAnnotation:
        statusBar()->showMessage(tr("在画布上单击以创建注释"));
        break;
    case DiagramScene::Tool::Connect:
    case DiagramScene::Tool::ConnectMaterial:
    case DiagramScene::Tool::ConnectAnnotation:
        statusBar()->showMessage(tr("依次单击起点和终点；右键取消连线"));
        break;
    }
}

void MainWindow::updateRecentFiles()
{
    if (!m_recentMenu)
        return;
    m_recentMenu->clear();
    for (const QString &fileName : m_recentFiles.files()) {
        QAction *action = new QAction(QFileInfo(fileName).fileName(), m_recentMenu);
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
    const qds::core::Variable *variable = m_scene->selectedVariable();
    m_nameEdit->setEnabled(variable != nullptr);
    m_equationEdit->setEnabled(variable != nullptr);
    m_unitEdit->setEnabled(variable != nullptr);
    m_commentEdit->setEnabled(variable != nullptr);
    m_nameEdit->setText(variable ? QString::fromStdString(variable->name) : QString());
    m_equationEdit->setText(variable ? QString::fromStdString(variable->equation) : QString());
    QStringList completionWords;
    for (const qds::core::Variable &entry : m_controller->document().model().variables())
        completionWords.append(QString::fromStdString(entry.name));
    m_equationEdit->setCompletionWords(completionWords);
    m_unitEdit->setText(variable ? QString::fromStdString(variable->unit) : QString());
    m_commentEdit->setText(variable ? QString::fromStdString(variable->comment) : QString());
}

void MainWindow::refreshValidation()
{
    if (!m_issueList)
        return;
    m_issueList->clear();
    const auto issues = qds::core::validateDocument(m_controller->document());
    for (const auto &issue : issues) {
        const qds::core::Variable *variable = m_controller->document().variable(issue.entityId);
        const QString name = variable ? QString::fromStdString(variable->name) : tr("模型");
        const QString severity = issue.severity == qds::core::IssueSeverity::Error ? tr("错误") : tr("警告");
        QListWidgetItem *item = new QListWidgetItem(
            tr("%1 [%2]: %3").arg(name, severity, QString::fromStdString(issue.message)), m_issueList);
        item->setData(Qt::UserRole, static_cast<qulonglong>(issue.entityId));
    }
}
