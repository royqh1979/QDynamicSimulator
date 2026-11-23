#include "MainWindow.h"

#include "DiagramView.h"
#include "DocumentController.h"
#include <qds/io/XmileFile.h>

#include <QAction>
#include <QActionGroup>
#include <QCloseEvent>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QKeySequence>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QToolBar>

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
    if (!m_scene->items().isEmpty())
        m_view->centerOn(m_scene->itemsBoundingRect().center());
    updatePropertyEditor();
    return true;
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (maybeSave())
        event->accept();
    else
        event->ignore();
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

    QAction *selectAction = addToolAction(tr("选择"), DiagramScene::Tool::Select);
    selectAction->setChecked(true);
    selectAction->setShortcut(QKeySequence(Qt::Key_Escape));
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
}

void MainWindow::createPropertyPanel()
{
    QDockWidget *dock = new QDockWidget(tr("属性"), this);
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    QWidget *panel = new QWidget(dock);
    QFormLayout *layout = new QFormLayout(panel);
    m_nameEdit = new QLineEdit(panel);
    m_equationEdit = new QLineEdit(panel);
    layout->addRow(tr("名称"), m_nameEdit);
    layout->addRow(tr("方程"), m_equationEdit);
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
    connect(m_equationEdit, &QLineEdit::editingFinished, this, [this]() {
        m_scene->setSelectedEquation(m_equationEdit->text());
    });
    updatePropertyEditor();
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

void MainWindow::updatePropertyEditor()
{
    const qds::core::Variable *variable = m_scene->selectedVariable();
    m_nameEdit->setEnabled(variable != nullptr);
    m_equationEdit->setEnabled(variable != nullptr);
    m_nameEdit->setText(variable ? QString::fromStdString(variable->name) : QString());
    m_equationEdit->setText(variable ? QString::fromStdString(variable->equation) : QString());
}
