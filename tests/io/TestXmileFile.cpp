#include <qds/io/XmileFile.h>

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
#include <zlib.h>

using namespace qds::core;

namespace {

QByteArray extractModelXml(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QByteArray archive = file.readAll();
    const auto read16 = [&archive](int offset) {
        return static_cast<quint16>(static_cast<quint8>(archive[offset]))
            | (static_cast<quint16>(static_cast<quint8>(archive[offset + 1])) << 8);
    };
    const auto read32 = [&archive](int offset) {
        return static_cast<quint32>(static_cast<quint8>(archive[offset]))
            | (static_cast<quint32>(static_cast<quint8>(archive[offset + 1])) << 8)
            | (static_cast<quint32>(static_cast<quint8>(archive[offset + 2])) << 16)
            | (static_cast<quint32>(static_cast<quint8>(archive[offset + 3])) << 24);
    };
    if (archive.size() < 30 || read32(0) != 0x04034b50)
        return {};
    const quint16 method = read16(8);
    const quint32 compressedSize = read32(18);
    const quint32 uncompressedSize = read32(22);
    const int offset = 30 + read16(26) + read16(28);
    if (offset < 0 || compressedSize > static_cast<quint32>(archive.size() - offset))
        return {};
    if (method == 0)
        return archive.mid(offset, static_cast<int>(compressedSize));
    if (method != 8)
        return {};

    QByteArray result(static_cast<int>(uncompressedSize), Qt::Uninitialized);
    z_stream stream {};
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(archive.constData() + offset));
    stream.avail_in = compressedSize;
    stream.next_out = reinterpret_cast<Bytef *>(result.data());
    stream.avail_out = uncompressedSize;
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
        return {};
    const int status = inflate(&stream, Z_FINISH);
    inflateEnd(&stream);
    return status == Z_STREAM_END && stream.total_out == uncompressedSize ? result : QByteArray {};
}

} // namespace

class TestXmileFile : public QObject
{
    Q_OBJECT

private slots:
    void zipRoundTrip();
    void plainXmlOpen();
    void missingViewsAreCompleted();
    void invalidInput();
    void duplicateNames();
    void missingFile();
};

