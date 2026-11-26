#pragma once

#include "DiagramScene.h"
#include "RecentFiles.h"

#include <QMainWindow>

class QAction;
class QCloseEvent;
class QLabel;
class QListWidget;
class QMenu;
class QTimer;
class DiagramView;
class DocumentController;
class PropertyPanel;

namespace qds::io {
struct Workspace;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
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
    void setCurrentFile(const QString &fileName);
    void setDirty(bool dirty);
    void setTool(DiagramScene::Tool tool);
    void refreshValidation();
    void updateRecentFiles();
    void updateCursorStatus();
    qds::io::Workspace currentWorkspace() const;
    void applyWorkspace(const qds::io::Workspace &workspace);
    void autoSave();
    QString recoveryFileName() const;

    DocumentController *m_controller;
    DiagramScene *m_scene;
    DiagramView *m_view;
    PropertyPanel *m_propertyPanel = nullptr;
    QListWidget *m_issueList = nullptr;
    QMenu *m_recentMenu = nullptr;
    QAction *m_selectAction = nullptr;
    QTimer *m_autoSaveTimer = nullptr;
    QLabel *m_cursorPosition = nullptr;
    QPointF m_cursorScenePosition;
    RecentFiles m_recentFiles;
    QString m_fileName;
    bool m_dirty = false;
};
