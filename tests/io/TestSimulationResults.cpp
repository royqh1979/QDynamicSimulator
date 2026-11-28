#include <core/Identifier.h>
#include <core/simulation/Simulation.h>
#include <io/XmileFile.h>
#include <io/MdlFile.h>

#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <QtTest>

#include <algorithm>
#include <cmath>

using namespace qds::core;
using qds::io::XmileFile;

namespace {

struct ReferenceData {
    QVector<double> times;
    QHash<QString, QVector<double>> columns;
};

ReferenceData readReference(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QString text = QString::fromUtf8(file.readAll());
    const QStringList lines = text.split(QRegularExpression(QStringLiteral("\\r\\n|\\n|\\r")), Qt::SkipEmptyParts);
    if (lines.isEmpty())
        return {};
    const QChar delimiter = lines.front().contains('\t') ? QChar('\t') : QChar(',');
    const QStringList headers = lines.front().split(delimiter, Qt::KeepEmptyParts);
    ReferenceData result;
    for (const QString &header : headers)
        result.columns.insert(normalizeIdentifier(header), {});
    for (int row = 1; row < lines.size(); ++row) {
        const QStringList fields = lines[row].split(delimiter, Qt::KeepEmptyParts);
        if (fields.size() != headers.size())
            return {};
        for (int column = 0; column < headers.size(); ++column) {
            bool valid = false;
            const double value = fields[column].trimmed().toDouble(&valid);
            if (!valid)
                return {};
            const QString key = normalizeIdentifier(headers[column]);
            if (column == 0 || key == "time" || key == "months")
                result.times.push_back(value);
            result.columns[key].push_back(value);
        }
    }
    return result;
}

void compareReference(const Document &document, const SimulationRun &run, const ReferenceData &reference)
{
    QCOMPARE(run.status(), SimulationRunStatus::Completed);
    QCOMPARE(run.times().size(), reference.times.size());
    for (int row = 0; row < reference.times.size(); ++row) {
        const double magnitude = reference.times[row] == 0.0
            ? 1.0 : std::pow(10.0, std::floor(std::log10(std::abs(reference.times[row]))));
        const double timeTolerance = std::max(1e-9, magnitude * 5.1e-6);
        QVERIFY2(std::abs(run.times()[row] - reference.times[row]) <= timeTolerance,
            qPrintable(QStringLiteral("Time mismatch at row %1: actual=%2 expected=%3 tolerance=%4")
                .arg(row).arg(run.times()[row]).arg(reference.times[row]).arg(timeTolerance)));
    }
    const QSet<QString> ignored {"initial_time", "final_time", "time_step", "saveper", "time", "months"};
    for (auto column = reference.columns.constBegin(); column != reference.columns.constEnd(); ++column) {
        if (ignored.contains(column.key()))
            continue;
        const ModelElement *element = document.model().findByName(column.key());
        QVERIFY2(element, qPrintable("Missing model column: " + column.key()));
        const QVector<double> actual = run.series(element->nameKey());
        QVERIFY(!actual.isEmpty());
        QCOMPARE(actual.size(), reference.times.size());
        for (int row = 0; row < actual.size(); ++row) {
            const double expected = column.value()[row];
            const double tolerance = std::max(2e-6, 1e-4 * std::abs(expected));
            QVERIFY2(std::abs(actual.at(row) - expected) <= tolerance,
                qPrintable(QStringLiteral("%1 at row %2: actual=%3 expected=%4 tolerance=%5")
                    .arg(column.key()).arg(row).arg(actual.at(row)).arg(expected).arg(tolerance)));
        }
    }
}

void verifyReferenceModel(const QString &modelPath, const QString &referencePath)
{
    Document document;
    QString error;
    QVERIFY2(XmileFile::importFile(modelPath, document, &error), qPrintable(error));
    const ReferenceData reference = readReference(referencePath);
    QVERIFY(!reference.times.isEmpty());
    const SimulationCompileResult compiled = compileSimulation(document);
    if (!compiled.plan) {
        for (const ModelIssue &issue : compiled.issues)
            qWarning() << issue.code << issue.message;
        QVERIFY(compiled.plan.has_value());
        return;
    }
    const SimulationRun run = SimulationRunner(document, *compiled.plan).run();
    compareReference(document, run, reference);
}

void verifyMdlReferenceModel(const QString &modelPath, const QString &referencePath)
{
    Document document;
    QString error;
    QVERIFY2(qds::io::MdlFile::importFile(modelPath, document, &error), qPrintable(error));
    const ReferenceData reference = readReference(referencePath);
    QVERIFY(!reference.times.isEmpty());
    const SimulationCompileResult compiled = compileSimulation(document);
    if (!compiled.plan) {
        for (const ModelIssue &issue : compiled.issues)
            qWarning() << issue.code << issue.message;
        QVERIFY(compiled.plan.has_value());
        return;
    }
    const SimulationRun run = SimulationRunner(document, *compiled.plan).run();
    compareReference(document, run, reference);
}

class TestSimulationResults : public QObject
{
    Q_OBJECT

private slots:
    void absModel();
    void evaluationOrderModel();
    void teacupModel();
    void chainedInitializationModel();
    void sirModel();
    void lookupModel();
    void lookupXscaleModel();
    void inlineLookupModel();
    void timeInputModel();
};

void TestSimulationResults::absModel()
{
    verifyReferenceModel(QStringLiteral(QDS_ABS_XMILE_PATH), QStringLiteral(QDS_ABS_OUTPUT_PATH));
}

void TestSimulationResults::evaluationOrderModel()
{
    verifyReferenceModel(QStringLiteral(QDS_EVAL_ORDER_XMILE_PATH), QStringLiteral(QDS_EVAL_ORDER_OUTPUT_PATH));
}

void TestSimulationResults::teacupModel()
{
    verifyReferenceModel(QStringLiteral(QDS_TEACUP_XMILE_PATH), QStringLiteral(QDS_TEACUP_OUTPUT_PATH));
}

void TestSimulationResults::chainedInitializationModel()
{
    verifyReferenceModel(QStringLiteral(QDS_CHAINED_INIT_XMILE_PATH), QStringLiteral(QDS_CHAINED_INIT_OUTPUT_PATH));
}

void TestSimulationResults::sirModel()
{
    verifyReferenceModel(QStringLiteral(QDS_SIR_MODEL_XMILE_PATH), QStringLiteral(QDS_SIR_MODEL_OUTPUT_PATH));
    Document document;
    QString error;
    QVERIFY(XmileFile::importFile(QStringLiteral(QDS_SIR_MODEL_XMILE_PATH), document, &error));
    const SimulationCompileResult compiled = compileSimulation(document);
    QVERIFY(compiled.plan);
    const SimulationRun run = SimulationRunner(document, *compiled.plan).run();
    const QVector<double> susceptible = run.series("susceptible");
    const QVector<double> infectious = run.series("infectious");
    const QVector<double> recovered = run.series("recovered");
    const double initialPopulation = susceptible.front() + infectious.front() + recovered.front();
    QCOMPARE(initialPopulation, 1005.0);
    for (int index = 0; index < run.times().size(); ++index) {
        QVERIFY(std::abs(susceptible.at(index) + infectious.at(index) + recovered.at(index) - initialPopulation) < 1e-8);
    }
}

void TestSimulationResults::lookupModel()
{
    verifyReferenceModel(QStringLiteral(QDS_LOOKUP_XMILE_PATH), QStringLiteral(QDS_LOOKUP_OUTPUT_PATH));
}

void TestSimulationResults::lookupXscaleModel()
{
    verifyReferenceModel(QStringLiteral(QDS_LOOKUP_XSCALE_XMILE_PATH), QStringLiteral(QDS_LOOKUP_OUTPUT_PATH));
}

void TestSimulationResults::inlineLookupModel()
{
    verifyReferenceModel(QStringLiteral(QDS_INLINE_LOOKUP_XMILE_PATH), QStringLiteral(QDS_INLINE_LOOKUP_OUTPUT_PATH));
}

void TestSimulationResults::timeInputModel()
{
    verifyMdlReferenceModel(QStringLiteral(QDS_TIME_INPUT_MDL_PATH), QStringLiteral(QDS_TIME_INPUT_OUTPUT_PATH));
}

} // namespace

QTEST_APPLESS_MAIN(TestSimulationResults)
#include "TestSimulationResults.moc"
