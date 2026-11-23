#pragma once

#include "DiagramScene.h"

#include <QMainWindow>

class QCloseEvent;
class QLineEdit;
class DiagramView;
class DocumentController;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    bool openFile(const QString &fileName);

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

    DocumentController *m_controller;
    DiagramScene *m_scene;
    DiagramView *m_view;
    QLineEdit *m_nameEdit = nullptr;
    QLineEdit *m_equationEdit = nullptr;
    QString m_fileName;
    bool m_dirty = false;
};
