#pragma once

#include <QPlainTextEdit>
#include <QSyntaxHighlighter>

class QCompleter;

class EquationHighlighter : public QSyntaxHighlighter
{
public:
    explicit EquationHighlighter(QTextDocument *document);

protected:
    void highlightBlock(const QString &text) override;
};

class EquationEditor : public QPlainTextEdit
{
    Q_OBJECT

public:
    explicit EquationEditor(QWidget *parent = nullptr);
    QString text() const;
    void setText(const QString &text);
    void setCompletionWords(const QStringList &words);

signals:
    void editingFinished();

protected:
    void focusOutEvent(QFocusEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void complete();

    QCompleter *m_completer;
    EquationHighlighter *m_highlighter;
};
