#pragma once

#include <qds/core/ChangeSet.h>
#include <qds/core/Document.h>

#include <QObject>
#include <QPointF>
#include <QUndoStack>

#include <optional>
#include <vector>

struct ClipboardData {
    std::vector<qds::core::ElementData> elements;

    bool empty() const { return elements.empty(); }
};

class QFont;

class DocumentController : public QObject
{
    Q_OBJECT

public:
    explicit DocumentController(QObject *parent = nullptr);

    const qds::core::Document &document() const;
    QUndoStack *undoStack();

    qds::core::EntityId createVariable(qds::core::ElementKind kind, const QPointF &position);
    qds::core::EntityId createFlow(qds::core::EntityId from, qds::core::EntityId to);
    qds::core::EntityId createFlow(
        qds::core::EntityId from,
        qds::core::EntityId to,
        std::optional<QPointF> fromPosition,
        std::optional<QPointF> toPosition);
    qds::core::EntityId createConnector(qds::core::EntityId from, qds::core::EntityId to);
    qds::core::EntityId createCloud(const QPointF &position);
    qds::core::EntityId createComment(const QString &text, const QPointF &position);
    void removeElements(const std::vector<qds::core::EntityId> &ids);
    bool renameElement(qds::core::EntityId id, const QString &name);
    void setEquation(qds::core::EntityId id, const QString &equation);
    void setUnit(qds::core::EntityId id, const QString &unit);
    void setComment(qds::core::EntityId id, const QString &comment);
    void setCommentContent(qds::core::EntityId id, const QString &content);
    void setFontStyle(qds::core::EntityId id, const QFont &font);
    void moveElements(const std::vector<std::pair<qds::core::EntityId, QPointF>> &positions);
    bool reconnectFlow(qds::core::EntityId id, qds::core::EntityId from, qds::core::EntityId to);
    bool reconnectConnector(qds::core::EntityId id, qds::core::EntityId from, qds::core::EntityId to);
    void setRoute(qds::core::EntityId id, const qds::core::Route &route);
    void replaceDocument(qds::core::Document document);
    ClipboardData copyElements(const std::vector<qds::core::EntityId> &ids) const;
    std::vector<qds::core::EntityId> pasteClipboard(const ClipboardData &clipboard, const QPointF &offset);

signals:
    void documentChanged(const qds::core::ChangeNotification &notification);

private:
    void pushChanges(const qds::core::ChangeSet &changes, bool alreadyApplied = false);
    void applyChanges(const qds::core::ChangeSet &changes, bool after);
    void notify(const qds::core::ChangeNotification &notification);

    qds::core::Document m_document;
    QUndoStack m_undoStack;
};
