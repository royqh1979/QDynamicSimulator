#pragma once

#include <qds/core/ChangeSet.h>
#include <qds/core/Document.h>

#include <QGraphicsScene>
#include <QHash>

class DocumentController;
class VariableItem;
class ConnectionItem;

class DiagramScene : public QGraphicsScene
{
    Q_OBJECT

public:
    enum class Tool {
        Select,
        AddStock,
        AddFlow,
        AddAuxiliary,
        AddConstant,
        AddExogenous,
        AddCloud,
        AddAnnotation,
        Connect,
        ConnectMaterial,
        ConnectAnnotation
    };

    explicit DiagramScene(DocumentController *controller, QObject *parent = nullptr);

    void setTool(Tool tool);
    Tool tool() const;

    void removeSelected();
    bool renameSelected(const QString &newName);
    bool setSelectedEquation(const QString &equation);
    const qds::core::Variable *selectedVariable() const;
    VariableItem *variableItem(qds::core::EntityId id) const;
    ConnectionItem *connectionItem(qds::core::ConnectionId id) const;

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;

private:
    void applyChangeSet(const qds::core::ChangeSet &changeSet);
    VariableItem *createVariableItem(qds::core::EntityId id);
    ConnectionItem *createConnectionItem(qds::core::ConnectionId id);
    void updateConnections(qds::core::EntityId id);
    void finishMove();

    DocumentController *m_controller;
    QHash<qds::core::EntityId, VariableItem *> m_entityItems;
    QHash<qds::core::ConnectionId, ConnectionItem *> m_connectionItems;
    Tool m_tool = Tool::Select;
    qds::core::EntityId m_connectionSource = 0;
};
