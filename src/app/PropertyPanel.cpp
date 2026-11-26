#include "PropertyPanel.h"

#include "DiagramScene.h"
#include "DocumentController.h"
#include "EquationEditor.h"

#include <QApplication>
#include <QFontComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QSpinBox>

PropertyPanel::PropertyPanel(DocumentController *controller, DiagramScene *scene, QWidget *parent)
    : QWidget(parent)
    , m_controller(controller)
    , m_scene(scene)
    , m_nameEdit(new QLineEdit(this))
    , m_equationEdit(new EquationEditor(this))
    , m_unitEdit(new QLineEdit(this))
    , m_commentEdit(new QLineEdit(this))
    , m_commentLabel(new QLabel(this))
    , m_fontCombo(new QFontComboBox(this))
    , m_fontSize(new QSpinBox(this))
{
    setObjectName(QStringLiteral("propertyPanel"));
    m_nameEdit->setObjectName(QStringLiteral("nameEdit"));
    m_equationEdit->setObjectName(QStringLiteral("equationEdit"));
    m_unitEdit->setObjectName(QStringLiteral("unitEdit"));
    m_commentEdit->setObjectName(QStringLiteral("commentEdit"));
    m_commentLabel->setObjectName(QStringLiteral("commentLabel"));
    m_fontCombo->setObjectName(QStringLiteral("fontCombo"));
    m_fontSize->setObjectName(QStringLiteral("fontSize"));
    m_fontSize->setRange(6, 96);

    QFormLayout *layout = new QFormLayout(this);
    layout->addRow(tr("Name"), m_nameEdit);
    layout->addRow(tr("Equation"), m_equationEdit);
    layout->addRow(tr("Unit"), m_unitEdit);
    layout->addRow(m_commentLabel, m_commentEdit);
    layout->addRow(tr("Font"), m_fontCombo);
    layout->addRow(tr("Size"), m_fontSize);

    connect(m_scene, &QGraphicsScene::selectionChanged, this, &PropertyPanel::refresh);
    connect(m_controller, &DocumentController::documentChanged, this, &PropertyPanel::refresh);
    connect(m_nameEdit, &QLineEdit::editingFinished, this, [this]() {
        if (!m_scene->selectedModelElement())
            return;
        if (!m_scene->renameSelected(m_nameEdit->text())) {
            QMessageBox::warning(this, tr("Invalid name"), tr("The name cannot be empty or duplicate another variable."));
            refresh();
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
    connect(m_fontCombo, &QFontComboBox::currentFontChanged, this, [this](const QFont &font) {
        QFont sized = font;
        sized.setPointSize(m_fontSize->value());
        commitFontStyle(sized);
    });
    connect(m_fontSize, &QSpinBox::editingFinished, this, [this]() {
        QFont font = m_fontCombo->currentFont();
        font.setPointSize(m_fontSize->value());
        commitFontStyle(font);
    });
    refresh();
}

bool PropertyPanel::commit()
{
    const QString name = m_nameEdit->text();
    const QString equation = m_equationEdit->text();
    const QString unit = m_unitEdit->text();
    const QString comment = m_commentEdit->text();
    QFont font = m_fontCombo->currentFont();
    font.setPointSize(m_fontSize->value());
    if (m_scene->selectedModelElement()) {
        if (!m_scene->renameSelected(name)) {
            QMessageBox::warning(this, tr("Invalid name"), tr("The name cannot be empty or duplicate another variable."));
            refresh();
            return false;
        }
        m_scene->setSelectedEquation(equation);
        m_controller->setUnit(m_scene->selectedModelElement()->id(), unit);
        m_controller->setComment(m_scene->selectedModelElement()->id(), comment);
    } else if (m_scene->selectedComment()) {
        m_controller->setCommentContent(m_scene->selectedComment()->id(), comment);
    }
    commitFontStyle(font);
    return true;
}

void PropertyPanel::refresh()
{
    const qds::core::ModelElement *variable = m_scene->selectedModelElement();
    const qds::core::Comment *comment = m_scene->selectedComment();
    const QSignalBlocker fontBlocker(m_fontCombo);
    const QSignalBlocker sizeBlocker(m_fontSize);
    m_fontCombo->setEnabled(variable || comment);
    m_fontSize->setEnabled(variable || comment);
    m_nameEdit->setEnabled(variable != nullptr);
    m_equationEdit->setEnabled(variable != nullptr);
    m_unitEdit->setEnabled(variable != nullptr);
    m_commentEdit->setEnabled(variable != nullptr || comment != nullptr);
    m_commentLabel->setText(comment ? tr("Content") : tr("Comment"));
    m_nameEdit->setText(variable ? QString::fromStdString(variable->displayName()) : QString());
    m_equationEdit->setText(variable ? QString::fromStdString(variable->equation()) : QString());
    QStringList completionWords;
    for (const qds::core::ModelElement &entry : m_controller->document().model().elements())
        completionWords.append(QString::fromStdString(entry.displayName()));
    m_equationEdit->setCompletionWords(completionWords);
    m_unitEdit->setText(variable ? QString::fromStdString(variable->unit()) : QString());
    m_commentEdit->setText(variable ? QString::fromStdString(variable->comment())
        : comment ? QString::fromStdString(comment->content()) : QString());
    QFont font = QApplication::font();
    const std::vector<qds::core::EntityId> selection = m_scene->selectedElementIds();
    if (selection.size() == 1) {
        const qds::core::FontStyle style = m_controller->document().diagram().fontStyle(selection.front());
        if (!style.family.empty())
            font.setFamily(QString::fromStdString(style.family));
        if (style.pointSize > 0)
            font.setPointSize(style.pointSize);
    }
    m_fontCombo->setCurrentFont(font);
    m_fontSize->setValue(font.pointSize());
}

void PropertyPanel::commitFontStyle(const QFont &font)
{
    const std::vector<qds::core::EntityId> selection = m_scene->selectedElementIds();
    if (selection.size() == 1)
        m_controller->setFontStyle(selection.front(), font);
}
