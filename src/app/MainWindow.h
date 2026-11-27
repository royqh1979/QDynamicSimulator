#pragma once

#include "DiagramScene.h"
#include "RecentFiles.h"
#include <core/Validation.h>

#include <QMainWindow>

class QAction;
class QCloseEvent;
class QLabel;
class QLineEdit;
class QListWidget;
class QMenu;
class QTimer;
class DiagramView;
class DocumentController;
class PropertyPanel;
class SimulationController;
class SimulationResultsWidget;

namespace qds::io {
struct Workspace;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;
    bool openFile(const QString &fileName);
    bool exportPng(const QString &fileName);
    void copyPng();
    bool saveFile(const QString &fileName);
    void offerRecovery();

protected:
    void closeEvent(QCloseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void createActions();
    void createPropertyPanel();
    void restoreWindowLayout();
    void saveWindowLayout();
    void newFile();
    void open();
    bool save();
    bool saveAs();
    bool exportXmile();
    bool exportPng();
    bool maybeSave();
    void setCurrentFile(const QString &fileName, const QString &documentTitle = QString());
    QString nextUntitledTitle();
    void setDirty(bool dirty);
    void setTool(DiagramScene::Tool tool);
    void refreshValidation();
    void updateRecentFiles();
    void updateCursorStatus();
    void runSimulation();
    void openSimulationSettings();
    void updateSimulationControls();
    void showCompilationIssues(const QVector<qds::core::ModelIssue> &issues);
    qds::io::Workspace currentWorkspace() const;
    void applyWorkspace(const qds::io::Workspace &workspace);
    void autoSave();
    QString recoveryFileName() const;

    DocumentController *m_controller;
    SimulationController *m_simulation;
    SimulationResultsWidget *m_results = nullptr;
    QDockWidget *m_resultsDock = nullptr;
    DiagramScene *m_scene;
    DiagramView *m_view;
    PropertyPanel *m_propertyPanel = nullptr;
    QListWidget *m_issueList = nullptr;
    QMenu *m_recentMenu = nullptr;
    QAction *m_selectAction = nullptr;
    QTimer *m_autoSaveTimer = nullptr;
    QLabel *m_cursorPosition = nullptr;
    QAction *m_runAction = nullptr;
    QAction *m_cancelAction = nullptr;
    QAction *m_simulationSettingsAction = nullptr;
    QLineEdit *m_runName = nullptr;
    QPointF m_cursorScenePosition;
    RecentFiles m_recentFiles;
    QString m_fileName;
    QString m_documentTitle;
    bool m_dirty = false;
};
