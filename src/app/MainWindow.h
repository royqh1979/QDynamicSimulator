#pragma once

#include "DiagramScene.h"
#include "RecentFiles.h"

#include <QMainWindow>

class QAction;
class QCloseEvent;
class QLineEdit;
class QListWidget;
class QMenu;
class QTimer;
class DiagramView;
class DocumentController;
class EquationEditor;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    bool openFile(const QString &fileName);
    void offerRecovery();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void createActions();
    void createPropertyPanel();
    void newFile();
    void open();
    bool save();
    bool saveAs();
    bool saveFile(const QString &fileName);
    bool maybeSave();
    void setCurrentFile(const QString &fileName);
    void setDirty(bool dirty);
    void setTool(DiagramScene::Tool tool);
    void updatePropertyEditor();
    void refreshValidation();
    void updateRecentFiles();
    void autoSave();
    QString recoveryFileName() const;

    DocumentController *m_controller;
    DiagramScene *m_scene;
    DiagramView *m_view;
    QLineEdit *m_nameEdit = nullptr;
    EquationEditor *m_equationEdit = nullptr;
    QLineEdit *m_unitEdit = nullptr;
    QLineEdit *m_commentEdit = nullptr;
    QListWidget *m_issueList = nullptr;
    QMenu *m_recentMenu = nullptr;
    QAction *m_selectAction = nullptr;
    QTimer *m_autoSaveTimer = nullptr;
    RecentFiles m_recentFiles;
    QString m_fileName;
    bool m_dirty = false;
};