void TestXmileFile::zipRoundTrip()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("model.xmile"));
    Document document;
    const EntityId stock = document.createVariable(ElementKind::Stock, {120.5, 80.25});
    const EntityId auxiliary = document.createVariable(ElementKind::Auxiliary, {300.0, 240.0});
    const EntityId constant = document.createVariable(ElementKind::Constant, {300.0, 360.0});
    const EntityId data = document.createVariable(ElementKind::Data, {300.0, 480.0});
    const EntityId cloud = document.createCloud({450.0, 80.25});
    document.createCloud({700.0, 500.0});
    const EntityId note = document.createComment("\u6CE8\u91CA", {100.0, 300.0});
    const EntityId secondNote = document.createComment("\u6CE8\u91CA", {500.0, 300.0});
    QVERIFY(document.setCommentContent(note, "\u66F4\u65B0\u540E\u7684\u6CE8\u91CA"));
    const EntityId flow = document.createFlow(stock, cloud);
    QVERIFY(document.rename(stock, "\u5E93\u5B58 & Population"));
    document.setEquation(stock, "100");
    document.setUnit(stock, "people");
    document.setComment(stock, "Population stock");
    document.setEquation(flow, "\u8F85\u52A9\u53D8\u91CF * 0.5");
    QVERIFY(document.rename(auxiliary, "\u8F85\u52A9\u53D8\u91CF"));
    QVERIFY(document.rename(constant, "\u5E38\u91CF"));
    QVERIFY(document.rename(data, "\u5916\u90E8\u6570\u636E"));
    document.createConnector(stock, auxiliary);
    document.createConnector(auxiliary, flow);
    document.createConnector(note, flow);
    document.createConnector(secondNote, auxiliary);
    Route route;
    route.manual = true;
    route.locked = true;
    route.mode = RouteMode::Bezier;
    route.controlPoints = {{200.0, 100.0}, {260.0, 120.0}};
    document.setRoute(flow, route);

    QString error;
    QVERIFY2(qds::io::saveXmile(path, document, &error), qPrintable(error));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.read(2), QByteArray("PK"));
    file.close();
    const QByteArray xml = extractModelXml(path);
    QVERIFY(xml.contains("<xmile"));
    QVERIFY(xml.contains("version=\"1.1\""));
    QVERIFY(!xml.contains("<cloud"));
    QCOMPARE(xml.count("<text_box"), 2);
    QCOMPARE(xml.count("uid="), 4);

    Document loaded;
    QVERIFY2(qds::io::loadXmile(path, loaded, &error), qPrintable(error));
    std::size_t modelCount = 0;
    std::size_t connectorCount = 0;
    for (const ModelElement &element : loaded.model().elements()) {
        Q_UNUSED(element)
        ++modelCount;
    }
    for (const ViewElement &element : loaded.diagram().elements()) {
        if (element.kind() == ElementKind::Connector)
            ++connectorCount;
    }
    QCOMPARE(modelCount, std::size_t(5));
    QCOMPARE(connectorCount, std::size_t(4));
    const auto *loadedStock = dynamic_cast<const ModelElement *>(loaded.findByName("\u5E93\u5B58 & Population"));
    QVERIFY(loadedStock);
    QCOMPARE(loadedStock->equation, std::string("100"));
    QCOMPARE(loadedStock->unit, std::string("people"));
    QCOMPARE(loadedStock->comment, std::string("Population stock"));
    QCOMPARE(loaded.diagram().position(loadedStock->id()), Vec2({120.5, 80.25}));
    QCOMPARE(loaded.findByName("\u5E38\u91CF")->kind(), ElementKind::Constant);
    QCOMPARE(loaded.findByName("\u5916\u90E8\u6570\u636E")->kind(), ElementKind::Data);
    QCOMPARE(loaded.findByName("\u8F85\u52A9\u53D8\u91CF")->kind(), ElementKind::Auxiliary);
    const auto *loadedFlow = dynamic_cast<const Flow *>(loaded.findByName("Flow1"));
    QVERIFY(loadedFlow);
    QCOMPARE(loadedFlow->equation, std::string("\u8F85\u52A9\u53D8\u91CF * 0.5"));
    QCOMPARE(loadedFlow->endpoints.from, loadedStock->id());
    QVERIFY(loadedFlow->endpoints.to != 0);
    QCOMPARE(loaded.find(loadedFlow->endpoints.to)->kind(), ElementKind::Cloud);
    const Route loadedRoute = loaded.diagram().route(loadedFlow->id());
    QVERIFY(loadedRoute.controlPoints == std::vector<Vec2>({{200.0, 100.0}, {260.0, 120.0}}));
    QCOMPARE(loadedRoute.mode, RouteMode::Bezier);
    QVERIFY(loadedRoute.locked);
    QCOMPARE(loaded.diagram().position(loadedFlow->endpoints.to), Vec2({450.0, 80.25}));

    int commentCount = 0;
    int commentConnectorCount = 0;
    int cloudCount = 0;
    bool standaloneCloudFound = false;
    for (const ViewElement &element : loaded.diagram().elements()) {
        if (element.kind() == ElementKind::Cloud) {
            ++cloudCount;
            standaloneCloudFound = standaloneCloudFound
                || loaded.diagram().position(element.id()) == Vec2({700.0, 500.0});
        } else if (element.kind() == ElementKind::Comment) {
            ++commentCount;
            const bool first = loaded.diagram().position(element.id()) == Vec2({100.0, 300.0});
            QCOMPARE(static_cast<const Comment &>(element).content,
                     first ? std::string("\u66F4\u65B0\u540E\u7684\u6CE8\u91CA") : std::string("\u6CE8\u91CA"));
        } else if (element.kind() == ElementKind::Connector) {
            const auto &connector = static_cast<const Connector &>(element);
            const DocumentElement *from = loaded.find(connector.from);
            if (from->kind() != ElementKind::Comment)
                continue;
            ++commentConnectorCount;
            if (loaded.diagram().position(from->id()) == Vec2({100.0, 300.0}))
                QCOMPARE(connector.to, loadedFlow->id());
            else {
                QCOMPARE(loaded.diagram().position(from->id()), Vec2({500.0, 300.0}));
                QCOMPARE(connector.to, loaded.findByName("\u8F85\u52A9\u53D8\u91CF")->id());
            }
        }
    }
    QCOMPARE(commentCount, 2);
    QCOMPARE(commentConnectorCount, 2);
    QCOMPARE(cloudCount, 2);
    QVERIFY(standaloneCloudFound);
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
               "<aux name=\"A\"><eqn>2</eqn></aux>"
               "</variables></model></xmile>");
    file.close();
    Document document;
    QString error;
    QVERIFY2(qds::io::loadXmile(path, document, &error), qPrintable(error));
    QVERIFY(document.findByName("S"));
    QCOMPARE(document.findByName("A")->kind(), ElementKind::Auxiliary);
    const auto *flow = dynamic_cast<const Flow *>(document.findByName("F"));
    QVERIFY(flow);
    QVERIFY(flow->endpoints.from != 0 && flow->endpoints.to != 0);
    QVERIFY(flow->endpoints.from != flow->endpoints.to);
    QCOMPARE(document.find(flow->endpoints.from)->kind(), ElementKind::Cloud);
    QCOMPARE(document.find(flow->endpoints.to)->kind(), ElementKind::Cloud);
    QVERIFY(document.diagram().position(flow->endpoints.from).x > 0.0);
    QVERIFY(document.diagram().position(flow->endpoints.to).x
            > document.diagram().position(flow->endpoints.from).x);
    QVERIFY(document.diagram().position(document.findByName("S")->id()).x > 0.0);
}

