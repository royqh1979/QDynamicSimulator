#pragma once

#include <qds/core/ChangeSet.h>
#include <qds/core/Document.h>

#include <QObject>
#include <QPointF>
#include <QUndoStack>

#include <functional>
#include <vector>

class DocumentController : public QObject
{
    Q_OBJECT

public:
    using Callback = std::function<void()>;

    explicit DocumentController(QObject *parent = nullptr);

    const qds::core::Document &document() const;
    QUndoStack *undoStack();

    qds::core::EntityId createVariable(qds::core::VariableType type, const QPointF &position);
    void removeVariables(const std::vector<qds::core::EntityId> &ids);
    bool renameVariable(qds::core::EntityId id, const QString &name);
    void setEquation(qds::core::EntityId id, const QString &equation);
    void moveVariables(const std::vector<std::pair<qds::core::EntityId, QPointF>> &positions);
    qds::core::ConnectionId createConnection(
        qds::core::EntityId from,
        qds::core::EntityId to,
        qds::core::ConnectionType type = qds::core::ConnectionType::Information);
    void removeConnections(const std::vector<qds::core::ConnectionId> &ids);
    void setRoute(qds::core::ConnectionId id, const qds::core::Route &route);
    void replaceDocument(qds::core::Document document);

signals:
    void documentChanged(const qds::core::ChangeSet &changeSet);

private:
    void pushCommand(const QString &text, const Callback &undo, const Callback &redo);
    void notify(const qds::core::ChangeSet &changeSet);

    qds::core::Document m_document;
    QUndoStack m_undoStack;
};
