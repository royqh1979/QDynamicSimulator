#include "PropertyPanel.h"

#include "DiagramScene.h"
#include "DocumentController.h"
#include "EquationEditor.h"

#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>

PropertyPanel::PropertyPanel(DocumentController *controller, DiagramScene *scene, QWidget *parent)
    : QWidget(parent)
    , m_controller(controller)
    , m_scene(scene)
    , m_nameEdit(new QLineEdit(this))
    , m_equationEdit(new EquationEditor(this))
    , m_unitEdit(new QLineEdit(this))
    , m_commentEdit(new QLineEdit(this))
    , m_commentLabel(new QLabel(this))
{
    setObjectName(QStringLiteral("propertyPanel"));
    m_nameEdit->setObjectName(QStringLiteral("nameEdit"));
    m_equationEdit->setObjectName(QStringLiteral("equationEdit"));
    m_unitEdit->setObjectName(QStringLiteral("unitEdit"));
    m_commentEdit->setObjectName(QStringLiteral("commentEdit"));
    m_commentLabel->setObjectName(QStringLiteral("commentLabel"));

    QFormLayout *layout = new QFormLayout(this);
    layout->addRow(tr("Name"), m_nameEdit);
    layout->addRow(tr("Equation"), m_equationEdit);
    layout->addRow(tr("Unit"), m_unitEdit);
    layout->addRow(m_commentLabel, m_commentEdit);

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
    refresh();
}

bool PropertyPanel::commit()
{
    const QString name = m_nameEdit->text();
    const QString equation = m_equationEdit->text();
    const QString unit = m_unitEdit->text();
    const QString comment = m_commentEdit->text();
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
    return true;
}

void PropertyPanel::refresh()
{
    const qds::core::ModelElement *variable = m_scene->selectedModelElement();
    const qds::core::Comment *comment = m_scene->selectedComment();
    m_nameEdit->setEnabled(variable != nullptr);
    m_equationEdit->setEnabled(variable != nullptr);
    m_unitEdit->setEnabled(variable != nullptr);
    m_commentEdit->setEnabled(variable != nullptr || comment != nullptr);
    m_commentLabel->setText(comment ? tr("Content") : tr("Comment"));
    m_nameEdit->setText(variable ? QString::fromStdString(variable->displayName) : QString());
    m_equationEdit->setText(variable ? QString::fromStdString(variable->equation) : QString());
    QStringList completionWords;
    for (const qds::core::ModelElement &entry : m_controller->document().model().elements())
        completionWords.append(QString::fromStdString(entry.displayName));
    m_equationEdit->setCompletionWords(completionWords);
    m_unitEdit->setText(variable ? QString::fromStdString(variable->unit) : QString());
    m_commentEdit->setText(variable ? QString::fromStdString(variable->comment)
        : comment ? QString::fromStdString(comment->content) : QString());
}
