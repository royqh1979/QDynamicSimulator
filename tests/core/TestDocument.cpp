#include <qds/core/Document.h>

#include <QtTest>

using namespace qds::core;

class TestDocument : public QObject
{
    Q_OBJECT

private slots:
    void createAndLookup();
    void rejectInvalidOperations();
    void removeCascades();
    void restoreWithStableIds();
    void routeAndPositionState();
    void clearDocument();
    void entityAndConnectionTypes();
};

void TestDocument::createAndLookup()
{
    Document document;
    const EntityId stock = document.createVariable(VariableType::Stock, {10.0, 20.0});
    const EntityId stock2 = document.createVariable(VariableType::Stock, {30.0, 40.0});
    QVERIFY(stock != 0);
    QVERIFY(stock2 != 0);
    QVERIFY(stock != stock2);
    QCOMPARE(document.model().variables().size(), std::size_t(2));
    QCOMPARE(document.variable(stock)->name, std::string("Stock"));
    QCOMPARE(document.variable(stock2)->name, std::string("Stock 2"));
    QCOMPARE(document.diagram().position(stock), Vec2({10.0, 20.0}));
    QVERIFY(document.renameVariable(stock, "Population"));
    QCOMPARE(document.variable(stock)->name, std::string("Population"));
    QCOMPARE(document.variable(stock)->id, stock);
    QVERIFY(document.setEquation(stock, "100"));
    QCOMPARE(document.variable(stock)->equation, std::string("100"));
}

void TestDocument::rejectInvalidOperations()
{
    Document document;
    const EntityId stock = document.createVariable(VariableType::Stock);
    const EntityId flow = document.createVariable(VariableType::Flow);
    QVERIFY(!document.renameVariable(stock, " "));
    QVERIFY(!document.renameVariable(stock, "Flow"));
    QVERIFY(!document.renameVariable(9999, "Valid"));
    QVERIFY(!document.setEquation(9999, "1"));
    QVERIFY(!document.setPosition(9999, {1.0, 2.0}));
    QCOMPARE(document.createConnection(stock, stock), ConnectionId(0));
    QCOMPARE(document.createConnection(stock, 9999), ConnectionId(0));
    const ConnectionId connection = document.createConnection(stock, flow);
    QVERIFY(connection != 0);
    QCOMPARE(document.createConnection(stock, flow), ConnectionId(0));
    QVERIFY(!document.setRoute(9999, Route {}));
}

void TestDocument::removeCascades()
{
    Document document;
    const EntityId stock = document.createVariable(VariableType::Stock);
    const EntityId flow = document.createVariable(VariableType::Flow);
    const ConnectionId connection = document.createConnection(flow, stock);
    QVERIFY(document.removeVariable(stock));
    QVERIFY(document.variable(stock) == nullptr);
    QVERIFY(document.connection(connection) == nullptr);
    QVERIFY(document.diagram().routes().empty());
    QVERIFY(!document.removeVariable(stock));
    QVERIFY(!document.removeConnection(connection));
}

void TestDocument::restoreWithStableIds()
{
    Document document;
    const EntityId stock = document.createVariable(VariableType::Stock, {15.0, 25.0});
    const EntityId flow = document.createVariable(VariableType::Flow, {100.0, 25.0});
    const Connection connection {0, flow, stock};
    const ConnectionId connectionId = document.addConnection(connection);
    const Variable stockSnapshot = *document.variable(stock);
    const Connection connectionSnapshot = *document.connection(connectionId);
    Route route;
    route.manual = true;
    route.controlPoints = {{50.0, 25.0}};
    QVERIFY(document.setRoute(connectionId, route));

    QVERIFY(document.removeVariable(stock));
    QCOMPARE(document.addVariable(stockSnapshot, {15.0, 25.0}), stock);
    QCOMPARE(document.addConnection(connectionSnapshot), connectionId);
    QVERIFY(document.setRoute(connectionId, route));
    QCOMPARE(document.variable(stock)->name, stockSnapshot.name);
    QCOMPARE(document.diagram().position(stock), Vec2({15.0, 25.0}));
    QCOMPARE(document.diagram().route(connectionId), route);
}

void TestDocument::routeAndPositionState()
{
    Document document;
    const EntityId stock = document.createVariable(VariableType::Stock);
    const EntityId flow = document.createVariable(VariableType::Flow);
    const ConnectionId connection = document.createConnection(flow, stock);
    Route route;
    route.mode = RouteMode::Bezier;
    route.manual = true;
    route.locked = true;
    route.controlPoints = {{1.0, 2.0}, {3.0, 4.0}};
    QVERIFY(document.setPosition(stock, {99.0, 100.0}));
    QVERIFY(document.setRoute(connection, route));
    QCOMPARE(document.diagram().position(stock), Vec2({99.0, 100.0}));
    QCOMPARE(document.diagram().route(connection), route);
}

void TestDocument::clearDocument()
{
    Document document;
    const EntityId stock = document.createVariable(VariableType::Stock);
    document.createVariable(VariableType::Flow);
    document.createConnection(stock, 2);
    document.clear();
    QVERIFY(document.model().variables().empty());
    QVERIFY(document.model().connections().empty());
    QVERIFY(document.diagram().positions().empty());
    QVERIFY(document.diagram().routes().empty());
    QCOMPARE(document.createVariable(VariableType::Stock), EntityId(1));
}

void TestDocument::entityAndConnectionTypes()
{
    Document document;
    const EntityId stock = document.createVariable(VariableType::Stock);
    const EntityId flow = document.createVariable(VariableType::Flow);
    const EntityId auxiliary = document.createVariable(VariableType::Auxiliary);
    const EntityId constant = document.createVariable(VariableType::Constant);
    const EntityId exogenous = document.createVariable(VariableType::Exogenous);
    const EntityId cloud = document.createVariable(VariableType::Cloud);
    const EntityId note = document.createVariable(VariableType::Annotation);
    QVERIFY(constant && exogenous && cloud && note);
    const ConnectionId material = document.createConnection(flow, stock, ConnectionType::MaterialFlow);
    QVERIFY(material != 0);
    QVERIFY(document.createConnection(cloud, flow, ConnectionType::MaterialFlow) != 0);
    QCOMPARE(document.createConnection(auxiliary, stock, ConnectionType::MaterialFlow), ConnectionId(0));
    QVERIFY(document.createConnection(stock, auxiliary, ConnectionType::Information) != 0);
    QCOMPARE(document.createConnection(cloud, auxiliary, ConnectionType::Information), ConnectionId(0));
    QVERIFY(document.createConnection(note, stock, ConnectionType::Annotation) != 0);
    QCOMPARE(document.createConnection(stock, auxiliary, ConnectionType::Annotation), ConnectionId(0));
    QCOMPARE(document.connection(material)->type, ConnectionType::MaterialFlow);
}

QTEST_APPLESS_MAIN(TestDocument)
#include "TestDocument.moc"
