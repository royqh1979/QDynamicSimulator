#include <qds/core/Document.h>
#include <qds/core/Evaluation.h>
#include <qds/io/DocumentFile.h>
#include <qds/io/XmileFile.h>

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace qds::core;

namespace {

class TestArrayFiles : public QObject
{
    Q_OBJECT

private slots:
    void arrayRoundTrips();
    void realArrayImports();
};

void verifyArrayDefinitions(const Document &document)
{
    QCOMPARE(document.dimensions().size(), std::size_t(2));
    const Dimension *location = document.findDimension("Location");
    const Dimension *number = document.findDimension("N");
    QVERIFY(location && number);
    QCOMPARE(location->elements.size(), std::size_t(2));
    QCOMPARE(location->elements[1].nameKey, std::string("new_york"));
    QCOMPARE(number->elements.size(), std::size_t(2));

    const auto *apply = document.model().findByName("Apply");
    const auto *individual = document.model().findByName("Individual");
    QVERIFY(apply && individual);
    QCOMPARE(apply->dimensions, (std::vector<std::string> {"location", "n"}));
    QCOMPARE(apply->equation, std::string("1"));
    QVERIFY(apply->elementEquations.empty());
    QCOMPARE(individual->dimensions, std::vector<std::string> {"location"});
    QCOMPARE(individual->elementEquations.size(), std::size_t(2));
    QCOMPARE(individual->elementEquations[0].indices, std::vector<std::size_t> {0});
    QCOMPARE(individual->elementEquations[0].equation, std::string("10"));
    QCOMPARE(individual->elementEquations[1].indices, std::vector<std::size_t> {1});
    QCOMPARE(individual->elementEquations[1].equation, std::string("20"));
}

void TestArrayFiles::arrayRoundTrips()
{
    Document document;
    document.defineDimension("Location", {"Boston", "New York"});
    document.defineDimension("N", 2);
    const EntityId apply = document.createVariable(ElementKind::Auxiliary);
    const EntityId individual = document.createVariable(ElementKind::Auxiliary);
    document.rename(apply, "Apply");
    document.rename(individual, "Individual");
    document.setDimensions(apply, {"Location", "N"});
    document.setEquation(apply, "1");
    document.setDimensions(individual, {"Location"});
    document.setElementEquation(individual, {0}, "10");
    document.setElementEquation(individual, {1}, "20");

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QString error;
    const QString nativePath = directory.filePath(QStringLiteral("arrays.qds"));
    QVERIFY2(qds::io::DocumentFile::save(nativePath, document, &error), qPrintable(error));
    Document native;
    QVERIFY2(qds::io::DocumentFile::open(nativePath, native, &error), qPrintable(error));
    verifyArrayDefinitions(native);

    const QString xmilePath = directory.filePath(QStringLiteral("arrays.xmile"));
    QVERIFY2(qds::io::XmileFile::exportFile(xmilePath, document, &error), qPrintable(error));
    QFile file(xmilePath);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray xml = file.readAll();
    QVERIFY(xml.contains("<uses_arrays maximum_dimensions=\"2\""));
    QVERIFY(xml.contains("<dim name=\"N\" size=\"2\""));
    QVERIFY(xml.contains("subscript=\"&quot;New York&quot;\""));
    file.close();

    Document xmile;
    QVERIFY2(qds::io::XmileFile::importFile(xmilePath, xmile, &error), qPrintable(error));
    verifyArrayDefinitions(xmile);
}

void TestArrayFiles::realArrayImports()
{
    QString error;
    Document apply;
    QVERIFY2(qds::io::XmileFile::importFile(QStringLiteral(QDS_ARRAY_APPLY_XMILE_PATH), apply, &error), qPrintable(error));
    const Dimension *dimension = apply.findDimension("dim1");
    QVERIFY(dimension);
    QCOMPARE(dimension->elements.size(), std::size_t(3));
    const auto *array = apply.model().findByName("var1");
    QVERIFY(array);
    QCOMPARE(array->dimensions, std::vector<std::string> {"dim1"});
    QCOMPARE(array->equation, std::string("1, 2, 3"));
    const FormulaAnalysis literal = analyzeFormula(array->equation);
    QVERIFY(literal.issues.empty());
    QVERIFY(dynamic_cast<const ArrayLiteralNode *>(literal.ast.get()));
    EvaluationContext context;
    context.readDimension = [&apply](const std::string &name) { return apply.findDimension(name); };
    context.scope.dimensions = {"dim1"};
    const EvaluationResult evaluated = evaluateFormula(literal.ast.get(), context);
    const auto *values = evaluated.value ? std::get_if<ArrayValue>(&*evaluated.value) : nullptr;
    QVERIFY(values);
    QCOMPARE(values->values, (std::vector<double> {1, 2, 3}));

    Document individual;
    QVERIFY2(qds::io::XmileFile::importFile(QStringLiteral(QDS_ARRAY_INDIVIDUAL_XMILE_PATH), individual, &error), qPrintable(error));
    const auto *rate = individual.model().findByName("Rate A");
    QVERIFY(rate);
    QCOMPARE(rate->elementEquations.size(), std::size_t(3));
    QCOMPARE(rate->elementEquations[1].indices, std::vector<std::size_t> {1});
    QCOMPARE(rate->elementEquations[2].equation, std::string("0.3"));

    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("individual.xmile"));
    QVERIFY2(qds::io::XmileFile::exportFile(path, individual, &error), qPrintable(error));
    Document reimported;
    QVERIFY2(qds::io::XmileFile::importFile(path, reimported, &error), qPrintable(error));
    rate = reimported.model().findByName("Rate A");
    QVERIFY(rate);
    QCOMPARE(rate->elementEquations.size(), std::size_t(3));
    QCOMPARE(rate->elementEquations[0].equation, std::string("0.01"));
}

} // namespace

QTEST_APPLESS_MAIN(TestArrayFiles)
#include "TestArrayFiles.moc"
