#include <core/Document.h>
#include <core/Validation.h>

#include <QVector>
#include <QtTest>

using namespace qds::core;

class TestDocument : public QObject
{
    Q_OBJECT

private slots:
    void createAndLookup();
    void namingNormalization();
    void generatedNamesUseNameKeyIndex();
    void rejectInvalidOperations();
    void removeCascades();
    void restoreWithStableIds();
    void routeAndPositionState();
    void fontStyleState();
    void clearDocument();
    void entityAndConnectorTypes();
    void reconnectRoutes();
    void metadata();
    void applyFailureIsAtomic();
    void validation();
};

void TestDocument::createAndLookup()
{
    Document document;
    const EntityId stock = document.createVariable(ElementKind::Stock, {10.0, 20.0});
    const EntityId stock2 = document.createVariable(ElementKind::Stock, {30.0, 40.0});
    const EntityId cloud = document.createCloud({100.0, 20.0});
    const EntityId flow = document.createFlow(stock, cloud);
    const EntityId connector = document.createConnector(stock, flow);
    QVERIFY(stock != 0 && stock2 != 0 && stock != stock2);
    QCOMPARE(flow, EntityId(4));
    QCOMPARE(connector, EntityId(5));
    QCOMPARE(document.find(stock)->kind(), ElementKind::Stock);
    QCOMPARE(document.find(flow)->kind(), ElementKind::Flow);
    QCOMPARE(document.find(connector)->kind(), ElementKind::Connector);
    QCOMPARE(document.diagram().position(stock), Vec2({10.0, 20.0}));
    QVERIFY(document.rename(stock, "Population"));
    QCOMPARE(document.model().findByName("population")->id(), stock);
    QCOMPARE(document.model().findByName("POPULATION")->id(), stock);
    QVERIFY(document.setEquation(stock, "100"));
    QCOMPARE(static_cast<const ModelElement *>(document.find(stock))->equation(), QString("100"));
    std::size_t modelCount = 0;
    for (const ModelElement &element : document.model().elements()) {
        Q_UNUSED(element)
        ++modelCount;
    }
    QCOMPARE(modelCount, std::size_t(3));
}

void TestDocument::namingNormalization()
{
    Document document;
    const EntityId stock = document.createVariable(ElementKind::Stock);
    const EntityId cloud = document.createCloud();
    const EntityId flow = document.createFlow(stock, cloud);
    QVERIFY(!document.rename(stock, "flow1"));
    QVERIFY(document.rename(flow, "\u6D41\u5165 \u901F\u7387"));
    QCOMPARE(document.model().findByName("\u6D41\u5165_\u901F\u7387")->id(), flow);
    QVERIFY(document.rename(stock, "\u5E93\u5B58"));
    QVERIFY(document.rename(stock, "\u5E93\u5B58"));
    QVERIFY(!document.rename(stock, "\u6D41\u5165_\u901F\u7387"));

    const EntityId auxiliary = document.createVariable(ElementKind::Auxiliary);
    const EntityId second = document.createVariable(ElementKind::Auxiliary);
    QVERIFY(document.rename(auxiliary, "birth rate"));
    QVERIFY(!document.rename(second, "birth_rate"));
    QVERIFY(document.setEquation(auxiliary, "\"\u6D41\u5165 \u901F\u7387\" * 2"));
    QVERIFY(document.rename(flow, "\u6D41\u5165_\u901F\u7387"));
    QCOMPARE(static_cast<const ModelElement *>(document.find(auxiliary))->equation(),
             QString("\u6D41\u5165_\u901F\u7387 * 2"));
    QVERIFY(document.rename(auxiliary, "BIRTH RATE"));
    QCOMPARE(static_cast<const ModelElement *>(document.find(auxiliary))->displayName(), QString("BIRTH RATE"));
    const EntityId comment = document.createComment("\u5E93\u5B58");
    QVERIFY(document.find(comment));
    QVERIFY(document.rename(stock, "Population"));
    QCOMPARE(static_cast<const ModelElement *>(document.find(auxiliary))->equation(),
             QString("\u6D41\u5165_\u901F\u7387 * 2"));
}

