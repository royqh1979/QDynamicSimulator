#include <core/formula/Analysis.h>
#include <core/formula/Evaluation.h>
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
    const MacroDefinition &macro = *document.macros().begin();
    QCOMPARE(macro.displayName(), QString("Scale"));
    QCOMPARE(macro.parameters().size(), std::size_t(2));
    QCOMPARE(macro.parameters()[0].nameKey(), QString("x"));
    QCOMPARE(macro.parameters()[1].defaultEquation(), QString("2"));
    QCOMPARE(macro.equation(), QString("x * factor"));
    const ModelElement *output = nullptr;
    for (const ModelElement &entry : document.model().elements()) {
        if (entry.formula().root())
            output = &entry;
    }
    QVERIFY(output);
    EvaluationContext context;
    context.readMacro = makeMacroReader(document);
    const EvaluationResult result = evaluateFormula(output->formula().root(), context);
    const auto *scalar = result.value ? std::get_if<double>(&*result.value) : nullptr;
    QVERIFY(scalar);
    QCOMPARE(*scalar, 10.0);
}

void TestMacroFiles::macroRoundTrips()
{
    Document document;
    MacroDefinitionData macro;
    macro.displayName = "Scale";
    macro.parameters = {{"x", {}}, {"factor", "2"}};
    macro.equation = "x * factor";
    QVERIFY(document.defineMacro(macro));
    const EntityId output = document.createVariable(ElementKind::Auxiliary);
    document.setEquation(output, "Scale(5)");

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QString error;
    qds::io::Workspace workspace;
    const QString nativePath = directory.filePath(QStringLiteral("macro.qds"));
    QVERIFY2(qds::io::DocumentFile::save(nativePath, document, workspace, &error), qPrintable(error));
    Document native;
    QVERIFY2(qds::io::DocumentFile::open(nativePath, native, workspace, &error), qPrintable(error));
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
