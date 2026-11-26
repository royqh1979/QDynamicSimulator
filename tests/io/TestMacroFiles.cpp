#include <core/Analysis.h>
#include <core/Evaluation.h>
#include <io/DocumentFile.h>
#include <io/XmileFile.h>

#include <algorithm>

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace qds::core;

namespace {

class TestMacroFiles : public QObject
{
    Q_OBJECT

private slots:
    void macroRoundTrips();
};

void verifyMacro(const Document &document)
{
    QCOMPARE(document.macros().size(), std::size_t(1));
    const MacroDefinition &macro = document.macros().front();
    QCOMPARE(macro.displayName, std::string("Scale"));
    QCOMPARE(macro.parameters.size(), std::size_t(2));
    QCOMPARE(macro.parameters[0].nameKey, std::string("x"));
    QCOMPARE(macro.parameters[1].defaultEquation, std::string("2"));
    QCOMPARE(macro.equation, std::string("x * factor"));
    const ModelAnalysis analysis = analyzeDocument(document);
    const auto found = std::find_if(analysis.elements.begin(), analysis.elements.end(), [](const ElementAnalysis &entry) {
        return entry.formula.ast != nullptr;
    });
    const EvaluationResult result = evaluateFormula(found->formula.ast.get(), {});
    const auto *scalar = result.value ? std::get_if<double>(&*result.value) : nullptr;
    QVERIFY(scalar);
    QCOMPARE(*scalar, 10.0);
}

void TestMacroFiles::macroRoundTrips()
{
    Document document;
    MacroDefinition macro;
    macro.displayName = "Scale";
    macro.parameters = {{"x", "x", {}}, {"factor", "factor", "2"}};
    macro.equation = "x * factor";
    QVERIFY(document.defineMacro(macro));
    const EntityId output = document.createVariable(ElementKind::Auxiliary);
    document.setEquation(output, "Scale(5)");

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QString error;
    const QString nativePath = directory.filePath(QStringLiteral("macro.qds"));
    QVERIFY2(qds::io::DocumentFile::save(nativePath, document, &error), qPrintable(error));
    Document native;
    QVERIFY2(qds::io::DocumentFile::open(nativePath, native, &error), qPrintable(error));
    verifyMacro(native);

    const QString xmilePath = directory.filePath(QStringLiteral("macro.xmile"));
    QVERIFY2(qds::io::XmileFile::exportFile(xmilePath, document, &error), qPrintable(error));
    QFile file(xmilePath);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray xml = file.readAll();
    QVERIFY(xml.contains("<uses_macros"));
    QVERIFY(xml.contains("<parm default=\"2\">factor</parm>"));
    file.close();
    Document xmile;
    QVERIFY2(qds::io::XmileFile::importFile(xmilePath, xmile, &error), qPrintable(error));
    verifyMacro(xmile);
}

} // namespace

QTEST_APPLESS_MAIN(TestMacroFiles)
#include "TestMacroFiles.moc"