void TestDocument::generatedNamesUseNameKeyIndex()
{
    Document document;
    const EntityId firstStock = document.createVariable(ElementKind::Stock);
    const EntityId secondStock = document.createVariable(ElementKind::Stock);
    const EntityId auxiliary = document.createVariable(ElementKind::Auxiliary);
    QCOMPARE(static_cast<const ModelElement *>(document.find(firstStock))->displayName(), QString("Stock1"));
    QCOMPARE(static_cast<const ModelElement *>(document.find(secondStock))->displayName(), QString("Stock2"));
    QCOMPARE(static_cast<const ModelElement *>(document.find(auxiliary))->displayName(), QString("Auxiliary1"));
    QCOMPARE(static_cast<const ModelElement *>(document.find(firstStock))->nameKey(), QString("stock1"));

    QVERIFY(document.rename(auxiliary, "Stock3"));
    const EntityId thirdStock = document.createVariable(ElementKind::Stock);
    QCOMPARE(static_cast<const ModelElement *>(document.find(thirdStock))->displayName(), QString("Stock4"));
    QVERIFY(document.model().findByName("stock4"));
    QCOMPARE(document.model().findByName("stock3")->id(), auxiliary);

    QVERIFY(document.remove(secondStock).size() == 1);
    QVERIFY(!document.model().findByName("Stock2"));
    QVERIFY(document.restore(*document.capture(firstStock)) == false);
}

void TestDocument::rejectInvalidOperations()
{
    Document document;
    const EntityId stock = document.createVariable(ElementKind::Stock);
    const EntityId auxiliary = document.createVariable(ElementKind::Auxiliary);
    QCOMPARE(document.createVariable(ElementKind::Flow), EntityId(0));
    QCOMPARE(document.createFlow(auxiliary, stock), EntityId(0));
    QCOMPARE(document.createFlow(stock, stock), EntityId(0));
    QCOMPARE(document.createFlow(0, 0), EntityId(0));
    QCOMPARE(document.createFlow(stock, 0), EntityId(0));
    QCOMPARE(document.createConnector(stock, stock), EntityId(0));
    QCOMPARE(document.createConnector(stock, 9999), EntityId(0));
    QVERIFY(!document.rename(stock, " "));
    QVERIFY(!document.rename(9999, "Valid"));
    QVERIFY(!document.setEquation(9999, "1"));
    QVERIFY(!document.setPosition(9999, {1.0, 2.0}));
    QVERIFY(!document.setRoute(9999, Route {}));
}

void TestDocument::removeCascades()
{
    Document document;
    const EntityId stock = document.createVariable(ElementKind::Stock);
    const EntityId cloud = document.createCloud();
    const EntityId flow = document.createFlow(stock, cloud);
    const EntityId auxiliary = document.createVariable(ElementKind::Auxiliary);
    const EntityId connector = document.createConnector(auxiliary, flow);
    const QVector<EntityId> removed = document.remove(stock);
    QVERIFY(document.find(stock) == nullptr);
    QVERIFY(document.find(flow) == nullptr);
    QVERIFY(document.find(connector) == nullptr);
    QVERIFY(document.find(auxiliary));
    QVERIFY(document.find(cloud));
    QCOMPARE(removed.front(), connector);
    QVERIFY(document.diagram().routes().empty());
    QVERIFY(document.remove(stock).empty());
}

