#include <io/DocumentFile.h>
#include <io/XmileFile.h>

#include <QFile>
#include <QTemporaryDir>
#include <QVector>
#include <QtTest>
#include <zlib.h>

#include <limits>
#include <unordered_set>

using namespace qds::core;

namespace {

QByteArray extractDocumentXml(const QString &path)
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

void verifyConnectedDocument(const Document &document, std::size_t expectedModelCount, std::size_t expectedConnectorCount)
{
    std::size_t modelCount = 0;
    for (const ModelElement &element : document.model().elements()) {
        Q_UNUSED(element)
        ++modelCount;
    }
    QCOMPARE(modelCount, expectedModelCount);

    std::unordered_set<EntityId> connected;
    std::size_t connectorCount = 0;
    for (const DocumentElement &element : document.elements()) {
        if (element.kind() == ElementKind::Flow) {
            QCOMPARE(document.diagram().route(element.id()).mode, RouteMode::Straight);
            const auto &flow = static_cast<const Flow &>(element);
            QVERIFY(flow.endpoints().from != 0 && flow.endpoints().to != 0);
            QVERIFY(document.find(flow.endpoints().from) && document.find(flow.endpoints().to));
            connected.insert(flow.id());
            connected.insert(flow.endpoints().from);
            connected.insert(flow.endpoints().to);
        } else if (element.kind() == ElementKind::Connector) {
            QCOMPARE(document.diagram().route(element.id()).mode, RouteMode::Bezier);
            ++connectorCount;
            const auto &connector = static_cast<const Connector &>(element);
            QVERIFY(connector.from() != 0 && connector.to() != 0);
            QVERIFY(document.find(connector.from()) && document.find(connector.to()));
            connected.insert(connector.id());
            connected.insert(connector.from());
            connected.insert(connector.to());
        }
    }
    QCOMPARE(connectorCount, expectedConnectorCount);
    for (const DocumentElement &element : document.elements())
        QVERIFY2(connected.count(element.id()) != 0, qPrintable(QString::number(element.id())));
    for (const auto &entry : document.diagram().routes()) {
        QVERIFY(!entry.second.manual);
        for (const RoutePoint &point : entry.second.controlPoints)
            QVERIFY(point.automatic);
    }
}

} // namespace

class TestXmileFile : public QObject
{
    Q_OBJECT

private slots:
    void nativeRoundTrip();
    void lookupRoundTrip();
    void plainXmlOpen();
    void sirModelImport();
    void simulationSettingsImport();
    void xmileExport();
    void xmileRejectsZip();
    void missingViewsAreCompleted();
    void workspaceValidation();
    void invalidInput();
    void duplicateNames();
    void missingFile();
};

