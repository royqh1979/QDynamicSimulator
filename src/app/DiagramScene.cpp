#include "DiagramScene.h"

#include "DiagramItems.h"

#include <QGraphicsSceneMouseEvent>

DiagramScene::DiagramScene(QObject *parent)
    : QGraphicsScene(parent)
{
    setSceneRect(-3000.0, -3000.0, 6000.0, 6000.0);
}

const qds::core::Model &DiagramScene::model() const
{
    return m_model;
}

void DiagramScene::setModel(const qds::core::Model &model)
{
    clear();
    m_items.clear();
    m_connectionSource = nullptr;
    m_model = model;

    for (const qds::core::Variable &variable : m_model.variables())
        createVariableItem(variable);
    for (const qds::core::Connection &connection : m_model.connections())
        createConnectionItem(connection);
}

void DiagramScene::setTool(Tool tool)
{
    m_tool = tool;
    m_connectionSource = nullptr;
}

DiagramScene::Tool DiagramScene::tool() const
{
    return m_tool;
}

void DiagramScene::removeSelected()
{
    const QList<QGraphicsItem *> selection = selectedItems();
    QVector<VariableItem *> variables;
    for (QGraphicsItem *item : selection) {
        if (VariableItem *variable = qgraphicsitem_cast<VariableItem *>(item))
            variables.append(variable);
    }

    for (VariableItem *item : variables) {
        const QString name = item->name();
        removeConnections(name);
        m_items.remove(name);
        m_model.removeVariable(name.toStdString());
        delete item;
    }
    if (!variables.isEmpty())
        emit contentChanged();
}

bool DiagramScene::renameSelected(const QString &newName)
{
    VariableItem *item = qgraphicsitem_cast<VariableItem *>(selectedItems().value(0));
    if (!item)
        return false;
    const QString oldName = item->name();
    const QString name = newName.trimmed();
    if (oldName == name)
        return true;
    if (!m_model.renameVariable(oldName.toStdString(), name.toStdString()))
        return false;

    m_items.remove(oldName);
    item->setName(name);
    m_items.insert(name, item);
    emit contentChanged();
    return true;
}

bool DiagramScene::setSelectedEquation(const QString &equation)
{
    VariableItem *item = qgraphicsitem_cast<VariableItem *>(selectedItems().value(0));
    if (!item)
        return false;
    const std::string name = item->name().toStdString();
    const qds::core::Variable *variable = m_model.variable(name);
    if (!variable || variable->equation == equation.toStdString())
        return true;
    const bool changed = m_model.setEquation(name, equation.toStdString());
    if (changed)
        emit contentChanged();
    return changed;
}

const qds::core::Variable *DiagramScene::selectedVariable() const
{
    VariableItem *item = qgraphicsitem_cast<VariableItem *>(selectedItems().value(0));
    return item ? m_model.variable(item->name().toStdString()) : nullptr;
}

void DiagramScene::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    if (event->button() == Qt::RightButton && m_tool == Tool::Connect) {
        m_connectionSource = nullptr;
        clearSelection();
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && m_tool != Tool::Select && m_tool != Tool::Connect) {
        qds::core::VariableType type = qds::core::VariableType::Stock;
        if (m_tool == Tool::AddFlow)
            type = qds::core::VariableType::Flow;
        else if (m_tool == Tool::AddAuxiliary)
            type = qds::core::VariableType::Auxiliary;

        const QPointF position = event->scenePos();
        const std::string name = m_model.createVariable(type, position.x(), position.y());
        VariableItem *item = createVariableItem(*m_model.variable(name));
        clearSelection();
        item->setSelected(true);
        emit contentChanged();
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && m_tool == Tool::Connect) {
        VariableItem *item = qgraphicsitem_cast<VariableItem *>(itemAt(event->scenePos(), QTransform()));
        if (!item) {
            m_connectionSource = nullptr;
            clearSelection();
            event->accept();
            return;
        }
        if (!m_connectionSource) {
            m_connectionSource = item;
            clearSelection();
            item->setSelected(true);
        } else if (m_connectionSource != item) {
            const qds::core::Connection connection {
                m_connectionSource->name().toStdString(), item->name().toStdString()};
            if (m_model.addConnection(connection.from, connection.to))
                createConnectionItem(connection);
            m_connectionSource = nullptr;
            clearSelection();
            item->setSelected(true);
            emit contentChanged();
        }
        event->accept();
        return;
    }

    QGraphicsScene::mousePressEvent(event);
}

VariableItem *DiagramScene::createVariableItem(const qds::core::Variable &variable)
{
    const QString name = QString::fromStdString(variable.name);
    VariableItem *item = new VariableItem(name, variable.type);
    item->setPos(variable.x, variable.y);
    addItem(item);
    m_items.insert(name, item);
    connect(item, &VariableItem::positionChanged, this, &DiagramScene::updateVariablePosition);
    return item;
}

void DiagramScene::createConnectionItem(const qds::core::Connection &connection)
{
    VariableItem *from = m_items.value(QString::fromStdString(connection.from));
    VariableItem *to = m_items.value(QString::fromStdString(connection.to));
    if (from && to)
        addItem(new ConnectionItem(from, to));
}

void DiagramScene::updateVariablePosition(const QString &name, const QPointF &position)
{
    m_model.setPosition(name.toStdString(), position.x(), position.y());
    updateConnections(name);
    emit contentChanged();
}

void DiagramScene::updateConnections(const QString &name)
{
    const QList<QGraphicsItem *> allItems = items();
    for (QGraphicsItem *item : allItems) {
        if (ConnectionItem *connection = qgraphicsitem_cast<ConnectionItem *>(item)) {
            if (connection->connects(name))
                connection->updatePosition();
        }
    }
}

void DiagramScene::removeConnections(const QString &name)
{
    const QList<QGraphicsItem *> allItems = items();
    for (QGraphicsItem *item : allItems) {
        if (ConnectionItem *connection = qgraphicsitem_cast<ConnectionItem *>(item)) {
            if (connection->connects(name))
                delete connection;
        }
    }
}
