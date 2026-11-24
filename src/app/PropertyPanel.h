#pragma once

#include <QWidget>

class DocumentController;
class DiagramScene;
class EquationEditor;
class QLabel;
class QLineEdit;

class PropertyPanel : public QWidget
{
    Q_OBJECT

public:
    PropertyPanel(DocumentController *controller, DiagramScene *scene, QWidget *parent = nullptr);

    bool commit();
    void refresh();

private:
    DocumentController *m_controller;
    DiagramScene *m_scene;
    QLineEdit *m_nameEdit;
    EquationEditor *m_equationEdit;
    QLineEdit *m_unitEdit;
    QLineEdit *m_commentEdit;
    QLabel *m_commentLabel;
};