void TestXmileFile::nativeRoundTrip()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("model.qds"));
    Document document;
    const EntityId stock = document.createVariable(ElementKind::Stock, {120.5, 80.25});
    const EntityId auxiliary = document.createVariable(ElementKind::Variable, {300.0, 240.0});
    const EntityId constant = document.createVariable(ElementKind::Variable, {300.0, 360.0});
    const EntityId data = document.createVariable(ElementKind::Variable, {300.0, 480.0});
    const EntityId cloud = document.createCloud({450.0, 80.25});
    document.createCloud({700.0, 500.0});
    const EntityId note = document.createComment("\u6CE8\u91CA", {100.0, 300.0});
    const EntityId secondNote = document.createComment("\u6CE8\u91CA", {500.0, 300.0});
    QVERIFY(document.setCommentContent(note, "\u66F4\u65B0\u540E\u7684\u6CE8\u91CA"));
    const EntityId flow = document.createFlow(stock, cloud);
    QVERIFY(document.rename(stock, "\u5E93\u5B58 & Population"));
    document.setInitialEquation(stock, "100");
    document.setUnit(stock, "people");
    document.setComment(stock, "Population stock");
    document.setFontStyle(stock, {"Arial", 18});
    document.setFontStyle(note, {"", 14});
    document.setFontStyle(flow, {"Times New Roman", 16});
    document.setEquation(flow, "\u8F85\u52A9\u53D8\u91CF * 0.5");
    QVERIFY(document.rename(auxiliary, "\u8F85\u52A9\u53D8\u91CF"));
    QVERIFY(document.rename(constant, "\u5E38\u91CF"));
    QVERIFY(document.rename(data, "\u5916\u90E8\u6570\u636E"));
    const EntityId automaticConnector = document.createConnector(stock, auxiliary);
    Route automaticRoute = defaultConnectorRoute();
    automaticRoute.controlPoints[1].relative.y = -automaticRoute.controlPoints[1].relative.y;
    document.setRoute(automaticConnector, automaticRoute);
    document.createConnector(auxiliary, flow);
    document.createConnector(note, flow);
    document.createConnector(secondNote, auxiliary);
    Route route;
    route.manual = true;
    route.locked = true;
    route.mode = RouteMode::Bezier;
    route.controlPoints = {{0.25, 100.0}, {0.75, 120.0}};
    document.setRoute(flow, route);
    const SimulationSettings simulation {-2.5, 12.0, 0.25, 0.5, IntegrationMethod::RungeKutta4, QStringLiteral("Month")};
    QVERIFY(document.setSimulationSettings(simulation));
    const qds::io::Workspace workspace {true, 0.66, {123.5, 456.25}};

    QString error;
    QVERIFY2(qds::io::DocumentFile::save(path, document, workspace, &error), qPrintable(error));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.read(2), QByteArray("PK"));
    file.seek(0);
    const QByteArray archive = file.readAll();
    QCOMPARE(archive.count("PK\x03\x04"), 1);
    file.close();
    const QByteArray xml = extractDocumentXml(path);
    QVERIFY(xml.contains("<qds-document version=\"1\">"));
    QVERIFY(xml.contains("<model>"));
    QVERIFY(xml.contains("<diagram>"));
    QVERIFY(xml.contains("<workspace"));
    QVERIFY(xml.contains("<simulation"));
    QVERIFY(xml.contains("save-interval=\"0.5\""));
    QVERIFY(xml.contains("method=\"rk4\""));
    QVERIFY(xml.contains("time-unit=\"Month\""));
    QVERIFY(xml.contains("zoom=\"0.66\""));
    QVERIFY(xml.contains("center-x=\"123.5\""));
    QVERIFY(xml.contains("center-y=\"456.25\""));
    QCOMPARE(xml.count("<cloud"), 2);
    QCOMPARE(xml.count("<comment"), 2);
    QCOMPARE(xml.count("<variable"), 3);
    QVERIFY(xml.contains("font-family=\"Arial\""));
    QVERIFY(xml.contains("font-size=\"14\""));

    Document loaded;
    qds::io::Workspace loadedWorkspace;
    bool nativeFormat = false;
    QVERIFY2(qds::io::DocumentFile::open(path, loaded, loadedWorkspace, &error, &nativeFormat), qPrintable(error));
    QVERIFY(nativeFormat);
    QCOMPARE(loadedWorkspace, workspace);
    QCOMPARE(loaded.simulationSettings(), simulation);

    const QByteArray memoryXml = qds::io::DocumentFile::toXml(document, workspace, &error);
    QVERIFY(!memoryXml.startsWith("PK"));
    Document memoryDocument;
    qds::io::Workspace memoryWorkspace;
    QVERIFY2(qds::io::DocumentFile::fromXml(memoryXml, memoryDocument, memoryWorkspace, &error), qPrintable(error));
    QCOMPARE(memoryDocument.simulationSettings(), simulation);
    QCOMPARE(memoryWorkspace, workspace);
    QVERIFY(memoryDocument.findByName("\u5E93\u5B58 & Population"));

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
    QCOMPARE(dynamic_cast<const Stock *>(loadedStock)->initialEquation(), QString("100"));
    QCOMPARE(loadedStock->unit(), QString("people"));
    QCOMPARE(loadedStock->comment(), QString("Population stock"));
    QCOMPARE(loaded.diagram().position(loadedStock->id()), Vec2({120.5, 80.25}));
    QCOMPARE(loaded.diagram().fontStyle(loadedStock->id()), (FontStyle {"Arial", 18}));
    QCOMPARE(loaded.findByName("\u5E38\u91CF")->kind(), ElementKind::Variable);
    QCOMPARE(loaded.findByName("\u5916\u90E8\u6570\u636E")->kind(), ElementKind::Variable);
    QCOMPARE(loaded.findByName("\u8F85\u52A9\u53D8\u91CF")->kind(), ElementKind::Variable);
    const auto *loadedFlow = dynamic_cast<const Flow *>(loaded.findByName("Flow1"));
    QVERIFY(loadedFlow);
    QCOMPARE(loaded.diagram().fontStyle(loadedFlow->id()), (FontStyle {"Times New Roman", 16}));
    QCOMPARE(loadedFlow->equation(), QString("\u8F85\u52A9\u53D8\u91CF * 0.5"));
    QCOMPARE(loadedFlow->endpoints().from, loadedStock->id());
    QVERIFY(loadedFlow->endpoints().to != 0);
    QCOMPARE(loaded.find(loadedFlow->endpoints().to)->kind(), ElementKind::Cloud);
    const Route loadedRoute = loaded.diagram().route(loadedFlow->id());
    QVERIFY(loadedRoute.controlPoints == QVector<RoutePoint>({{0.25, 100.0}, {0.75, 120.0}}));
    QCOMPARE(loadedRoute.mode, RouteMode::Bezier);
    QVERIFY(loadedRoute.locked);
    QCOMPARE(loaded.diagram().position(loadedFlow->endpoints().to), Vec2({450.0, 80.25}));

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
            if (first)
                QCOMPARE(loaded.diagram().fontStyle(element.id()), (FontStyle {"", 14}));
            QCOMPARE(static_cast<const Comment &>(element).content(),
                     first ? QString("\u66F4\u65B0\u540E\u7684\u6CE8\u91CA") : QString("\u6CE8\u91CA"));
        } else if (element.kind() == ElementKind::Connector) {
            const Route loadedConnectorRoute = loaded.diagram().route(element.id());
            const Route expectedRoute = defaultConnectorRoute();
            QCOMPARE(loadedConnectorRoute.mode, expectedRoute.mode);
            QCOMPARE(loadedConnectorRoute.manual, expectedRoute.manual);
            QCOMPARE(loadedConnectorRoute.locked, expectedRoute.locked);
            QCOMPARE(loadedConnectorRoute.controlPoints.size(), expectedRoute.controlPoints.size());
            for (std::size_t index = 0; index < expectedRoute.controlPoints.size(); ++index) {
                const RoutePoint &actualPoint = loadedConnectorRoute.controlPoints[index];
                const RoutePoint &expectedPoint = expectedRoute.controlPoints[index];
                QCOMPARE(actualPoint.automatic, expectedPoint.automatic);
                QVERIFY(qAbs(actualPoint.relative.x - expectedPoint.relative.x) < 1e-12);
                QVERIFY(qAbs(actualPoint.relative.y - expectedPoint.relative.y) < 1e-12);
            }
            const auto &connector = static_cast<const Connector &>(element);
            const DocumentElement *from = loaded.find(connector.from());
            if (from->kind() != ElementKind::Comment)
                continue;
            ++commentConnectorCount;
            if (loaded.diagram().position(from->id()) == Vec2({100.0, 300.0}))
                QCOMPARE(connector.to(), loadedFlow->id());
            else {
                QCOMPARE(loaded.diagram().position(from->id()), Vec2({500.0, 300.0}));
                QCOMPARE(connector.to(), loaded.findByName("\u8F85\u52A9\u53D8\u91CF")->id());
            }
        }
    }
    QCOMPARE(commentCount, 2);
    QCOMPARE(commentConnectorCount, 2);
    QCOMPARE(cloudCount, 2);
    QVERIFY(standaloneCloudFound);
}