void TestXmileFile::missingViewsAreCompleted()
{
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("partial.xmile"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("<?xml version=\"1.0\"?><xmile version=\"1.1\"><model><variables>"
               "<stock name=\"Source\"><eqn>10</eqn><outflow>F</outflow></stock>"
               "<stock name=\"Target\"><eqn>20</eqn><inflow>F</inflow></stock>"
               "<flow name=\"F\"><eqn>2</eqn></flow>"
               "<flow name=\"Independent\"><eqn>3</eqn></flow>"
               "</variables></model><views><view type=\"stock_flow\">"
               "<stock name=\"Source\" x=\"300\" y=\"200\"/>"
               "<flow name=\"F\" x=\"380\" y=\"200\"><pts>"
               "<pt x=\"300\" y=\"200\"/><pt x=\"460\" y=\"200\"/>"
               "</pts></flow>"
               "</view></views></xmile>");
    file.close();
    Document document;
    QString error;
    QVERIFY2(qds::io::loadXmile(path, document, &error), qPrintable(error));
    const auto *flow = dynamic_cast<const Flow *>(document.findByName("F"));
    QVERIFY(flow);
    QCOMPARE(document.find(flow->endpoints.from)->kind(), ElementKind::Stock);
    QCOMPARE(document.find(flow->endpoints.to)->kind(), ElementKind::Stock);
    QCOMPARE(flow->endpoints.to, document.findByName("Target")->id());
    QCOMPARE(document.diagram().position(flow->endpoints.from), Vec2({300.0, 200.0}));
    QVERIFY(document.diagram().position(flow->endpoints.to).x > 0.0);

    const auto *independent = dynamic_cast<const Flow *>(document.findByName("Independent"));
    QVERIFY(independent);
    QCOMPARE(document.find(independent->endpoints.from)->kind(), ElementKind::Cloud);
    QCOMPARE(document.find(independent->endpoints.to)->kind(), ElementKind::Cloud);
    QVERIFY(independent->endpoints.from != independent->endpoints.to);
    for (const ModelElement &element : document.model().elements()) {
        QVERIFY(element.id() != 0);
        if (element.kind() == ElementKind::Flow) {
            const auto &routed = static_cast<const Flow &>(element);
            QVERIFY(routed.endpoints.from != 0 && routed.endpoints.to != 0);
        }
    }
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
    document.createVariable(ElementKind::Stock);
    QString error;
    QVERIFY(!qds::io::loadXmile(path, document, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(document.elements().begin()->kind(), ElementKind::Stock);
}

void TestXmileFile::duplicateNames()
{
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("duplicate.xmile"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("<xmile><model><variables>"
               "<stock name=\"Flow\"><eqn>0</eqn></stock>"
               "<aux name=\"flow\"><eqn>1</eqn></aux>"
               "</variables></model></xmile>");
    file.close();
    Document document;
    document.createVariable(ElementKind::Stock);
    QString error;
    QVERIFY(!qds::io::loadXmile(path, document, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(document.elements().begin()->kind() == ElementKind::Stock);
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
