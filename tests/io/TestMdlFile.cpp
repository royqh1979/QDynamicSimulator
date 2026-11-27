#include <core/formula/Analysis.h>
#include <core/formula/Evaluation.h>
#include <core/simulation/Simulation.h>
#include <io/DocumentFile.h>
#include <io/MdlFile.h>

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include <cmath>

using namespace qds::core;
using qds::io::MdlFile;

namespace {

class TestMdlFile : public QObject
{
    Q_OBJECT

private slots:
    void absModel();
    void teacupModel();
    void rabbitModel();
    void additionalSketchViewIgnored();
    void unicodeAndQuotedNames();
    void assignmentFormsAndContinuation();
    void longestSymbolMatch();
    void multipleElementEquations();
    void literalArrays();
    void arrayCorpusModels();
    void numericRanges();
    void macroModel();
    void specialCharacterModel();
    void complexNetRate();
    void openCloudLifecycle();
    void invalidInput_data();
    void invalidInput();
    void documentFileRoundTrip();
};

Document import(const QString &text)
{
    Document document;
    QString error;
    QVector<ModelIssue> warnings;
    const bool ok = MdlFile::importText(text, document, &error, &warnings);
    if (!ok)
        qWarning() << error;
    return ok ? std::move(document) : Document {};
}

Document importFile(const QString &path)
{
    Document document;
    QString error;
    QVector<ModelIssue> warnings;
    const bool ok = MdlFile::importFile(path, document, &error, &warnings);
    if (!ok)
        qWarning() << error;
    return ok ? std::move(document) : Document {};
}

const Flow *flow(const Document &document, const QString &name)
{
    return dynamic_cast<const Flow *>(document.findByName(name));
}

const Connector *connector(const Document &document, EntityId from, EntityId to)
{
    for (const ViewElement &element : document.diagram().elements()) {
        if (element.kind() != ElementKind::Connector)
            continue;
        const auto *candidate = static_cast<const Connector *>(&element);
        if (candidate->from() == from && candidate->to() == to)
            return candidate;
    }
    return nullptr;
}

int connectorCount(const Document &document)
{
    int result = 0;
    for (const ViewElement &element : document.diagram().elements())
        result += element.kind() == ElementKind::Connector ? 1 : 0;
    return result;
}

int cloudCount(const Document &document)
{
    int result = 0;
    for (const DocumentElement &element : document.elements())
        result += element.kind() == ElementKind::Cloud ? 1 : 0;
    return result;
}

int unconnectedCloudCount(const Document &document)
{
    int result = 0;
    for (const DocumentElement &element : document.elements()) {
        if (element.kind() != ElementKind::Cloud)
            continue;
        bool connected = false;
        for (const ModelElement &candidate : document.model().elements()) {
            if (candidate.kind() != ElementKind::Flow)
                continue;
            const FlowEndpoints endpoints = static_cast<const Flow &>(candidate).endpoints();
            connected = connected || endpoints.from == element.id() || endpoints.to == element.id();
        }
        result += connected ? 0 : 1;
    }
    return result;
}

QString rabbitText()
{
    return QStringLiteral(R"MDL({UTF-8}
births = Population * birth rate
~ Person/Year
~ Births per year.
|
deaths = Population / average lifetime
~ Person/Year
~ Deaths per year.
|
Population = INTEG(births - deaths, 100)
~ Person
~ Population.
|
birth rate = 0.04
~ 1/Year
~ Birth rate.
|
average lifetime = 25
~ Year
~ Average lifetime.
|
FINAL TIME = 80
~ Year
~ Final time.
|
INITIAL TIME = 0
~ Year
~ Initial time.
|
SAVEPER = TIME STEP
~ Year
~ Save interval.
|
TIME STEP = 0.5
~ Year
~ Time step.
|
\\\---/// Sketch information - do not modify anything except names
V300  Do not put anything below this section - it will be ignored
*View 1
$192-192-192,0,Times New Roman|12||0-0-0|0-0-0|0-0-255|-1--1--1|-1--1--1|96,96,100,0
10,1,Population,275,69,40,20,3,3,0,0,0,0,0,0
12,2,48,91,71,8,8,0,3,0,0,-1,0,0,0
1,3,5,1,4,0,0,22,0,0,0,-1--1--1,,1|(204,71)|
1,4,5,2,100,0,0,22,0,0,0,-1--1--1,,1|(130,71)|
11,5,48,167,71,6,8,34,3,0,0,1,0,0,0
10,6,births,167,90,19,11,40,3,0,0,-1,0,0,0
12,7,48,487,70,8,8,0,3,0,0,-1,0,0,0
1,8,10,7,4,0,0,22,0,0,0,-1--1--1,,1|(441,70)|
1,9,10,1,100,0,0,22,0,0,0,-1--1--1,,1|(353,70)|
11,10,48,397,70,6,8,34,3,0,0,1,0,0,0
10,11,deaths,397,89,22,11,40,3,0,0,-1,0,0,0
10,12,birth rate,99,118,29,11,8,3,0,0,0,0,0,0
10,13,average lifetime,508,104,49,11,8,3,0,0,0,0,0,0
1,14,12,6,0,0,0,0,0,64,0,-1--1--1,,1|(130,104)|
1,15,13,11,0,0,0,0,0,64,0,-1--1--1,,1|(445,95)|
12,16,0,269,22,37,9,8,4,0,0,-1,0,0,0
Comment
1,17,1,5,1,0,0,0,0,64,0,-1--1--1,,1|(203,31)|
1,18,1,10,1,0,0,0,0,64,0,-1--1--1,,1|(349,27)|
///---\\\
)MDL");
}

void TestMdlFile::absModel()
{
    Document document = importFile(QStringLiteral(QDS_ABS_MDL_PATH));
    const Stock *stock = dynamic_cast<const Stock *>(document.findByName("StockA"));
    QVERIFY(stock);
    QCOMPARE(stock->initialEquation(), QString("-10"));
    const Flow *flowA = flow(document, "FlowA");
    QVERIFY(flowA);
    QCOMPARE(flowA->equation(), QString("1"));
    QCOMPARE(document.find(flowA->endpoints().to)->id(), stock->id());
    QCOMPARE(document.find(flowA->endpoints().from)->kind(), ElementKind::Cloud);
    const ModelElement *testAbs = document.findByName("test abs");
    QVERIFY(testAbs);
    QCOMPARE(testAbs->equation(), QString("ABS(StockA)"));
    QVERIFY(connector(document, stock->id(), testAbs->id()));
    QCOMPARE(connectorCount(document), 1);
    QCOMPARE(cloudCount(document), 1);
    QCOMPARE(unconnectedCloudCount(document), 0);
    QCOMPARE(document.simulationSettings().stopTime(), 20.0);
    QCOMPARE(document.simulationSettings().dt(), 1.0);
    QCOMPARE(document.simulationSettings().saveInterval(), 1.0);
    QCOMPARE(document.simulationSettings().timeUnit(), QString("Month"));
    const SimulationCompileResult compiled = compileSimulation(document);
    QVERIFY(compiled.plan);
    const SimulationRun run = SimulationRunner(document, *compiled.plan).run();
    QCOMPARE(run.status(), SimulationRunStatus::Completed);
    QCOMPARE(run.series(testAbs->id())->front(), 10.0);
}

void TestMdlFile::teacupModel()
{
    Document document = importFile(QStringLiteral(QDS_TEACUP_MDL_PATH));
    const Stock *stock = dynamic_cast<const Stock *>(document.findByName("Teacup Temperature"));
    QVERIFY(stock);
    QCOMPARE(stock->initialEquation(), QString("180"));
    const Flow *heatLoss = flow(document, "Heat Loss to Room");
    QVERIFY(heatLoss);
    QCOMPARE(heatLoss->equation(), QString("(\"Teacup Temperature\" - \"Room Temperature\") / \"Characteristic Time\""));
    QCOMPARE(document.find(heatLoss->endpoints().from)->id(), stock->id());
    QCOMPARE(document.find(heatLoss->endpoints().to)->kind(), ElementKind::Cloud);
    QCOMPARE(document.diagram().position(stock->id()).x, 307.0);
    QCOMPARE(document.diagram().position(heatLoss->endpoints().to).x, 479.0);
    const ModelElement *room = document.findByName("Room Temperature");
    const ModelElement *characteristic = document.findByName("Characteristic Time");
    QVERIFY(connector(document, stock->id(), heatLoss->id()));
    QVERIFY(connector(document, room->id(), heatLoss->id()));
    QVERIFY(connector(document, characteristic->id(), heatLoss->id()));
    QCOMPARE(connectorCount(document), 3);
    QCOMPARE(cloudCount(document), 1);
    QCOMPARE(unconnectedCloudCount(document), 0);
    QCOMPARE(document.simulationSettings().stopTime(), 30.0);
    QCOMPARE(document.simulationSettings().dt(), 0.125);
    QCOMPARE(document.simulationSettings().saveInterval(), 0.125);
    QCOMPARE(document.simulationSettings().timeUnit(), QString("Minute"));
    QCOMPARE(document.simulationSettings().integrationMethod(), IntegrationMethod::Euler);
    const SimulationCompileResult compiled = compileSimulation(document);
    if (!compiled.plan) {
        for (const ModelIssue &issue : compiled.issues)
            qWarning() << issue.code << issue.message;
    }
    QVERIFY(compiled.plan);
    const SimulationRun run = SimulationRunner(document, *compiled.plan).run();
    QCOMPARE(run.status(), SimulationRunStatus::Completed);
    const QVector<double> *temperature = run.series(stock->id());
    QVERIFY(temperature);
    QCOMPARE(temperature->front(), 180.0);
    const double expectedFinal = 70.0 + 110.0 * std::pow(1.0 - 0.125 / 10.0, 240);
    QVERIFY(std::abs(temperature->back() - expectedFinal) < 1e-8);

    QString error;
    QVector<ModelIssue> warnings;
    Document withWarnings;
    QVERIFY(MdlFile::importFile(QStringLiteral(QDS_TEACUP_MDL_PATH), withWarnings, &error, &warnings));
    QVERIFY(!warnings.isEmpty());
    QCOMPARE(warnings.front().code, QString("mdl-range-not-preserved"));
}

void TestMdlFile::rabbitModel()
{
    Document document = import(rabbitText());
    const ModelElement *population = document.findByName("Population");
    const Flow *births = flow(document, "births");
    const Flow *deaths = flow(document, "deaths");
    const ModelElement *birthRate = document.findByName("birth rate");
    const ModelElement *lifetime = document.findByName("average lifetime");
    QVERIFY(population && births && deaths && birthRate && lifetime);
    QCOMPARE(document.find(births->endpoints().to)->id(), population->id());
    QCOMPARE(document.find(births->endpoints().from)->kind(), ElementKind::Cloud);
    QCOMPARE(document.find(deaths->endpoints().from)->id(), population->id());
    QCOMPARE(document.find(deaths->endpoints().to)->kind(), ElementKind::Cloud);
    QVERIFY(connector(document, birthRate->id(), births->id()));
    QVERIFY(connector(document, lifetime->id(), deaths->id()));
    QVERIFY(connector(document, population->id(), births->id()));
    QVERIFY(connector(document, population->id(), deaths->id()));
    QCOMPARE(connectorCount(document), 4);
    QCOMPARE(cloudCount(document), 2);
    QCOMPARE(unconnectedCloudCount(document), 0);
    QCOMPARE(document.diagram().position(births->endpoints().from).x, 91.0);
    QCOMPARE(document.diagram().position(deaths->endpoints().to).x, 487.0);
    int comments = 0;
    for (const ViewElement &element : document.diagram().elements()) {
        if (element.kind() == ElementKind::Comment) {
            ++comments;
            QCOMPARE(static_cast<const Comment &>(element).content(), QString("Comment"));
        }
    }
    QCOMPARE(comments, 1);
    const SimulationCompileResult compiled = compileSimulation(document);
    QVERIFY(compiled.plan);
    const SimulationRun run = SimulationRunner(document, *compiled.plan).run();
    QCOMPARE(run.status(), SimulationRunStatus::Completed);
    QCOMPARE(run.series(population->id())->front(), 100.0);
}

void TestMdlFile::additionalSketchViewIgnored()
{
    Document document;
    QString error;
    QVector<ModelIssue> warnings;
    QVERIFY2(MdlFile::importText(QStringLiteral(
        "x=1~~|\n"
        "\\\\\\---/// Sketch\nV300\n*View 1\n"
        "10,1,x,100,120,20,8,8,3,0,0,0,0,0,0\n"
        "*View 2\n"
        "10,1,x,900,800,20,8,8,3,0,0,0,0,0,0\n"
        "///---\\\\\\\n"), document, &error, &warnings), qPrintable(error));
    QCOMPARE(document.diagram().position(document.findByName("x")->id()).x, 100.0);
    QCOMPARE(warnings.size(), 1);
    QCOMPARE(warnings.front().code, QString("mdl-additional-view-ignored"));
}

void TestMdlFile::unicodeAndQuotedNames()
{
    const QString stockName = QStringLiteral("\u5b58\u91cf");
    const QString flowName = QStringLiteral("\u6d41\u5165");
    const QString text = QStringLiteral("%1=INTEG(%2,-10)~~|\n%2=1~~|\nresult=ABS(%1)+\"A&B\"~~|\n\"A&B\"=2~~|")
        .arg(stockName, flowName);
    Document document = import(text);
    const ModelElement *stock = document.findByName(stockName);
    QVERIFY(stock);
    const ModelElement *result = document.findByName("result");
    QVERIFY(result);
    QCOMPARE(result->equation(), QString("ABS(%1)+\"A&B\"").arg(stockName));
    QVERIFY(result->formula().root());
    QVERIFY(result->formula().issues().isEmpty());
}

void TestMdlFile::assignmentFormsAndContinuation()
{
    Document document = import(QStringLiteral(
        "Capitalized Variable=5~~|\n"
        "lowercase reference=capitalized variable~~|\n"
        "data value := 2~~|\n"
        "fixed value == 3~~|\n"
        "braced=1 + {ignored comment} 2~~|\n"
        "continued=1 + \\\n 2~~|\n"
        "documented=1~unit~company's model|\n"
        "following=4~~|\n"));
    QCOMPARE(document.findByName("lowercase reference")->equation(), QString("\"Capitalized Variable\""));
    QCOMPARE(document.findByName("data value")->equation(), QString("2"));
    QCOMPARE(document.findByName("fixed value")->equation(), QString("3"));
    QCOMPARE(document.findByName("braced")->equation(), QString("1 +  2"));
    QCOMPARE(document.findByName("continued")->equation(), QString("1 +  2"));
    QCOMPARE(document.findByName("documented")->comment(), QString("company's model"));
    QCOMPARE(document.findByName("following")->equation(), QString("4"));
}

void TestMdlFile::longestSymbolMatch()
{
    const QString apostropheName = QStringLiteral("\u00e9rosion d'action");
    Document document = import(QStringLiteral("a=1~~|\na b=2~~|\nresult=a b + a~~|\n%1=2~~|\napostrophe result=%1~~|")
        .arg(apostropheName));
    QCOMPARE(document.findByName("result")->equation(), QString("\"a b\" + a"));
    QCOMPARE(document.findByName("apostrophe result")->equation(), QString("\"%1\"").arg(apostropheName));
    QVERIFY(document.findByName("apostrophe result")->formula().issues().isEmpty());
}

void TestMdlFile::multipleElementEquations()
{
    Document document = import(QStringLiteral(
        "fruits: banana, apple, orange ~~|\n"
        "price[banana]=1.2~~|\n"
        "price[apple]=2.3~~|\n"
        "price[orange]=3.2\n"
        "~ euros/kg [0,100]\n"
        "~ Price of fruit.\n"
        "|\n"));
    const ModelElement *price = document.findByName("price");
    QVERIFY(price);
    QCOMPARE(price->dimensions().size(), 1);
    QCOMPARE(price->elementEquations().size(), std::size_t(3));
    QCOMPARE(price->equation(), QString());
    QCOMPARE(price->unit(), QString("euros/kg"));
    QCOMPARE(price->comment(), QString("Price of fruit."));
    QCOMPARE(price->elementEquations()[0].equation(), QString("1.2"));
    QCOMPARE(price->elementEquations()[1].equation(), QString("2.3"));
    QCOMPARE(price->elementEquations()[2].equation(), QString("3.2"));
}

void TestMdlFile::literalArrays()
{
    Document document = import(QStringLiteral(
        "dim: (entry1-entry3) ~~|\n"
        "column: first, second ~~|\n"
        "values[dim,column]=1,2;3,4;5,6;~~|\n"
        "partial[dim,second]=10;20;30;~~|\n"));
    const ModelElement *values = document.findByName("values");
    QVERIFY(values);
    QCOMPARE(values->dimensions().size(), 2);
    QCOMPARE(values->elementEquations().size(), std::size_t(6));
    QCOMPARE(values->equation(), QString());
    QCOMPARE(values->elementEquations()[0].equation(), QString("1"));
    QCOMPARE(values->elementEquations()[5].equation(), QString("6"));
    const ModelElement *partial = document.findByName("partial");
    QVERIFY(partial);
    QCOMPARE(partial->dimensions().size(), 2);
    QCOMPARE(partial->elementEquations().size(), std::size_t(3));
    QCOMPARE(partial->equation(), QString());
    QCOMPARE(partial->elementEquations()[0].equation(), QString("10"));
    QCOMPARE(partial->elementEquations()[2].equation(), QString("30"));
}

void TestMdlFile::arrayCorpusModels()
{
    Document oneDimensional = importFile(QStringLiteral(QDS_ARRAY_1D_MDL_PATH));
    const ModelElement *rate = oneDimensional.findByName("Rate A");
    QVERIFY(rate);
    QCOMPARE(rate->elementEquations().size(), std::size_t(3));
    QCOMPARE(rate->equation(), QString());
    Document twoDimensional = importFile(QStringLiteral(QDS_ARRAY_2D_MDL_PATH));
    const ModelElement *values = twoDimensional.findByName("Initial Values");
    QVERIFY(values);
    QCOMPARE(values->dimensions().size(), 2);
    QCOMPARE(values->elementEquations().size(), std::size_t(6));
    const Stock *stock = dynamic_cast<const Stock *>(twoDimensional.findByName("Stock A"));
    QVERIFY(stock);
    QVERIFY(stock->initialEquation().contains("\"Initial Values\""));
}

void TestMdlFile::numericRanges()
{
    Document document = import(QStringLiteral("lot: (LOT1-LOT3),LOT12,(LOT14-LOT16)~~|\nvalue[lot]=1~~|"));
    QVERIFY(document.findDimension("lot"));
    QCOMPARE(document.findDimension("lot")->elements.size(), 7);
    QCOMPARE(document.findDimension("lot")->elements[0].displayName, QString("LOT1"));
    QCOMPARE(document.findDimension("lot")->elements[6].displayName, QString("LOT16"));
}

void TestMdlFile::macroModel()
{
    Document document = importFile(QStringLiteral(QDS_MACRO_MDL_PATH));
    QCOMPARE(document.macros().size(), std::size_t(1));
    const MacroDefinition &macro = *document.macros().begin();
    QCOMPARE(macro.displayName(), QString("EXPRESSION MACRO"));
    QCOMPARE(macro.equation(), QString("input * parameter"));
    const ModelElement *output = document.findByName("macro output");
    QVERIFY(output);
    QCOMPARE(output->equation(), QString("\"EXPRESSION MACRO\"(\"macro input\",\"macro parameter\")"));
    QVERIFY(output->formula().root());
    QVERIFY(output->formula().issues().isEmpty());
    EvaluationContext context;
    context.readMacro = makeMacroReader(document);
    context.readVariable = [&document](const QString &name) -> std::optional<double> {
        const ModelElement *element = document.findByName(name);
        return element ? element->equation().toDouble() : std::optional<double> {};
    };
    const EvaluationResult result = evaluateFormula(output->formula().root(), context);
    QVERIFY(result.value);
    QCOMPARE(std::get<double>(*result.value), 5.5);
}

void TestMdlFile::specialCharacterModel()
{
    Document document = importFile(QStringLiteral(QDS_SPECIAL_MDL_PATH));
    const Stock *stock = dynamic_cast<const Stock *>(document.findByName(QString("Stock with \n Newline Character")));
    QVERIFY(stock);
    QCOMPARE(stock->initialEquation(), QString("67"));
    QVERIFY(document.findByName("Flow*with a few^arithmetic + characters"));
    for (const ModelElement &element : document.model().elements()) {
        QVERIFY2(element.formula().issues().isEmpty(), qPrintable(element.displayName()));
        for (const ArrayElementFormula &entry : element.elementEquations())
            QVERIFY(entry.formula().issues().isEmpty());
    }
}

void TestMdlFile::complexNetRate()
{
    Document document = import(QStringLiteral("input=2~~|\nStock=INTEG(3*input,10)~~|"));
    const Flow *net = flow(document, "Stock net flow");
    QVERIFY(net);
    QCOMPARE(net->equation(), QString("3*input"));
    const ModelElement *stock = document.findByName("Stock");
    QCOMPARE(document.find(net->endpoints().to)->id(), stock->id());
    const SimulationCompileResult compiled = compileSimulation(document);
    QVERIFY(compiled.plan);
    const SimulationRun run = SimulationRunner(document, *compiled.plan).run();
    QCOMPARE(run.status(), SimulationRunStatus::Completed);
    QCOMPARE(run.series(stock->id())->front(), 10.0);
}

void TestMdlFile::openCloudLifecycle()
{
    Document document = importFile(QStringLiteral(QDS_WFINV_MDL_PATH));
    QCOMPARE(cloudCount(document), 3);
    QCOMPARE(unconnectedCloudCount(document), 0);
    const ModelElement *inventory = document.findByName("Inventory");
    const ModelElement *workforce = document.findByName("Workforce");
    const Flow *production = flow(document, "production");
    const Flow *sales = flow(document, "sales");
    const Flow *hiring = flow(document, "net hire rate");
    QVERIFY(inventory && workforce && production && sales && hiring);
    QCOMPARE(document.find(production->endpoints().from)->kind(), ElementKind::Cloud);
    QCOMPARE(document.find(production->endpoints().to)->id(), inventory->id());
    QCOMPARE(document.find(sales->endpoints().from)->id(), inventory->id());
    QCOMPARE(document.find(sales->endpoints().to)->kind(), ElementKind::Cloud);
    QCOMPARE(document.find(hiring->endpoints().from)->kind(), ElementKind::Cloud);
    QCOMPARE(document.find(hiring->endpoints().to)->id(), workforce->id());
}

void TestMdlFile::invalidInput_data()
{
    QTest::addColumn<QString>("text");
    QTest::newRow("empty") << QString();
    QTest::newRow("duplicate scalar") << QStringLiteral("x=1~~|\nx=2~~|");
    QTest::newRow("duplicate array element") << QStringLiteral("d:a,b~~|\nx[a]=1~~|\nx[a]=2~~|");
    QTest::newRow("exception") << QStringLiteral("d:a,b~~|\nx[d]:EXCEPT:[a]=1~~|\nx[a]=2~~|");
    QTest::newRow("mapping") << QStringLiteral("d:a,b->other~~|\nx=1~~|");
    QTest::newRow("macro internal") << QStringLiteral(":MACRO: M(x)\nM=x~~|\ninternal=1~~|\n:END OF MACRO:\ny=M(1)~~|");
    QTest::newRow("bad integ") << QStringLiteral("x=INTEG(1)~~|");
    QTest::newRow("bad control") << QStringLiteral("x=1~~|\nFINAL TIME=unknown~~|");
    QTest::newRow("unterminated quote") << QStringLiteral("x=\"unterminated~~|");
    QTest::newRow("unknown subscript") << QStringLiteral("x[missing]=1~~|");
    QTest::newRow("mixed apply and element") << QStringLiteral("d:a,b~~|\nx[d]=1~~|\nx[a]=2~~|");
    QTest::newRow("normalized duplicate") << QStringLiteral("A B=1~~|\na_b=2~~|");
    QTest::newRow("unsupported directive") << QStringLiteral(":THE CONDITION: x>0~~|\nx=1~~|");
    QTest::newRow("subrange") << QStringLiteral("part:a,b~~|\nfull:part~~|\nx=1~~|");
    QTest::newRow("string variable") << QStringLiteral("greeting :IS: \"hello\"~~|\nx=1~~|");
}

void TestMdlFile::invalidInput()
{
    QFETCH(QString, text);
    Document document;
    const EntityId existing = document.createVariable(ElementKind::Variable);
    QVERIFY(document.rename(existing, "Existing"));
    QString error;
    QVector<ModelIssue> warnings;
    QVERIFY2(!MdlFile::importText(text, document, &error, &warnings), qPrintable(text));
    QVERIFY(!error.isEmpty());
    QVERIFY(document.findByName("Existing"));
}

void TestMdlFile::documentFileRoundTrip()
{
    Document document;
    qds::io::Workspace workspace;
    QString error;
    bool native = true;
    QVERIFY2(qds::io::DocumentFile::open(QStringLiteral(QDS_TEACUP_MDL_PATH), document, workspace, &error, &native),
        qPrintable(error));
    QVERIFY(!native);
    const ModelElement *heatLoss = document.findByName("Heat Loss to Room");
    QVERIFY(heatLoss);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("teacup.qds"));
    QVERIFY2(qds::io::DocumentFile::save(path, document, workspace, &error), qPrintable(error));
    Document loaded;
    QVERIFY2(qds::io::DocumentFile::open(path, loaded, workspace, &error, &native), qPrintable(error));
    QVERIFY(native);
    QVERIFY(loaded.findByName("Heat Loss to Room"));
    QCOMPARE(loaded.findByName("Heat Loss to Room")->equation(), heatLoss->equation());
    QCOMPARE(loaded.simulationSettings(), document.simulationSettings());
}

} // namespace

QTEST_APPLESS_MAIN(TestMdlFile)
#include "TestMdlFile.moc"
