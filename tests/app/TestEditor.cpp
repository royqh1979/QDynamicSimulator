#include "DiagramItems.h"
#include "DiagramScene.h"
#include "DocumentController.h"

#include <QtTest>

using namespace qds::core;

class TestEditor : public QObject
{
    Q_OBJECT

private slots:
    void createUndoRedo();
    void renameAndEquationUndo();
    void moveUndoRedo();
    void connectionAndCascadeRestore();
    void routeUndoRedo();
    void replaceDocumentResetsScene();
    void connectionTypeValidation();
};

void TestEditor::createUndoRedo()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    const EntityId id = controller.createVariable(VariableType::Stock, QPointF(10.0, 20.0));
    QVERIFY(scene.variableItem(id));
    QCOMPARE(controller.document().diagram().position(id), Vec2({10.0, 20.0}));
    controller.undoStack()->undo();
    QVERIFY(controller.document().variable(id) == nullptr);
    QVERIFY(scene.variableItem(id) == nullptr);
    controller.undoStack()->redo();
    QVERIFY(scene.variableItem(id));
    QCOMPARE(controller.document().variable(id)->id, id);
}

void TestEditor::renameAndEquationUndo()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    const EntityId id = controller.createVariable(VariableType::Stock, QPointF());
    QVERIFY(controller.renameVariable(id, QStringLiteral("Population")));
    controller.setEquation(id, QStringLiteral("100"));
    QCOMPARE(scene.variableItem(id)->name(), QStringLiteral("Population"));
    controller.undoStack()->undo();
    QCOMPARE(controller.document().variable(id)->equation, std::string());
    controller.undoStack()->undo();
    QCOMPARE(controller.document().variable(id)->name, std::string("Stock"));
    controller.undoStack()->redo();
    controller.undoStack()->redo();
    QCOMPARE(controller.document().variable(id)->equation, std::string("100"));
    QVERIFY(!controller.renameVariable(id, QString()));
}

void TestEditor::moveUndoRedo()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    const EntityId first = controller.createVariable(VariableType::Stock, QPointF(0.0, 0.0));
    const EntityId second = controller.createVariable(VariableType::Auxiliary, QPointF(10.0, 10.0));
    controller.moveVariables({{first, QPointF(50.0, 60.0)}, {second, QPointF(70.0, 80.0)}});
    QCOMPARE(scene.variableItem(first)->pos(), QPointF(50.0, 60.0));
    QCOMPARE(scene.variableItem(second)->pos(), QPointF(70.0, 80.0));
    QCOMPARE(controller.undoStack()->count(), 3);
    controller.undoStack()->undo();
    QCOMPARE(scene.variableItem(first)->pos(), QPointF(0.0, 0.0));
    QCOMPARE(scene.variableItem(second)->pos(), QPointF(10.0, 10.0));
    controller.undoStack()->redo();
    QCOMPARE(scene.variableItem(first)->pos(), QPointF(50.0, 60.0));
}

void TestEditor::connectionAndCascadeRestore()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    const EntityId stock = controller.createVariable(VariableType::Stock, QPointF(0.0, 0.0));
    const EntityId flow = controller.createVariable(VariableType::Flow, QPointF(200.0, 0.0));
    const ConnectionId connection = controller.createConnection(flow, stock);
    QVERIFY(scene.connectionItem(connection));
    QCOMPARE(scene.connectionItem(connection)->from(), scene.variableItem(flow));
    controller.removeVariables({stock});
    QVERIFY(scene.variableItem(stock) == nullptr);
    QVERIFY(scene.connectionItem(connection) == nullptr);
    controller.undoStack()->undo();
    QVERIFY(scene.variableItem(stock));
    QVERIFY(scene.connectionItem(connection));
    QCOMPARE(controller.document().connection(connection)->from, flow);
    controller.undoStack()->redo();
    QVERIFY(scene.connectionItem(connection) == nullptr);
}

void TestEditor::routeUndoRedo()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    const EntityId stock = controller.createVariable(VariableType::Stock, QPointF());
    const EntityId flow = controller.createVariable(VariableType::Flow, QPointF(100.0, 0.0));
    const ConnectionId id = controller.createConnection(flow, stock);
    Route route;
    route.manual = true;
    route.controlPoints = {{50.0, 50.0}};
    controller.setRoute(id, route);
    QCOMPARE(controller.document().diagram().route(id), route);
    controller.undoStack()->undo();
    QCOMPARE(controller.document().diagram().route(id), Route());
    controller.undoStack()->redo();
    QCOMPARE(controller.document().diagram().route(id), route);
}

void TestEditor::replaceDocumentResetsScene()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    controller.createVariable(VariableType::Stock, QPointF());
    Document replacement;
    const EntityId id = replacement.createVariable(VariableType::Auxiliary, {30.0, 40.0});
    controller.replaceDocument(replacement);
    QCOMPARE(scene.items().size(), 1);
    QVERIFY(scene.variableItem(id));
    QCOMPARE(controller.undoStack()->count(), 0);
    QVERIFY(controller.undoStack()->isClean());
}

void TestEditor::connectionTypeValidation()
{
    DocumentController controller;
    DiagramScene scene(&controller);
    const EntityId stock = controller.createVariable(VariableType::Stock, QPointF());
    const EntityId auxiliary = controller.createVariable(VariableType::Auxiliary, QPointF(100.0, 0.0));
    const int before = controller.undoStack()->count();
    QCOMPARE(controller.createConnection(auxiliary, stock, ConnectionType::MaterialFlow), ConnectionId(0));
    QCOMPARE(controller.undoStack()->count(), before);
    const ConnectionId information = controller.createConnection(stock, auxiliary, ConnectionType::Information);
    QVERIFY(information != 0);
    QVERIFY(scene.connectionItem(information));
}

QTEST_MAIN(TestEditor)
#include "TestEditor.moc"