void TestXmileFile::lookupRoundTrip()
{
    Document document;
    const EntityId lookup = document.createLookup();
    QVERIFY(document.rename(lookup, "Rate Table"));
    const auto definition = LookupDefinition::create(
        {0.0, 1.0, 2.0}, {0.0, 2.0, 4.0}, LookupKind::Extrapolate);
    QVERIFY(definition && document.setLookup(lookup, definition));
    const EntityId inlineVariable = document.createVariable(ElementKind::Variable);
    document.rename(inlineVariable, "Inline");
    document.setEquation(inlineVariable, "1");
    const auto inlineDefinition = LookupDefinition::create(
        {0.0, 1.0}, {2.0, 2.0}, LookupKind::Discrete);
    QVERIFY(inlineDefinition && document.setLookup(inlineVariable, inlineDefinition));

    QString error;
    const QByteArray xml = qds::io::DocumentFile::toXml(document, {}, &error);
    QVERIFY2(!xml.isEmpty(), qPrintable(error));
    QVERIFY(xml.contains("<lookup kind=\"extrapolate\""));
    Document native;
    qds::io::Workspace workspace;
    QVERIFY2(qds::io::DocumentFile::fromXml(xml, native, workspace, &error), qPrintable(error));
    const ModelElement *nativeLookup = native.model().findByName("Rate Table");
    const ModelElement *nativeInline = native.model().findByName("Inline");
    QVERIFY(nativeLookup && nativeInline);
    QVERIFY(nativeLookup->lookup() == definition);
    QVERIFY(nativeInline->lookup() == inlineDefinition);

    QByteArray invalidXml = xml;
    invalidXml.replace("<x>0,1,2</x>", "<x>0,0,2</x>");
    Document invalid;
    QVERIFY(!qds::io::DocumentFile::fromXml(invalidXml, invalid, workspace, &error));

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("lookup.xmile"));
    QVERIFY2(qds::io::XmileFile::exportFile(path, document, &error), qPrintable(error));
    Document imported;
    QVERIFY2(qds::io::XmileFile::importFile(path, imported, &error), qPrintable(error));
    const ModelElement *importedLookup = imported.model().findByName("Rate Table");
    const ModelElement *importedInline = imported.model().findByName("Inline");
    QVERIFY(importedLookup && importedInline);
    QCOMPARE(importedLookup->kind(), ElementKind::Lookup);
    QVERIFY(importedLookup->lookup() == definition);
    QVERIFY(importedInline->lookup() == inlineDefinition);
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
    qds::io::Workspace workspace {true, 2.0, {10.0, 20.0}};
    QString error;
    bool nativeFormat = true;
    QVERIFY2(qds::io::DocumentFile::open(path, document, workspace, &error, &nativeFormat), qPrintable(error));
    QVERIFY(!nativeFormat);
    QCOMPARE(workspace, qds::io::Workspace {});
    QVERIFY(document.findByName("S"));
    QCOMPARE(document.findByName("A")->kind(), ElementKind::Variable);
    const auto *flow = dynamic_cast<const Flow *>(document.findByName("F"));
    QVERIFY(flow);
    QVERIFY(flow->endpoints().from != 0 && flow->endpoints().to != 0);
    QVERIFY(flow->endpoints().from != flow->endpoints().to);
    QCOMPARE(document.find(flow->endpoints().from)->kind(), ElementKind::Cloud);
    QCOMPARE(document.find(flow->endpoints().to)->kind(), ElementKind::Cloud);
    QVERIFY(document.diagram().position(flow->endpoints().from).x > 0.0);
    QVERIFY(document.diagram().position(flow->endpoints().to).x
            > document.diagram().position(flow->endpoints().from).x);
    QVERIFY(document.diagram().position(document.findByName("S")->id()).x > 0.0);
}