void TestDocument::restoreWithStableIds()
{
    Document document;
    const EntityId stock = document.createVariable(ElementKind::Stock, {15.0, 25.0});
    const EntityId cloud = document.createCloud({200.0, 25.0});
    const EntityId flow = document.createFlow(stock, cloud);
    const EntityId auxiliary = document.createVariable(ElementKind::Auxiliary);
    const EntityId connector = document.createConnector(auxiliary, flow);
    Route route;
    route.mode = RouteMode::Bezier;
    route.manual = true;
    route.controlPoints = {{0.25, 25.0}, {0.75, 50.0}};
    QVERIFY(document.setRoute(flow, route));
    std::unordered_map<EntityId, ElementData> records;
    for (EntityId id : {stock, flow, connector})
        records[id] = *document.capture(id);

    const auto removed = document.remove(stock);
    for (auto it = removed.rbegin(); it != removed.rend(); ++it)
        QVERIFY(document.restore(records[*it]));
    QCOMPARE(document.find(flow)->id(), flow);
    QCOMPARE(static_cast<const Flow *>(document.find(flow))->endpoints().from, stock);
    QCOMPARE(static_cast<const Connector *>(document.find(connector))->to(), flow);
    QCOMPARE(document.diagram().route(flow), route);
    const EntityId next = document.createVariable(ElementKind::Stock);
    QVERIFY(next > connector);
}

void TestDocument::routeAndPositionState()
{
    Document document;
    const EntityId stock = document.createVariable(ElementKind::Stock);
    const EntityId cloud = document.createCloud({200.0, 0.0});
    const EntityId flow = document.createFlow(stock, cloud);
    QCOMPARE(document.diagram().route(flow).mode, RouteMode::Straight);
    const EntityId auxiliary = document.createVariable(ElementKind::Auxiliary);
    const EntityId connector = document.createConnector(auxiliary, flow);
    QCOMPARE(document.diagram().route(connector), defaultConnectorRoute());
    Route route;
    route.mode = RouteMode::Bezier;
    route.manual = true;
    route.locked = true;
    route.controlPoints = {{0.25, 2.0}, {0.75, 4.0}};
    QVERIFY(document.setPosition(stock, {99.0, 100.0}));
    QCOMPARE(document.diagram().position(flow), Vec2({149.5, 50.0}));
    QVERIFY(document.setRoute(flow, route));
    QCOMPARE(document.diagram().route(flow), route);
    Route orthogonal = route;
    orthogonal.mode = RouteMode::Orthogonal;
    QVERIFY(!document.setRoute(flow, orthogonal));
    QCOMPARE(document.diagram().route(flow).mode, RouteMode::Bezier);
}

void TestDocument::fontStyleState()
{
    Document document;
    const EntityId stock = document.createVariable(ElementKind::Stock);
    const EntityId comment = document.createComment("Note");
    const EntityId cloud = document.createCloud();
    QCOMPARE(document.diagram().fontStyle(stock), FontStyle {});
    const FontStyle stockFont {"Arial", 18};
    QVERIFY(document.setFontStyle(stock, stockFont));
    QCOMPARE(document.diagram().fontStyle(stock), stockFont);
    const FontStyle commentFont {"", 14};
    QVERIFY(document.setFontStyle(comment, commentFont));
    QCOMPARE(document.diagram().fontStyle(comment), commentFont);
    QVERIFY(!document.setFontStyle(cloud, stockFont));
    QVERIFY(!document.setFontStyle(stock, {"Arial", -1}));
    QVERIFY(!document.setFontStyle(9999, stockFont));

    const ElementData captured = *document.capture(stock);
    QCOMPARE(captured.font, stockFont);
    document.remove(stock);
    QVERIFY(document.diagram().fontStyles().empty() || document.diagram().fontStyle(stock) == FontStyle {});
    QVERIFY(document.restore(captured));
    QCOMPARE(document.diagram().fontStyle(stock), stockFont);
    QVERIFY(document.setFontStyle(stock, FontStyle {}));
    QCOMPARE(document.diagram().fontStyle(stock), FontStyle {});
}

void TestDocument::clearDocument()
{
    Document document;
    const EntityId stock = document.createVariable(ElementKind::Stock);
    const EntityId cloud = document.createCloud();
    document.createFlow(stock, cloud);
    document.clear();
    QVERIFY(document.elements().empty());
    QVERIFY(document.diagram().positions().empty());
    QVERIFY(document.diagram().routes().empty());
    QCOMPARE(document.createVariable(ElementKind::Stock), EntityId(1));
}

