#include <core/simulation/Simulation.h>

#include <QtTest>

#include <algorithm>
#include <cmath>
#include <functional>

using namespace qds::core;

namespace {

SimulationRun runModel(const Document &document, const SimulationRunner::Cancellation &cancelled = {})
{
    SimulationCompileResult compiled = compileSimulation(document);
    if (!compiled.plan) {
        QStringList messages;
        for (const ModelIssue &issue : compiled.issues)
            messages.push_back(issue.code);
        qFatal("Unexpected compilation failure: %s", qPrintable(messages.join(',')));
    }
    return SimulationRunner(document, *compiled.plan).run(cancelled);
}

bool hasIssue(const SimulationCompileResult &result, const QString &code)
{
    return std::any_of(result.issues.begin(), result.issues.end(), [&code](const ModelIssue &issue) {
        return issue.code == code;
    });
}

void expectNear(double actual, double expected, double tolerance)
{
    QVERIFY2(std::abs(actual - expected) <= tolerance,
        qPrintable(QStringLiteral("actual=%1 expected=%2 tolerance=%3").arg(actual).arg(expected).arg(tolerance)));
}

Document exponentialDocument(double dt)
{
    Document document;
    const EntityId stock = document.createVariable(ElementKind::Stock);
    const EntityId cloud = document.createCloud();
    const EntityId flow = document.createFlow(cloud, stock);
    document.rename(stock, "Stock");
    document.setInitialEquation(stock, "1");
    document.setEquation(flow, "Stock");
    document.setSimulationSettings({0.0, 1.0, dt, dt});
    return document;
}

class TestSimulation : public QObject
{
    Q_OBJECT

private slots:
    void initializationAndEvaluationOrder();
    void stockFlowsAndConservation();
    void exponentialEulerAndConvergence();
    void logisticAndOscillatorBenchmarks();
    void saveIntervalsAndTimeBounds();
    void macrosAndResultAccess();
    void rungeKutta4();
    void rungeKutta4StageFailure();
    void compilationDiagnostics();
    void cancellationAndRuntimeFailure();
};

void TestSimulation::initializationAndEvaluationOrder()
{
    Document document;
    const EntityId stock = document.createVariable(ElementKind::Stock);
    const EntityId auxiliary = document.createVariable(ElementKind::Variable);
    const EntityId offset = document.createVariable(ElementKind::Variable);
    const EntityId base = document.createVariable(ElementKind::Variable);
    document.rename(stock, "Stock");
    document.rename(auxiliary, "Twice");
    document.rename(offset, "Offset");
    document.rename(base, "Base");
    document.setInitialEquation(stock, "Offset + Base");
    document.setEquation(auxiliary, "Stock * 2");
    document.setEquation(offset, "3");
    document.setEquation(base, "2");
    document.setSimulationSettings({0.0, 0.0, 1.0, 1.0});

    const SimulationRun run = runModel(document);
    QCOMPARE(run.status(), SimulationRunStatus::Completed);
    QCOMPARE(run.times(), QVector<double> {0.0});
    QCOMPARE(run.endTime(), std::optional<double>(0.0));
    QCOMPARE(*run.series(stock), QVector<double> {5.0});
    QCOMPARE(*run.series(auxiliary), QVector<double> {10.0});
    QCOMPARE(*run.series(offset), QVector<double> {3.0});
    QVERIFY(run.series(99999) == nullptr);
}

void TestSimulation::stockFlowsAndConservation()
{
    Document document;
    const EntityId source = document.createVariable(ElementKind::Stock);
    const EntityId target = document.createVariable(ElementKind::Stock);
    const EntityId flow = document.createFlow(source, target);
    document.rename(source, "Source");
    document.rename(target, "Target");
    document.setInitialEquation(source, "10");
    document.setInitialEquation(target, "0");
    document.setEquation(flow, "2");
    document.setSimulationSettings({0.0, 2.0, 1.0, 1.0});

    const SimulationRun run = runModel(document);
    QCOMPARE(run.status(), SimulationRunStatus::Completed);
    QCOMPARE(run.times(), (QVector<double> {0.0, 1.0, 2.0}));
    QCOMPARE(*run.series(source), (QVector<double> {10.0, 8.0, 6.0}));
    QCOMPARE(*run.series(target), (QVector<double> {0.0, 2.0, 4.0}));
    QCOMPARE(*run.series(flow), (QVector<double> {2.0, 2.0, 2.0}));
    for (int index = 0; index < run.times().size(); ++index)
        QCOMPARE(run.series(source)->at(index) + run.series(target)->at(index), 10.0);
}

void TestSimulation::exponentialEulerAndConvergence()
{
    const Document coarseDocument = exponentialDocument(0.1);
    const SimulationRun coarse = runModel(coarseDocument);
    QCOMPARE(coarse.status(), SimulationRunStatus::Completed);
    QCOMPARE(coarse.times().size(), 11);
    const auto stocks = coarseDocument.model().findByName("Stock");
    QVERIFY(stocks);
    const double expected = std::pow(1.1, 10);
    expectNear(coarse.series(stocks->id())->back(), expected, 1e-12);

    const Document fineDocument = exponentialDocument(0.05);
    const SimulationRun fine = runModel(fineDocument);
    QCOMPARE(fine.times().size(), 21);
    const double analytic = std::exp(1.0);
    const double coarseError = std::abs(coarse.series(stocks->id())->back() - analytic);
    const double fineError = std::abs(fine.series(stocks->id())->back() - analytic);
    QVERIFY(coarseError > fineError * 1.8);
    QVERIFY(coarseError < fineError * 2.2);
}

void TestSimulation::logisticAndOscillatorBenchmarks()
{
    Document logistic;
    const EntityId logisticStock = logistic.createVariable(ElementKind::Stock);
    const EntityId logisticCloud = logistic.createCloud();
    const EntityId logisticFlow = logistic.createFlow(logisticCloud, logisticStock);
    logistic.rename(logisticStock, "Stock");
    logistic.setInitialEquation(logisticStock, "1");
    logistic.setEquation(logisticFlow, "Stock * (1 - Stock / 10)");
    logistic.setSimulationSettings({0.0, 1.0, 0.01, 0.01});
    const SimulationRun logisticRun = runModel(logistic);
    QCOMPARE(logisticRun.status(), SimulationRunStatus::Completed);
    const double logisticExpected = 10.0 / (1.0 + 9.0 * std::exp(-1.0));
    expectNear(logisticRun.series(logisticStock)->back(), logisticExpected, 0.01);

    Document oscillator;
    const EntityId position = oscillator.createVariable(ElementKind::Stock);
    const EntityId velocity = oscillator.createVariable(ElementKind::Stock);
    const EntityId cloud = oscillator.createCloud();
    const EntityId positionFlow = oscillator.createFlow(cloud, position);
    const EntityId velocityFlow = oscillator.createFlow(cloud, velocity);
    oscillator.rename(position, "Position");
    oscillator.rename(velocity, "Velocity");
    oscillator.setInitialEquation(position, "1");
    oscillator.setInitialEquation(velocity, "0");
    oscillator.setEquation(positionFlow, "Velocity");
    oscillator.setEquation(velocityFlow, "-Position - 0.2 * Velocity");
    oscillator.setSimulationSettings({0.0, 1.0, 0.01, 0.01});
    const SimulationRun oscillatorRun = runModel(oscillator);
    QCOMPARE(oscillatorRun.status(), SimulationRunStatus::Completed);
    const double omega = std::sqrt(0.99);
    const double positionExpected = std::exp(-0.1) * (std::cos(omega) + 0.1 / omega * std::sin(omega));
    const double velocityExpected = std::exp(-0.1)
        * (-0.1 * (std::cos(omega) + 0.1 / omega * std::sin(omega))
            - omega * std::sin(omega) + 0.1 * std::cos(omega));
    expectNear(oscillatorRun.series(position)->back(), positionExpected, 0.006);
    expectNear(oscillatorRun.series(velocity)->back(), velocityExpected, 0.006);
}

void TestSimulation::saveIntervalsAndTimeBounds()
{
    Document document;
    const EntityId stock = document.createVariable(ElementKind::Stock);
    const EntityId cloud = document.createCloud();
    const EntityId flow = document.createFlow(cloud, stock);
    document.setInitialEquation(stock, "0");
    document.setEquation(flow, "1");
    QVERIFY(document.setSimulationSettings({0.0, 1.0, 0.3, 0.6}));
    SimulationRun run = runModel(document);
    QCOMPARE(run.status(), SimulationRunStatus::Completed);
    QCOMPARE(run.times(), (QVector<double> {0.0, 0.6, 1.0}));
    QCOMPARE(*run.series(stock), (QVector<double> {0.0, 0.6, 1.0}));

    QVERIFY(document.setSimulationSettings({2.0, 2.0, 0.3, 0.6}));
    run = runModel(document);
    QCOMPARE(run.status(), SimulationRunStatus::Completed);
    QCOMPARE(run.times(), QVector<double> {2.0});
    QCOMPARE(*run.series(stock), QVector<double> {0.0});
}

void TestSimulation::macrosAndResultAccess()
{
    Document document;
    MacroDefinitionData scale;
    scale.displayName = "Scale";
    scale.parameters = {{"x", {}}, {"factor", "2"}};
    scale.equation = "x * factor";
    QVERIFY(document.defineMacro(scale));
    const EntityId stock = document.createVariable(ElementKind::Stock);
    const EntityId cloud = document.createCloud();
    const EntityId flow = document.createFlow(cloud, stock);
    document.rename(stock, "Stock");
    document.setInitialEquation(stock, "1");
    document.setEquation(flow, "Scale(Stock)");
    document.setSimulationSettings({0.0, 1.0, 1.0, 1.0});
    const SimulationRun run = runModel(document);
    QCOMPARE(run.status(), SimulationRunStatus::Completed);
    QCOMPARE(*run.series(flow), (QVector<double> {2.0, 6.0}));
    QCOMPARE(*run.series(stock), (QVector<double> {1.0, 3.0}));
}

void TestSimulation::rungeKutta4()
{
    Document coarseDocument = exponentialDocument(0.25);
    QVERIFY(coarseDocument.setSimulationSettings({0.0, 1.0, 0.25, 0.25, IntegrationMethod::RungeKutta4}));
    const SimulationRun coarse = runModel(coarseDocument);
    QCOMPARE(coarse.status(), SimulationRunStatus::Completed);
    QCOMPARE(coarse.settings().integrationMethod(), IntegrationMethod::RungeKutta4);
    QCOMPARE(coarse.times().size(), 5);
    const EntityId stock = coarseDocument.findByName("Stock")->id();
    const double analytic = std::exp(1.0);
    expectNear(coarse.series(stock)->back(), analytic, 0.001);

    Document fineDocument = exponentialDocument(0.125);
    QVERIFY(fineDocument.setSimulationSettings({0.0, 1.0, 0.125, 0.125, IntegrationMethod::RungeKutta4}));
    const SimulationRun fine = runModel(fineDocument);
    QCOMPARE(fine.times().size(), 9);
    const double coarseError = std::abs(coarse.series(stock)->back() - analytic);
    const double fineError = std::abs(fine.series(stock)->back() - analytic);
    QVERIFY(coarseError > fineError * 14.0);
    QVERIFY(coarseError < fineError * 18.0);

    Document timeDocument;
    const EntityId timeStock = timeDocument.createVariable(ElementKind::Stock);
    const EntityId cloud = timeDocument.createCloud();
    const EntityId timeFlow = timeDocument.createFlow(cloud, timeStock);
    timeDocument.setInitialEquation(timeStock, "0");
    timeDocument.setEquation(timeFlow, "TIME");
    QVERIFY(timeDocument.setSimulationSettings({0.0, 1.0, 1.0, 1.0, IntegrationMethod::RungeKutta4}));
    const SimulationRun timeRun = runModel(timeDocument);
    QCOMPARE(timeRun.status(), SimulationRunStatus::Completed);
    QCOMPARE(*timeRun.series(timeStock), (QVector<double> {0.0, 0.5}));
}

void TestSimulation::rungeKutta4StageFailure()
{
    Document document;
    const EntityId stock = document.createVariable(ElementKind::Stock);
    const EntityId cloud = document.createCloud();
    const EntityId flow = document.createFlow(cloud, stock);
    document.setInitialEquation(stock, "1");
    document.setEquation(flow, "1 / (TIME - 0.5)");
    QVERIFY(document.setSimulationSettings({0.0, 1.0, 1.0, 1.0, IntegrationMethod::RungeKutta4}));
    const SimulationRun run = runModel(document);
    QCOMPARE(run.status(), SimulationRunStatus::Failed);
    QCOMPARE(run.times(), (QVector<double> {0.0}));
    QCOMPARE(*run.series(stock), (QVector<double> {1.0}));
    QCOMPARE(run.endTime(), std::optional<double>(0.5));
    QVERIFY(!run.issues().isEmpty());
    QCOMPARE(run.issues().front().code, QStringLiteral("non-finite-simulation-value"));
}

void TestSimulation::compilationDiagnostics()
{
    Document missing;
    missing.createVariable(ElementKind::Stock);
    QVERIFY(hasIssue(compileSimulation(missing), "missing-simulation-equation"));

    Document unsupported;
    const EntityId integral = unsupported.createVariable(ElementKind::Stock);
    unsupported.setInitialEquation(integral, "INTEG(1, 0)");
    QVERIFY(hasIssue(compileSimulation(unsupported), "unsupported-simulation-function"));

    Document data;
    const EntityId dataElement = data.createVariable(ElementKind::Variable);
    data.setEquation(dataElement, "1");
    QVERIFY(compileSimulation(data).plan);

    Document array;
    array.defineDimension("N", 2);
    const EntityId arrayElement = array.createVariable(ElementKind::Variable);
    array.setDimensions(arrayElement, {"N"});
    array.setElementEquation(arrayElement, {0}, "1");
    array.setElementEquation(arrayElement, {1}, "2");
    QVERIFY(hasIssue(compileSimulation(array), "unsupported-simulation-array"));

    Document initialCycle;
    const EntityId firstStock = initialCycle.createVariable(ElementKind::Stock);
    const EntityId secondStock = initialCycle.createVariable(ElementKind::Stock);
    initialCycle.rename(firstStock, "First");
    initialCycle.rename(secondStock, "Second");
    initialCycle.setInitialEquation(firstStock, "Second");
    initialCycle.setInitialEquation(secondStock, "First");
    QVERIFY(hasIssue(compileSimulation(initialCycle), "simultaneous-initial-equations"));

    Document runtimeCycle;
    const EntityId first = runtimeCycle.createVariable(ElementKind::Variable);
    const EntityId second = runtimeCycle.createVariable(ElementKind::Variable);
    runtimeCycle.rename(first, "First");
    runtimeCycle.rename(second, "Second");
    runtimeCycle.setEquation(first, "Second");
    runtimeCycle.setEquation(second, "First");
    QVERIFY(hasIssue(compileSimulation(runtimeCycle), "simultaneous-equations"));

}

void TestSimulation::cancellationAndRuntimeFailure()
{
    const Document document = exponentialDocument(1.0);
    int checks = 0;
    const SimulationRun cancelled = runModel(document, [&checks]() { return ++checks >= 3; });
    QCOMPARE(cancelled.status(), SimulationRunStatus::Cancelled);
    QVERIFY(!cancelled.times().isEmpty());
    QVERIFY(cancelled.times().size() < 2);
    QVERIFY(hasIssue(SimulationCompileResult {{}, cancelled.issues()}, "simulation-cancelled"));

    Document failure;
    const EntityId stock = failure.createVariable(ElementKind::Stock);
    const EntityId cloud = failure.createCloud();
    const EntityId flow = failure.createFlow(cloud, stock);
    failure.rename(stock, "Stock");
    failure.setInitialEquation(stock, "1");
    failure.setEquation(flow, "1 / (Stock - 1)");
    const SimulationRun failed = runModel(failure);
    QCOMPARE(failed.status(), SimulationRunStatus::Failed);
    QVERIFY(failed.times().isEmpty());
    QVERIFY(!failed.issues().isEmpty());
    QCOMPARE(failed.issues().front().code, QStringLiteral("non-finite-simulation-value"));
}

} // namespace

QTEST_APPLESS_MAIN(TestSimulation)
#include "TestSimulation.moc"