void TestXmileFile::sirModelImport()
{
    Document document;
    QString error;
    QVERIFY2(qds::io::XmileFile::importFile(QStringLiteral(QDS_SIR_XMILE_PATH), document, &error), qPrintable(error));
    verifyConnectedDocument(document, 8, 6);
    QCOMPARE(document.simulationSettings().startTime(), 0.0);
    QCOMPARE(document.simulationSettings().stopTime(), 100.0);
    QCOMPARE(document.simulationSettings().dt(), 0.03125);
    QCOMPARE(document.simulationSettings().saveInterval(), 0.03125);
    QCOMPARE(document.simulationSettings().integrationMethod(), IntegrationMethod::Euler);
    QCOMPARE(document.simulationSettings().timeUnit(), QStringLiteral("Time"));
    const auto *succumbing = dynamic_cast<const Flow *>(document.findByName("succumbing"));
    const auto *recovering = dynamic_cast<const Flow *>(document.findByName("recovering"));
    QVERIFY(succumbing && recovering);
    QCOMPARE(succumbing->endpoints().from, document.findByName("susceptible")->id());
    QCOMPARE(succumbing->endpoints().to, document.findByName("infectious")->id());
    QCOMPARE(recovering->endpoints().from, document.findByName("infectious")->id());
    QCOMPARE(recovering->endpoints().to, document.findByName("recovered")->id());

    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("sir-export.xmile"));
    QVERIFY2(qds::io::XmileFile::exportFile(path, document, &error), qPrintable(error));
    Document reimported;
    QVERIFY2(qds::io::XmileFile::importFile(path, reimported, &error), qPrintable(error));
    verifyConnectedDocument(reimported, 8, 6);
    QCOMPARE(reimported.simulationSettings(), document.simulationSettings());
}

