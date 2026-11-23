#include <qds/io/XmileFile.h>

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace qds::core;

class TestXmileFile : public QObject
{
    Q_OBJECT

private slots:
    void zipRoundTrip();
    void plainXmlOpen();
    void invalidInput();
    void missingFile();
};

void TestXmileFile::zipRoundTrip()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("model.xmile"));
    Document document;
    const EntityId stock = document.createVariable(VariableType::Stock, {120.5, 80.25});
    const EntityId flow = document.createVariable(VariableType::Flow, {300.0, 80.25});
    const EntityId auxiliary = document.createVariable(VariableType::Auxiliary, {300.0, 240.0});
    const EntityId cloud = document.createVariable(VariableType::Cloud, {450.0, 80.25});
    const EntityId note = document.createVariable(VariableType::Annotation, {100.0, 300.0});
    QVERIFY(document.renameVariable(stock, "库存 & Population"));
    document.setEquation(stock, "100");
    document.setUnit(stock, "people");
    document.setComment(stock, "Population stock");
    document.setEquation(flow, "辅助变量 * 0.5");
    QVERIFY(document.renameVariable(auxiliary, "辅助变量"));
    const ConnectionId inflow = document.createConnection(flow, stock, ConnectionType::MaterialFlow);
    document.createConnection(stock, auxiliary);
    document.createConnection(auxiliary, flow);
    document.createConnection(flow, cloud, ConnectionType::MaterialFlow);
    document.createConnection(note, stock, ConnectionType::Annotation);
    Route route;
    route.manual = true;
    route.locked = true;
    route.mode = RouteMode::Bezier;
    route.controlPoints = {{200.0, 100.0}};
    document.setRoute(inflow, route);

    QString error;
    QVERIFY2(qds::io::saveXmile(path, document, &error), qPrintable(error));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.read(2), QByteArray("PK"));

    Document loaded;
    QVERIFY2(qds::io::loadXmile(path, loaded, &error), qPrintable(error));
    QCOMPARE(loaded.model().variables().size(), std::size_t(5));
    QCOMPARE(loaded.model().connections().size(), std::size_t(5));
    const Variable *loadedStock = loaded.model().variableByName("库存 & Population");
    QVERIFY(loadedStock);
    QCOMPARE(loadedStock->equation, std::string("100"));
    QCOMPARE(loadedStock->unit, std::string("people"));
    QCOMPARE(loadedStock->comment, std::string("Population stock"));
    QCOMPARE(loaded.diagram().position(loadedStock->id), Vec2({120.5, 80.25}));
    QCOMPARE(loaded.model().variableByName("Flow")->equation, std::string("辅助变量 * 0.5"));
    QVERIFY(loaded.model().variableByName("Cloud"));
    QVERIFY(loaded.model().variableByName("Note"));
    QCOMPARE(loaded.diagram().position(loaded.model().variableByName("Cloud")->id), Vec2({450.0, 80.25}));
    const Variable *loadedFlow = loaded.model().variableByName("Flow");
    bool routeFound = false;
    for (const Connection &connection : loaded.model().connections()) {
        if (connection.from == loadedFlow->id && connection.to == loadedStock->id) {
            routeFound = true;
            QVERIFY(loaded.diagram().route(connection.id).controlPoints == std::vector<Vec2>({{200.0, 100.0}}));
            QCOMPARE(loaded.diagram().route(connection.id).mode, RouteMode::Bezier);
            QVERIFY(loaded.diagram().route(connection.id).locked);
        }
    }
    QVERIFY(routeFound);
}

void TestXmileFile::plainXmlOpen()
{
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("plain.xmile"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("<?xml version=\"1.0\"?><xmile><model><variables>"
               "<stock name=\"S\"><eqn>10</eqn></stock>"
               "<flow name=\"F\"><eqn>1</eqn></flow>"
               "</variables></model></xmile>");
    file.close();
    Document document;
    QString error;
    QVERIFY2(qds::io::loadXmile(path, document, &error), qPrintable(error));
    QCOMPARE(document.model().variables().size(), std::size_t(2));
    QVERIFY(document.model().variableByName("S"));
    QVERIFY(document.diagram().position(document.model().variableByName("S")->id).x > 0.0);
}

void TestXmileFile::invalidInput()
{
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("invalid.xmile"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("<xmile><model>");
    file.close();
    Document document;
    document.createVariable(VariableType::Stock);
    QString error;
    QVERIFY(!qds::io::loadXmile(path, document, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(document.model().variables().size(), std::size_t(1));
}

void TestXmileFile::missingFile()
{
    Document document;
    QString error;
    QVERIFY(!qds::io::loadXmile(QStringLiteral("R:/definitely-missing-qds-model.xmile"), document, &error));
    QVERIFY(!error.isEmpty());
}

QTEST_APPLESS_MAIN(TestXmileFile)
#include "TestXmileFile.moc"