void TestDocument::entityAndConnectorTypes()
{
    Document document;
    const EntityId stock = document.createVariable(ElementKind::Stock);
    const EntityId auxiliary = document.createVariable(ElementKind::Auxiliary);
    const EntityId constant = document.createVariable(ElementKind::Constant);
    const EntityId data = document.createVariable(ElementKind::Data);
    const EntityId cloud = document.createCloud();
    const EntityId secondCloud = document.createCloud();
    const EntityId comment = document.createComment("Note");
    QVERIFY(constant && data && cloud && secondCloud && comment);
    QCOMPARE(static_cast<const Comment *>(document.find(comment))->content(), QString("Note"));
    const EntityId secondComment = document.createComment("Note");
    QVERIFY(secondComment != 0);
    QCOMPARE(static_cast<const Comment *>(document.find(secondComment))->content(), QString("Note"));
    QVERIFY(document.setCommentContent(secondComment, "Updated note"));
    QCOMPARE(static_cast<const Comment *>(document.find(secondComment))->content(), QString("Updated note"));
    QVERIFY(!document.setCommentContent(stock, "Invalid"));
    const EntityId flow = document.createFlow(stock, cloud);
    QVERIFY(flow != 0);
    QCOMPARE(static_cast<const Flow *>(document.find(flow))->endpoints().from, stock);
    const EntityId arrow = document.createConnector(stock, auxiliary);
    QVERIFY(arrow != 0);
    QCOMPARE(document.createConnector(stock, auxiliary), EntityId(0));
    QVERIFY(document.createConnector(comment, flow) != 0);
    QCOMPARE(document.createConnector(comment, secondComment), EntityId(0));
    ElementData invalidCommentLink;
    invalidCommentLink.kind = ElementKind::Connector;
    invalidCommentLink.from = comment;
    invalidCommentLink.to = secondComment;
    QVERIFY(!document.restore(invalidCommentLink));
    QCOMPARE(document.createConnector(cloud, auxiliary), EntityId(0));
}

void TestDocument::reconnectRoutes()
{
    Document document;
    const EntityId first = document.createVariable(ElementKind::Stock);
    const EntityId second = document.createVariable(ElementKind::Stock);
    const EntityId cloud = document.createCloud();
    const EntityId auxiliary = document.createVariable(ElementKind::Auxiliary);
    const EntityId connector = document.createConnector(first, auxiliary);
    QVERIFY(document.reconnectConnector(connector, second, auxiliary));
    QCOMPARE(static_cast<const Connector *>(document.find(connector))->from(), second);
    QVERIFY(!document.reconnectConnector(connector, 9999, auxiliary));
    const EntityId flow = document.createFlow(first, cloud);
    QVERIFY(document.reconnectFlow(flow, second, cloud));
    QCOMPARE(static_cast<const Flow *>(document.find(flow))->endpoints().from, second);
    QVERIFY(!document.reconnectFlow(flow, auxiliary, cloud));
}

void TestDocument::metadata()
{
    Document document;
    const EntityId id = document.createVariable(ElementKind::Stock);
    QVERIFY(document.setUnit(id, "people"));
    QVERIFY(document.setComment(id, "Population stock"));
    const auto *model = static_cast<const ModelElement *>(document.find(id));
    QCOMPARE(model->unit(), QString("people"));
    QCOMPARE(model->comment(), QString("Population stock"));
    QVERIFY(!document.setUnit(9999, "x"));
    QVERIFY(!document.setComment(9999, "x"));
}

