#pragma once

#include "DiagramScene.h"
#include "RecentFiles.h"

#include <QMainWindow>

class QAction;
class QCloseEvent;
class QListWidget;
class QMenu;
class QTimer;
class DiagramView;
class DocumentController;
class PropertyPanel;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    bool openFile(const QString &fileName);
    void offerRecovery();

protected:
    void closeEvent(QCloseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void createActions();
    void createPropertyPanel();
    void newFile();
    void open();
    bool save();
    bool saveAs();
    bool exportXmile();
    bool saveFile(const QString &fileName);
    bool maybeSave();
    void setCurrentFile(const QString &fileName);
    void setDirty(bool dirty);
    void setTool(DiagramScene::Tool tool);
    void refreshValidation();
    void updateRecentFiles();
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
    RecentFiles m_recentFiles;
    QString m_fileName;
    bool m_dirty = false;
};