void TestXmileFile::simulationSettingsImport()
{
    Document reciprocal;
    QString error;
    QVERIFY2(qds::io::XmileFile::importFile(QStringLiteral(QDS_SIR_RECIPROCAL_XMILE_PATH), reciprocal, &error),
        qPrintable(error));
    QCOMPARE(reciprocal.simulationSettings().dt(), 0.03125);
    QCOMPARE(reciprocal.simulationSettings().saveInterval(), 0.03125);

    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("settings.xmile"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("<xmile><sim_specs method=\"rk2\" time_units=\"Month\">"
               "<start>1</start><stop>5</stop><dt reciprocal=\"true\">4</dt></sim_specs>"
               "<model><sim_specs method=\"gear, rk4\"><stop>9</stop></sim_specs>"
               "<variables><stock name=\"S\"><eqn>1</eqn></stock></variables></model></xmile>");
    file.close();
    Document document;
    QVERIFY2(qds::io::XmileFile::importFile(path, document, &error), qPrintable(error));
    QCOMPARE(document.simulationSettings().startTime(), 1.0);
    QCOMPARE(document.simulationSettings().stopTime(), 9.0);
    QCOMPARE(document.simulationSettings().dt(), 0.25);
    QCOMPARE(document.simulationSettings().saveInterval(), 0.25);
    QCOMPARE(document.simulationSettings().integrationMethod(), IntegrationMethod::RungeKutta4);
    QCOMPARE(document.simulationSettings().timeUnit(), QStringLiteral("Month"));

    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("<xmile><sim_specs><start>0</start><stop>2</stop></sim_specs>"
               "<model><variables/></model></xmile>");
    file.close();
    QVERIFY2(qds::io::XmileFile::importFile(path, document, &error), qPrintable(error));
    QCOMPARE(document.simulationSettings().dt(), 1.0);
    QCOMPARE(document.simulationSettings().saveInterval(), 1.0);

    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("<xmile><sim_specs method=\"unsupported\"><start>0</start><stop>1</stop></sim_specs>"
               "<model><variables/></model></xmile>");
    file.close();
    const SimulationSettings original = document.simulationSettings();
    QVERIFY(!qds::io::XmileFile::importFile(path, document, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(document.simulationSettings(), original);
}

void TestXmileFile::xmileExport()
{
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("export.xmile"));
    Document document;
    const EntityId stock = document.createVariable(ElementKind::Stock, {100.0, 100.0});
    const EntityId cloud = document.createCloud({300.0, 100.0});
    document.createFlow(stock, cloud);
    document.createVariable(ElementKind::Variable, {100.0, 300.0});
    document.createComment("Note", {400.0, 300.0});
    QVERIFY(document.setSimulationSettings({2.0, 4.0, 0.25, 0.5, IntegrationMethod::RungeKutta4, QStringLiteral("Month")}));
    QString error;
    QVERIFY2(qds::io::XmileFile::exportFile(path, document, &error), qPrintable(error));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray xml = file.readAll();
    QVERIFY(!xml.startsWith("PK"));
    QVERIFY(xml.contains("<xmile"));
    QVERIFY(xml.contains("version=\"1.1\""));
    QVERIFY(xml.contains("method=\"rk4\""));
    QVERIFY(xml.contains("time_units=\"Month\""));
    QVERIFY(xml.contains("<start>2</start>"));
    QVERIFY(xml.contains("<stop>4</stop>"));
    QVERIFY(xml.contains("<dt>0.25</dt>"));
    QVERIFY(!xml.contains("saveper"));
    QVERIFY(!xml.contains("<cloud"));
    QVERIFY(!xml.contains("qds-workspace"));

    Document imported;
    QVERIFY2(qds::io::XmileFile::importFile(path, imported, &error), qPrintable(error));
    const auto *importedFlow = dynamic_cast<const Flow *>(imported.findByName("Flow1"));
    QVERIFY(importedFlow);
    QCOMPARE(imported.find(importedFlow->endpoints().from)->kind(), ElementKind::Stock);
    QCOMPARE(imported.find(importedFlow->endpoints().to)->kind(), ElementKind::Cloud);
    QCOMPARE(imported.findByName("Variable1")->kind(), ElementKind::Variable);
    QCOMPARE(imported.model().findByName("Stock1")->id(), importedFlow->endpoints().from);
    QCOMPARE(imported.simulationSettings(),
        SimulationSettings(2.0, 4.0, 0.25, 0.25, IntegrationMethod::RungeKutta4, QStringLiteral("Month")));

    file.close();
    QVERIFY(document.setSimulationSettings({0.0, 0.0, 0.25, 0.25}));
    QVERIFY(!qds::io::XmileFile::exportFile(path, document, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), xml);
}

void TestXmileFile::xmileRejectsZip()
{
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("native.qds"));
    Document document;
    document.createVariable(ElementKind::Stock);
    QString error;
    QVERIFY(qds::io::DocumentFile::save(path, document, qds::io::Workspace {}, &error));
    Document imported;
    QVERIFY(!qds::io::XmileFile::importFile(path, imported, &error));
    QVERIFY(error.contains("uncompressed XML"));
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
    qds::io::Workspace workspace;
    QString error;
    QVERIFY2(qds::io::DocumentFile::open(path, document, workspace, &error), qPrintable(error));
    const auto *flow = dynamic_cast<const Flow *>(document.findByName("F"));
    QVERIFY(flow);
    QCOMPARE(document.find(flow->endpoints().from)->kind(), ElementKind::Stock);
    QCOMPARE(document.find(flow->endpoints().to)->kind(), ElementKind::Stock);
    QCOMPARE(flow->endpoints().to, document.findByName("Target")->id());
    QCOMPARE(document.diagram().position(flow->endpoints().from), Vec2({300.0, 200.0}));
    QVERIFY(document.diagram().position(flow->endpoints().to).x > 0.0);

    const auto *independent = dynamic_cast<const Flow *>(document.findByName("Independent"));
    QVERIFY(independent);
    QCOMPARE(document.find(independent->endpoints().from)->kind(), ElementKind::Cloud);
    QCOMPARE(document.find(independent->endpoints().to)->kind(), ElementKind::Cloud);
    QVERIFY(independent->endpoints().from != independent->endpoints().to);
    for (const ModelElement &element : document.model().elements()) {
        QVERIFY(element.id() != 0);
        if (element.kind() == ElementKind::Flow) {
            const auto &routed = static_cast<const Flow &>(element);
            QVERIFY(routed.endpoints().from != 0 && routed.endpoints().to != 0);
        }
    }
}

void TestXmileFile::workspaceValidation()
{
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("workspace.qds"));
    Document document;
    document.createVariable(ElementKind::Stock);
    const qds::io::Workspace valid {true, 1.5, {10.0, 20.0}, true, 30, 40};
    QString error;
    QVERIFY2(qds::io::DocumentFile::save(path, document, valid, &error), qPrintable(error));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray originalContents = file.readAll();
    file.close();

    QVector<qds::io::Workspace> invalidValues;
    for (double zoom : {0.0, -1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        qds::io::Workspace invalid = valid;
        invalid.zoom = zoom;
        invalidValues.push_back(invalid);
    }
    qds::io::Workspace invalidCenter = valid;
    invalidCenter.center.x = std::numeric_limits<double>::infinity();
    invalidValues.push_back(invalidCenter);
    for (const qds::io::Workspace &invalid : invalidValues) {
        QVERIFY(!qds::io::DocumentFile::save(path, document, invalid, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), originalContents);
        file.close();
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
    qds::io::Workspace workspace {true, 2.0, {10.0, 20.0}};
    const qds::io::Workspace originalWorkspace = workspace;
    QVERIFY(!qds::io::DocumentFile::open(path, document, workspace, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(workspace, originalWorkspace);
    QCOMPARE(document.elements().begin()->kind(), ElementKind::Stock);

    const QString nativePath = directory.filePath(QStringLiteral("invalid.qds"));
    QVERIFY(qds::io::DocumentFile::save(nativePath, document, qds::io::Workspace {}, &error));
    file.setFileName(nativePath);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QByteArray contents = file.readAll();
    file.close();
    QVERIFY(contents.size() > 50);
    contents[50] = contents.at(50) ^ 0x7f;
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(file.write(contents), contents.size());
    file.close();
    QVERIFY(!qds::io::DocumentFile::open(nativePath, document, workspace, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(workspace, originalWorkspace);
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
    qds::io::Workspace workspace {true, 2.0, {10.0, 20.0}};
    const qds::io::Workspace originalWorkspace = workspace;
    QVERIFY(!qds::io::DocumentFile::open(path, document, workspace, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(workspace, originalWorkspace);
    QVERIFY(document.elements().begin()->kind() == ElementKind::Stock);
}

void TestXmileFile::missingFile()
{
    Document document;
    qds::io::Workspace workspace;
    QString error;
    QVERIFY(!qds::io::DocumentFile::open(QStringLiteral("R:/definitely-missing-qds-model.xmile"), document, workspace, &error));
    QVERIFY(!error.isEmpty());
}

QTEST_APPLESS_MAIN(TestXmileFile)
#include "TestXmileFile.moc"