void TestDocument::applyFailureIsAtomic()
{
    Document document;
    const EntityId stock = document.createVariable(ElementKind::Stock);
    const EntityId otherStock = document.createVariable(ElementKind::Stock);
    const EntityId cloud = document.createCloud();
    const EntityId flow = document.createFlow(stock, cloud);
    QVERIFY(document.rename(stock, "Population"));
    QVERIFY(document.setEquation(stock, "100"));
    QVERIFY(document.setUnit(stock, "people"));
    QVERIFY(document.setComment(stock, "Initial"));
    QVERIFY(document.setFontStyle(stock, {"Arial", 18}));

    const ElementData original = *document.capture(stock);
    ElementData edit = original;
    edit.displayName = "Stock2";
    edit.equation = "200";
    edit.unit = "invalid";
    QVERIFY(!document.apply(edit));
    QCOMPARE(*document.capture(stock), original);

    edit = original;
    edit.font = {"Arial", -1};
    edit.equation = "300";
    QVERIFY(!document.apply(edit));
    QCOMPARE(*document.capture(stock), original);

    edit = original;
    edit.equation = "400";
    edit.displayName = "Updated";
    edit.position = {50.0, 75.0};
    QVERIFY(document.apply(edit));
    QCOMPARE(*document.capture(stock), edit);
    QVERIFY(document.model().findByName("updated"));

    const ElementData originalFlow = *document.capture(flow);
    ElementData flowEdit = originalFlow;
    flowEdit.flowEndpoints = {otherStock, cloud};
    flowEdit.route.mode = RouteMode::Orthogonal;
    QVERIFY(!document.apply(flowEdit));
    QCOMPARE(*document.capture(flow), originalFlow);

    const EntityId otherFlow = document.createFlow(otherStock, cloud);
    QVERIFY(otherFlow != 0);
    const ElementData originalOtherFlow = *document.capture(otherFlow);
    flowEdit.route.mode = RouteMode::Straight;
    QVERIFY(!document.apply(flowEdit));
    QCOMPARE(*document.capture(flow), originalFlow);
    QCOMPARE(*document.capture(otherFlow), originalOtherFlow);

    const EntityId auxiliary = document.createVariable(ElementKind::Auxiliary);
    const EntityId connector = document.createConnector(stock, auxiliary);
    const ElementData originalConnector = *document.capture(connector);
    ElementData connectorEdit = originalConnector;
    connectorEdit.from = 0;
    connectorEdit.position = {500.0, 500.0};
    QVERIFY(!document.apply(connectorEdit));
    QCOMPARE(*document.capture(connector), originalConnector);

    QVERIFY(document.defineDimension("Dim", 2));
    QVERIFY(document.setDimensions(stock, {"dim"}));
    QVERIFY(document.setElementEquation(stock, {0}, "1"));
    QVERIFY(document.setElementEquation(stock, {1}, "2"));
    const ElementData originalArray = *document.capture(stock);
    edit = originalArray;
    edit.equation = "1";
    QVERIFY(!document.apply(edit));
    QCOMPARE(*document.capture(stock), originalArray);
}

void TestDocument::validation()
{
    Document document;
    const EntityId stock = document.createVariable(ElementKind::Stock);
    const EntityId first = document.createVariable(ElementKind::Auxiliary);
    const EntityId second = document.createVariable(ElementKind::Auxiliary);
    document.rename(stock, "\u5E93\u5B58");
    document.setEquation(stock, "100");
    document.rename(first, "\u8F85\u52A9 \u53D8\u91CF");
    document.setEquation(first, "Auxiliary2 + Missing");
    document.setEquation(second, "\"\u8F85\u52A9 \u53D8\u91CF\"");
    document.createConnector(first, second);
    document.createConnector(second, first);
    const auto issues = validateDocument(document);
    bool undefined = false;
    bool cycle = false;
    bool unused = false;
    for (const ModelIssue &issue : issues) {
        undefined = undefined || issue.message.contains("Undefined variable");
        cycle = cycle || issue.message.contains("cycle");
        unused = unused || issue.severity == IssueSeverity::Warning;
    }
    QVERIFY(undefined);
    QVERIFY(cycle);
    QVERIFY(unused);

    document.setEquation(first, "\u5E93\u5B58 + ");
    QVERIFY(!validateDocument(document).empty());
}

QTEST_APPLESS_MAIN(TestDocument)
#include "TestDocument.moc"
