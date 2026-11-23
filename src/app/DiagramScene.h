#pragma once

#include <qds/core/Model.h>

#include <QGraphicsScene>
#include <QHash>

class VariableItem;

class DiagramScene : public QGraphicsScene
{
    Q_OBJECT

public:
    enum class Tool {
        Select,
        AddStock,
        AddFlow,
        AddAuxiliary,
        Connect
    };

    explicit DiagramScene(QObject *parent = nullptr);

    const qds::core::Model &model() const;
    void setModel(const qds::core::Model &model);
    void setTool(Tool tool);
    Tool tool() const;

    void removeSelected();
    bool renameSelected(const QString &newName);
    bool setSelectedEquation(const QString &equation);
    const qds::core::Variable *selectedVariable() const;

signals:
    void contentChanged();

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;

private:
    VariableItem *createVariableItem(const qds::core::Variable &variable);
    void createConnectionItem(const qds::core::Connection &connection);
    void updateVariablePosition(const QString &name, const QPointF &position);
    void updateConnections(const QString &name);
    void removeConnections(const QString &name);

    qds::core::Model m_model;
    QHash<QString, VariableItem *> m_items;
    Tool m_tool = Tool::Select;
    VariableItem *m_connectionSource = nullptr;
};
