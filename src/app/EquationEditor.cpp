#include "EquationEditor.h"

#include <QAbstractItemView>
#include <QCompleter>
#include <QKeyEvent>
#include <QRegularExpression>
#include <QScrollBar>
#include <QStringListModel>
#include <QTextCharFormat>

EquationHighlighter::EquationHighlighter(QTextDocument *document)
    : QSyntaxHighlighter(document)
{
}

void EquationHighlighter::highlightBlock(const QString &text)
{
    QTextCharFormat numberFormat;
    numberFormat.setForeground(QColor(160, 80, 20));
    QTextCharFormat functionFormat;
    functionFormat.setForeground(QColor(40, 100, 180));
    functionFormat.setFontWeight(QFont::Bold);
    QTextCharFormat operatorFormat;
    operatorFormat.setForeground(QColor(150, 45, 130));

    const QRegularExpression numberPattern(QStringLiteral("\\b\\d+(?:\\.\\d+)?\\b"));
    QRegularExpressionMatchIterator numbers = numberPattern.globalMatch(text);
    while (numbers.hasNext()) {
        const QRegularExpressionMatch match = numbers.next();
        setFormat(match.capturedStart(), match.capturedLength(), numberFormat);
    }
    const QRegularExpression functionPattern(QStringLiteral("\\b[A-Za-z_][A-Za-z0-9_]*(?=\\s*\\()"));
    QRegularExpressionMatchIterator functions = functionPattern.globalMatch(text);
    while (functions.hasNext()) {
        const QRegularExpressionMatch match = functions.next();
        setFormat(match.capturedStart(), match.capturedLength(), functionFormat);
    }
    const QRegularExpression operatorPattern(QStringLiteral("[+\\-*/^=<>]+"));
    QRegularExpressionMatchIterator operators = operatorPattern.globalMatch(text);
    while (operators.hasNext()) {
        const QRegularExpressionMatch match = operators.next();
        setFormat(match.capturedStart(), match.capturedLength(), operatorFormat);
    }
}

EquationEditor::EquationEditor(QWidget *parent)
    : QPlainTextEdit(parent)
    , m_completer(new QCompleter(this))
    , m_highlighter(new EquationHighlighter(document()))
{
    setLineWrapMode(NoWrap);
    setFixedHeight(32);
    setTabChangesFocus(true);
    m_completer->setWidget(this);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_completer->setCompletionMode(QCompleter::PopupCompletion);
    m_completer->setModel(new QStringListModel(QStringList {
        QStringLiteral("ABS"), QStringLiteral("DELAY"), QStringLiteral("EXP"), QStringLiteral("INTEG"),
        QStringLiteral("LN"), QStringLiteral("LOOKUP"), QStringLiteral("MAX"), QStringLiteral("MIN"),
        QStringLiteral("PULSE"), QStringLiteral("RAMP"), QStringLiteral("SMOOTH"), QStringLiteral("STEP")}, m_completer));
    connect(m_completer, QOverload<const QString &>::of(&QCompleter::activated), this, [this](const QString &completion) {
        QTextCursor cursor = textCursor();
        cursor.movePosition(QTextCursor::StartOfWord, QTextCursor::KeepAnchor);
        cursor.insertText(completion);
        setTextCursor(cursor);
    });
}

QString EquationEditor::text() const
{
    return toPlainText();
}

void EquationEditor::setText(const QString &text)
{
    setPlainText(text);
}

void EquationEditor::setCompletionWords(const QStringList &words)
{
    QStringList functions {
        QStringLiteral("ABS"), QStringLiteral("DELAY"), QStringLiteral("EXP"), QStringLiteral("INTEG"),
        QStringLiteral("LN"), QStringLiteral("LOOKUP"), QStringLiteral("MAX"), QStringLiteral("MIN"),
        QStringLiteral("PULSE"), QStringLiteral("RAMP"), QStringLiteral("SMOOTH"), QStringLiteral("STEP")};
    functions.append(words);
    functions.removeDuplicates();
    m_completer->setModel(new QStringListModel(functions, m_completer));
}

void EquationEditor::focusOutEvent(QFocusEvent *event)
{
    QPlainTextEdit::focusOutEvent(event);
    if (!m_completer->popup()->isVisible())
        emit editingFinished();
}

void EquationEditor::keyPressEvent(QKeyEvent *event)
{
    if (m_completer->popup()->isVisible() && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter
            || event->key() == Qt::Key_Escape || event->key() == Qt::Key_Tab)) {
        QPlainTextEdit::keyPressEvent(event);
        return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        emit editingFinished();
        event->accept();
        return;
    }
    QPlainTextEdit::keyPressEvent(event);
    complete();
}

void EquationEditor::complete()
{
    if (textCursor().position() == 0) {
        m_completer->popup()->hide();
        return;
    }
    m_completer->setCompletionPrefix(textCursor().block().text().left(textCursor().positionInBlock()));
    const QString word = textCursor().block().text().left(textCursor().positionInBlock()).section(
        QRegularExpression(QStringLiteral("[^A-Za-z0-9_]")), -1);
    m_completer->setCompletionPrefix(word);
    if (word.isEmpty()) {
        m_completer->popup()->hide();
        return;
    }
    const QRect cursor = cursorRect();
    m_completer->popup()->setFixedWidth(qMax(160, m_completer->popup()->sizeHintForColumn(0) + 20));
    m_completer->complete(QRect(cursor.bottomLeft(), QSize(160, 20)));
}
