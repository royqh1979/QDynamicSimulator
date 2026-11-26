#pragma once

#include <QWidget>

class DocumentController;
class DiagramScene;
class QFont;
class EquationEditor;
class QFontComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;

class PropertyPanel : public QWidget
{
    Q_OBJECT

public:
    PropertyPanel(DocumentController *controller, DiagramScene *scene, QWidget *parent = nullptr);

    bool commit();
    void refresh();

private:
    void commitFontStyle(const QFont &font);

    DocumentController *m_controller;
    DiagramScene *m_scene;
    QLineEdit *m_nameEdit;
    EquationEditor *m_equationEdit;
    QLabel *m_equationLabel;
    QLabel *m_flowsLabel;
    QLineEdit *m_flowsEdit;
    QLineEdit *m_unitEdit;
    QLineEdit *m_commentEdit;
    QLabel *m_commentLabel;
    QFontComboBox *m_fontCombo;
    QSpinBox *m_fontSize;
};
