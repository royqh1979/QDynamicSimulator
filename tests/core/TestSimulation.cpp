#include <core/simulation/Simulation.h>

#include <QtTest>

#include <algorithm>
#include <cmath>
#include <functional>
#include <numeric>

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
    document.rename(stock, "stock");
    document.setInitialEquation(stock, "1");
    document.setEquation(flow, "stock");
    document.setSimulationSettings({0.0, 1.0, dt, dt});
    return document;
}

Document randomDocument(int samples = 10000)
{
    Document document;
    const auto add = [&document](const QString &name, const QString &equation) {
        const EntityId id = document.createVariable(ElementKind::Variable);
        document.rename(id, name);
        document.setEquation(id, equation);
    };
    add("uniform", "RANDOM(0, 10)");
    add("normal_output", "NORMAL(2, 1)");
    add("lognormal_output", "LOGNORMAL(10, 1)");
    add("poisson_output", "POISSON(3)");
    add("exponential_output", "EXPRND(1)");
    document.setSimulationSettings({0.0, samples - 1.0, 1.0, 1.0});
    return document;
}

std::pair<double, double> meanVariance(const QVector<double> &values)
{
    Q_ASSERT(!values.isEmpty());
    const double mean = std::accumulate(values.begin(), values.end(), 0.0) / values.size();
    double variance = 0.0;
    for (const double value : values)
        variance += (value - mean) * (value - mean);
    return {mean, variance / values.size()};
}

double ksStatistic(QVector<double> samples, const std::function<double(double)> &cdf, bool discrete = false)
{
    std::sort(samples.begin(), samples.end());
    double statistic = 0.0;
    for (int first = 0; first < samples.size();) {
        int last = first + 1;
        while (last < samples.size() && samples[last] == samples[first])
            ++last;
        const double current = std::clamp(cdf(samples[first]), 0.0, 1.0);
        const double previous = first == 0 ? 0.0 : std::clamp(cdf(discrete ? samples[first - 1] : samples[first]), 0.0, 1.0);
        const double upper = static_cast<double>(last) / samples.size();
        const double lower = static_cast<double>(first) / samples.size();
        statistic = std::max({statistic, std::abs(upper - current), std::abs(previous - lower)});
        first = last;
    }
    return statistic;
}

double normalCdf(double value, double mean, double deviation)
{
    return 0.5 * (1.0 + std::erf((value - mean) / (deviation * std::sqrt(2.0))));
}

double poissonCdf(double value, double mean)
{
    const int limit = static_cast<int>(std::floor(value));
    double probability = std::exp(-mean);
    double cumulative = probability;
    for (int index = 0; index < limit; ++index) {
        probability *= mean / (index + 1);
        cumulative += probability;
    }
    return cumulative;
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
    void arrayStockEulerAndResults();
    void arrayElementEquationsAndBroadcast();
    void arrayRungeKutta4AndConservation();
    void arrayDimensionReordering();
    void arrayDimensionFailures();
    void rungeKutta4();
    void rungeKutta4StageFailure();
    void unifiedRunMetadata();
    void seriesValueSemantics();
    void referenceBindingSemantics();
    void boundAstOwnershipAndMacroRecursion();
    void lookupDefinitions();
    void lookupSimulation();
    void timeFunctionBoundaries();
    void randomFunctionsReproducibility();
    void randomFunctionDistributionsAndErrors();
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
    document.rename(stock, "stock");
    document.rename(auxiliary, "twice");
    document.rename(offset, "offset");
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
    const auto stockSeries = run.series("stock");
    const auto twiceSeries = run.series("twice");
    const auto offsetSeries = run.series("offset");
    QVERIFY(!stockSeries.isEmpty() && !twiceSeries.isEmpty() && !offsetSeries.isEmpty());
    QCOMPARE(stockSeries, QVector<double> {5.0});
    QCOMPARE(twiceSeries, QVector<double> {10.0});
    QCOMPARE(offsetSeries, QVector<double> {3.0});
    QVERIFY(run.series("missing").isEmpty());
}

void TestSimulation::stockFlowsAndConservation()
{
    Document document;
    const EntityId source = document.createVariable(ElementKind::Stock);
    const EntityId target = document.createVariable(ElementKind::Stock);
    const EntityId flow = document.createFlow(source, target);
    document.rename(source, "source");
    document.rename(target, "target");
    document.setInitialEquation(source, "10");
    document.setInitialEquation(target, "0");
    document.setEquation(flow, "2");
    document.setSimulationSettings({0.0, 2.0, 1.0, 1.0});

    const SimulationRun run = runModel(document);
    QCOMPARE(run.status(), SimulationRunStatus::Completed);
    QCOMPARE(run.times(), (QVector<double> {0.0, 1.0, 2.0}));
    QCOMPARE(run.series("source"), (QVector<double> {10.0, 8.0, 6.0}));
    QCOMPARE(run.series("target"), (QVector<double> {0.0, 2.0, 4.0}));
    QCOMPARE(run.series("flow1"), (QVector<double> {2.0, 2.0, 2.0}));
    for (int index = 0; index < run.times().size(); ++index)
        QCOMPARE(run.series("source").at(index) + run.series("target").at(index), 10.0);
}

void TestSimulation::exponentialEulerAndConvergence()
{
    const Document coarseDocument = exponentialDocument(0.1);
    const SimulationRun coarse = runModel(coarseDocument);
    QCOMPARE(coarse.status(), SimulationRunStatus::Completed);
    QCOMPARE(coarse.times().size(), 11);
    const auto stocks = coarseDocument.model().findByName("stock");
    QVERIFY(stocks);
    const double expected = std::pow(1.1, 10);
    expectNear(coarse.series("stock").back(), expected, 1e-12);

    const Document fineDocument = exponentialDocument(0.05);
    const SimulationRun fine = runModel(fineDocument);
    QCOMPARE(fine.times().size(), 21);
    const double analytic = std::exp(1.0);
    const double coarseError = std::abs(coarse.series("stock").back() - analytic);
    const double fineError = std::abs(fine.series("stock").back() - analytic);
    QVERIFY(coarseError > fineError * 1.8);
    QVERIFY(coarseError < fineError * 2.2);
}

void TestSimulation::logisticAndOscillatorBenchmarks()
{
    Document logistic;
    const EntityId logisticStock = logistic.createVariable(ElementKind::Stock);
    const EntityId logisticCloud = logistic.createCloud();
    const EntityId logisticFlow = logistic.createFlow(logisticCloud, logisticStock);
    logistic.rename(logisticStock, "stock");
    logistic.setInitialEquation(logisticStock, "1");
    logistic.setEquation(logisticFlow, "Stock * (1 - Stock / 10)");
    logistic.setSimulationSettings({0.0, 1.0, 0.01, 0.01});
    const SimulationRun logisticRun = runModel(logistic);
    QCOMPARE(logisticRun.status(), SimulationRunStatus::Completed);
    const double logisticExpected = 10.0 / (1.0 + 9.0 * std::exp(-1.0));
    expectNear(logisticRun.series("stock").back(), logisticExpected, 0.01);

    Document oscillator;
    const EntityId position = oscillator.createVariable(ElementKind::Stock);
    const EntityId velocity = oscillator.createVariable(ElementKind::Stock);
    const EntityId cloud = oscillator.createCloud();
    const EntityId positionFlow = oscillator.createFlow(cloud, position);
    const EntityId velocityFlow = oscillator.createFlow(cloud, velocity);
    oscillator.rename(position, "position");
    oscillator.rename(velocity, "velocity");
    oscillator.setInitialEquation(position, "1");
    oscillator.setInitialEquation(velocity, "0");
    oscillator.setEquation(positionFlow, "velocity");
    oscillator.setEquation(velocityFlow, "-Position - 0.2 * Velocity");
    oscillator.setSimulationSettings({0.0, 1.0, 0.01, 0.01});
    const SimulationRun oscillatorRun = runModel(oscillator);
    QCOMPARE(oscillatorRun.status(), SimulationRunStatus::Completed);
    const double omega = std::sqrt(0.99);
    const double positionExpected = std::exp(-0.1) * (std::cos(omega) + 0.1 / omega * std::sin(omega));
    const double velocityExpected = std::exp(-0.1)
        * (-0.1 * (std::cos(omega) + 0.1 / omega * std::sin(omega))
            - omega * std::sin(omega) + 0.1 * std::cos(omega));
    expectNear(oscillatorRun.series("position").back(), positionExpected, 0.006);
    expectNear(oscillatorRun.series("velocity").back(), velocityExpected, 0.006);
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
    QVERIFY(!run.series("stock1").isEmpty());
    QCOMPARE(run.series("stock1"), (QVector<double> {0.0, 0.6, 1.0}));

    QVERIFY(document.setSimulationSettings({2.0, 2.0, 0.3, 0.6}));
    run = runModel(document);
    QCOMPARE(run.status(), SimulationRunStatus::Completed);
    QCOMPARE(run.times(), QVector<double> {2.0});
    QVERIFY(!run.series("stock1").isEmpty());
    QCOMPARE(run.series("stock1"), QVector<double> {0.0});
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
    document.rename(stock, "stock");
    document.setInitialEquation(stock, "1");
    document.setEquation(flow, "Scale(Stock)");
    document.setSimulationSettings({0.0, 1.0, 1.0, 1.0});
    const SimulationRun run = runModel(document);
    QCOMPARE(run.status(), SimulationRunStatus::Completed);
    QCOMPARE(run.series("flow1"), (QVector<double> {2.0, 6.0}));
    QCOMPARE(run.series("stock"), (QVector<double> {1.0, 3.0}));
}

void TestSimulation::arrayStockEulerAndResults()
{
    Document document;
    QVERIFY(document.defineDimension("N", 2));
    const EntityId stock = document.createVariable(ElementKind::Stock);
    const EntityId cloud = document.createCloud();
    const EntityId flow = document.createFlow(cloud, stock);
    document.rename(stock, "stock");
    document.rename(flow, "rate");
    document.setDimensions(stock, {"N"});
    document.setDimensions(flow, {"N"});
    document.setInitialEquation(stock, "1");
    document.setEquation(flow, "Stock * N");
    document.setSimulationSettings({0.0, 1.0, 1.0, 1.0});

    const SimulationRun run = runModel(document);
    QCOMPARE(run.status(), SimulationRunStatus::Completed);
    QVERIFY(run.series("stock").isEmpty());
    const QVector<ArrayValue> stocks = run.arraySeries("stock");
    const QVector<ArrayValue> rates = run.arraySeries("rate");
    QVERIFY(!stocks.isEmpty() && !rates.isEmpty());
    QCOMPARE(stocks.size(), 2);
    QCOMPARE(rates.size(), 2);
    QCOMPARE(stocks.at(0).dimensions, QVector<QString> {"n"});
    QCOMPARE(stocks.at(0).shape, QVector<std::size_t> {2});
    QCOMPARE(stocks.at(0).values, (QVector<double> {1.0, 1.0}));
    QCOMPARE(stocks.at(1).values, (QVector<double> {2.0, 3.0}));
    QCOMPARE(rates.at(0).values, (QVector<double> {1.0, 2.0}));
    QCOMPARE(rates.at(1).values, (QVector<double> {2.0, 6.0}));
    QVERIFY(run.arraySeries("missing").isEmpty());
}

void TestSimulation::arrayElementEquationsAndBroadcast()
{
    Document document;
    QVERIFY(document.defineDimension("N", 2));
    const EntityId rate = document.createVariable(ElementKind::Variable);
    document.rename(rate, "rate");
    document.setDimensions(rate, {"N"});
    document.setElementEquation(rate, {0}, "0.01");
    document.setElementEquation(rate, {1}, "0.2");
    const EntityId stock = document.createVariable(ElementKind::Stock);
    document.rename(stock, "stock");
    document.setDimensions(stock, {"N"});
    document.setElementEquation(stock, {0}, "1");
    document.setElementEquation(stock, {1}, "2");
    const EntityId cloud = document.createCloud();
    const EntityId flow = document.createFlow(cloud, stock);
    document.setDimensions(flow, {"N"});
    document.setEquation(flow, "Rate[N] + 1");
    document.setSimulationSettings({0.0, 1.0, 1.0, 1.0});

    const SimulationRun run = runModel(document);
    QCOMPARE(run.status(), SimulationRunStatus::Completed);
    const QVector<ArrayValue> stocks = run.arraySeries("stock");
    const QVector<ArrayValue> flows = run.arraySeries("flow1");
    QVERIFY(!stocks.isEmpty() && !flows.isEmpty());
    QCOMPARE(stocks.at(0).values, (QVector<double> {1.0, 2.0}));
    QCOMPARE(stocks.at(1).values, (QVector<double> {2.01, 3.2}));
    QCOMPARE(flows.at(0).values, (QVector<double> {1.01, 1.2}));
    QCOMPARE(flows.at(1).values, (QVector<double> {1.01, 1.2}));
}

void TestSimulation::arrayRungeKutta4AndConservation()
{
    Document document;
    QVERIFY(document.defineDimension("N", 2));
    const EntityId source = document.createVariable(ElementKind::Stock);
    const EntityId target = document.createVariable(ElementKind::Stock);
    const EntityId flow = document.createFlow(source, target);
    document.rename(source, "source");
    document.rename(target, "target");
    document.setDimensions(source, {"N"});
    document.setDimensions(target, {"N"});
    document.setDimensions(flow, {"N"});
    document.setElementEquation(source, {0}, "10");
    document.setElementEquation(source, {1}, "20");
    document.setInitialEquation(target, "0");
    document.setEquation(flow, "2");
    document.setSimulationSettings({0.0, 1.0, 1.0, 1.0, IntegrationMethod::RungeKutta4});

    const SimulationRun run = runModel(document);
    QCOMPARE(run.status(), SimulationRunStatus::Completed);
    const QVector<ArrayValue> sources = run.arraySeries("source");
    const QVector<ArrayValue> targets = run.arraySeries("target");
    QVERIFY(!sources.isEmpty() && !targets.isEmpty());
    QCOMPARE(sources.at(0).values, (QVector<double> {10.0, 20.0}));
    QCOMPARE(sources.at(1).values, (QVector<double> {8.0, 18.0}));
    QCOMPARE(targets.at(0).values, (QVector<double> {0.0, 0.0}));
    QCOMPARE(targets.at(1).values, (QVector<double> {2.0, 2.0}));
    for (int index = 0; index < sources.size(); ++index) {
        QCOMPARE(sources.at(index).values[0] + targets.at(index).values[0], 10.0);
        QCOMPARE(sources.at(index).values[1] + targets.at(index).values[1], 20.0);
    }
}

void TestSimulation::arrayDimensionReordering()
{
    Document document;
    QVERIFY(document.defineDimension("A", 2));
    QVERIFY(document.defineDimension("B", 2));
    const EntityId matrix = document.createVariable(ElementKind::Variable);
    document.rename(matrix, "Matrix");
    document.setDimensions(matrix, {"A", "B"});
    document.setElementEquation(matrix, {0, 0}, "1");
    document.setElementEquation(matrix, {0, 1}, "2");
    document.setElementEquation(matrix, {1, 0}, "3");
    document.setElementEquation(matrix, {1, 1}, "4");
    const EntityId stock = document.createVariable(ElementKind::Stock);
    document.setDimensions(stock, {"B", "A"});
    document.setInitialEquation(stock, "0");
    const EntityId cloud = document.createCloud();
    const EntityId flow = document.createFlow(cloud, stock);
    document.setDimensions(flow, {"A", "B"});
    document.setEquation(flow, "Matrix");
    document.setSimulationSettings({0.0, 1.0, 1.0, 1.0});

    const SimulationRun run = runModel(document);
    QCOMPARE(run.status(), SimulationRunStatus::Completed);
    const QVector<ArrayValue> stocks = run.arraySeries("stock1");
    const QVector<ArrayValue> flows = run.arraySeries("flow1");
    QVERIFY(!stocks.isEmpty() && !flows.isEmpty());
    QCOMPARE(stocks.at(0).dimensions, (QVector<QString> {"b", "a"}));
    QCOMPARE(stocks.at(0).shape, (QVector<std::size_t> {2, 2}));
    QCOMPARE(stocks.at(0).values, (QVector<double> {0.0, 0.0, 0.0, 0.0}));
    QCOMPARE(stocks.at(1).values, (QVector<double> {1.0, 3.0, 2.0, 4.0}));
    QCOMPARE(flows.at(0).dimensions, (QVector<QString> {"a", "b"}));
    QCOMPARE(flows.at(0).values, (QVector<double> {1.0, 2.0, 3.0, 4.0}));
}

void TestSimulation::arrayDimensionFailures()
{
    Document document;
    QVERIFY(document.defineDimension("N", 2));
    QVERIFY(document.defineDimension("M", 3));
    const EntityId stock = document.createVariable(ElementKind::Stock);
    document.setDimensions(stock, {"N"});
    document.setInitialEquation(stock, "1");
    const EntityId cloud = document.createCloud();
    const EntityId flow = document.createFlow(cloud, stock);
    document.setDimensions(flow, {"M"});
    document.setEquation(flow, "1");
    document.setSimulationSettings({0.0, 1.0, 1.0, 1.0});

    const SimulationRun run = runModel(document);
    QCOMPARE(run.status(), SimulationRunStatus::Failed);
    QVERIFY(!run.issues().isEmpty());
    QCOMPARE(run.issues().front().code, QStringLiteral("incompatible-stock-flow"));
    const QVector<ArrayValue> stocks = run.arraySeries("stock1");
    QVERIFY(!stocks.isEmpty());
    QCOMPARE(stocks.size(), 1);
    QCOMPARE(stocks.front().values, (QVector<double> {1.0, 1.0}));

    Document scalarStock;
    scalarStock.defineDimension("N", 2);
    const EntityId scalar = scalarStock.createVariable(ElementKind::Stock);
    scalarStock.setInitialEquation(scalar, "1");
    const EntityId scalarCloud = scalarStock.createCloud();
    const EntityId arrayFlow = scalarStock.createFlow(scalarCloud, scalar);
    scalarStock.setDimensions(arrayFlow, {"N"});
    scalarStock.setEquation(arrayFlow, "1");
    scalarStock.setSimulationSettings({0.0, 1.0, 1.0, 1.0});
    const SimulationRun incompatible = runModel(scalarStock);
    QCOMPARE(incompatible.status(), SimulationRunStatus::Failed);
    QVERIFY(!incompatible.issues().isEmpty());
    QCOMPARE(incompatible.issues().front().code, QStringLiteral("incompatible-stock-flow"));
}

void TestSimulation::rungeKutta4()
{
    Document coarseDocument = exponentialDocument(0.25);
    QVERIFY(coarseDocument.setSimulationSettings({0.0, 1.0, 0.25, 0.25, IntegrationMethod::RungeKutta4}));
    const SimulationRun coarse = runModel(coarseDocument);
    QCOMPARE(coarse.status(), SimulationRunStatus::Completed);
    QCOMPARE(coarse.settings().integrationMethod(), IntegrationMethod::RungeKutta4);
    QCOMPARE(coarse.times().size(), 5);
    const EntityId stock = coarseDocument.findByName("stock")->id();
    const double analytic = std::exp(1.0);
    expectNear(coarse.series("stock").back(), analytic, 0.001);

    Document fineDocument = exponentialDocument(0.125);
    QVERIFY(fineDocument.setSimulationSettings({0.0, 1.0, 0.125, 0.125, IntegrationMethod::RungeKutta4}));
    const SimulationRun fine = runModel(fineDocument);
    QCOMPARE(fine.times().size(), 9);
    const double coarseError = std::abs(coarse.series("stock").back() - analytic);
    const double fineError = std::abs(fine.series("stock").back() - analytic);
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
    QCOMPARE(timeRun.series("stock1"), (QVector<double> {0.0, 0.5}));
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
    QCOMPARE(run.series("stock1"), (QVector<double> {1.0}));
    QCOMPARE(run.endTime(), std::optional<double>(0.5));
    QVERIFY(!run.issues().isEmpty());
    QCOMPARE(run.issues().front().code, QStringLiteral("non-finite-simulation-value"));
}

void TestSimulation::unifiedRunMetadata()
{
    const Document document = exponentialDocument(1.0);
    const SimulationRun numerical = runModel(document);
    const EntityId stock = document.findByName("stock")->id();
    const EntityId flow = document.findByName("flow1")->id();
    SimulationRunMetadata metadata;
    metadata.id = 42;
    metadata.name = "experiment";
    metadata.modelRevision = 7;
    metadata.variables = {RunVariableInfo(stock, "stock", "stock", "unit", ElementKind::Stock, false),
        RunVariableInfo(flow, "Flow1", "flow1", "unit", ElementKind::Flow, false)};
    metadata.causes.insert(stock, {flow});
    const SimulationRun identified = numerical.withMetadata(std::move(metadata));

    QCOMPARE(identified.id(), RunId(42));
    QCOMPARE(identified.name(), QString("experiment"));
    QCOMPARE(identified.modelRevision(), quint64(7));
    QCOMPARE(identified.variables().size(), 2);
    QCOMPARE(identified.variables().front().nameKey(), QString("stock"));
    QCOMPARE(identified.causes("stock"), QVector<QString> {"flow1"});
    QCOMPARE(identified.status(), numerical.status());
    QCOMPARE(identified.times(), numerical.times());
    QCOMPARE(identified.series("stock"), numerical.series("stock"));
    QCOMPARE(numerical.id(), RunId(0));
    QVERIFY(numerical.name().isEmpty());
    QCOMPARE(numerical.modelRevision(), quint64(0));
    QVERIFY(!numerical.variables().isEmpty());
    QVERIFY(numerical.causes("stock").isEmpty());
}

void TestSimulation::seriesValueSemantics()
{
    QVector<double> scalar;
    {
        const Document document = exponentialDocument(0.5);
        const SimulationRun run = runModel(document);
        scalar = run.series("stock");
        QCOMPARE(scalar.size(), 3);
        QVector<double> detached = scalar;
        detached[0] = 99.0;
        QCOMPARE(run.series("stock").front(), 1.0);
        QCOMPARE(scalar.front(), 1.0);
        QVERIFY(run.series("missing").isEmpty());
    }
    QCOMPARE(scalar.size(), 3);
    QCOMPARE(scalar.front(), 1.0);

    QVector<ArrayValue> arrays;
    {
        Document document;
        document.defineDimension("N", 2);
        const EntityId id = document.createVariable(ElementKind::Variable);
        document.rename(id, "stock");
        document.setDimensions(id, {"N"});
        document.setEquation(id, "1");
        document.setSimulationSettings({0.0, 0.0, 1.0, 1.0});
        const SimulationRun run = runModel(document);
        arrays = run.arraySeries("stock");
        QCOMPARE(arrays.size(), 1);
        arrays[0].values[0] = 9.0;
        QCOMPARE(run.arraySeries("stock").front().values.front(), 1.0);
    }
    QCOMPARE(arrays.size(), 1);
    QCOMPARE(arrays.front().values, (QVector<double> {9.0, 1.0}));
}

void TestSimulation::referenceBindingSemantics()
{
    Document special;
    const EntityId stock = special.createVariable(ElementKind::Stock);
    const EntityId cloud = special.createCloud();
    const EntityId flow = special.createFlow(cloud, stock);
    const EntityId namedTime = special.createVariable(ElementKind::Variable);
    QVERIFY(!special.rename(namedTime, "time"));
    special.setEquation(namedTime, "100");
    const EntityId observed = special.createVariable(ElementKind::Variable);
    special.rename(observed, "observed");
    special.setEquation(observed, "time");
    special.setInitialEquation(stock, "0");
    special.setEquation(flow, "1");
    special.setSimulationSettings({0.0, 1.0, 1.0, 1.0});
    const SimulationRun specialRun = runModel(special);
    QCOMPARE(specialRun.status(), SimulationRunStatus::Completed);
    QCOMPARE(specialRun.series("observed"), (QVector<double> {0.0, 1.0}));

    Document dimensions;
    dimensions.defineDimension("Kind", {"First", "Second"});
    const EntityId selected = dimensions.createVariable(ElementKind::Variable);
    dimensions.rename(selected, "selected");
    dimensions.setEquation(selected, "Kind.Second");
    const EntityId current = dimensions.createVariable(ElementKind::Variable);
    dimensions.rename(current, "current");
    dimensions.setDimensions(current, {"Kind"});
    dimensions.setEquation(current, "Kind");
    dimensions.setSimulationSettings({0.0, 0.0, 1.0, 1.0});
    const SimulationRun dimensionRun = runModel(dimensions);
    QCOMPARE(dimensionRun.status(), SimulationRunStatus::Completed);
    QCOMPARE(dimensionRun.series("selected"), (QVector<double> {2.0}));
    const QVector<ArrayValue> currentValues = dimensionRun.arraySeries("current");
    QCOMPARE(currentValues.size(), 1);
    QCOMPARE(currentValues.front().values, (QVector<double> {1.0, 2.0}));
}

void TestSimulation::boundAstOwnershipAndMacroRecursion()
{
    Document document;
    MacroDefinitionData fact;
    fact.displayName = "Fact";
    fact.parameters = {{"x", {}}};
    fact.equation = "IF x <= 1 THEN 1 ELSE x * Fact(x - 1)";
    QVERIFY(document.defineMacro(fact));
    const EntityId output = document.createVariable(ElementKind::Variable);
    QVERIFY(document.rename(output, "output"));
    QVERIFY(document.setEquation(output, "Fact(5)"));
    QVERIFY(document.setSimulationSettings({0.0, 0.0, 1.0, 1.0}));

    const ModelElement *model = static_cast<const ModelElement *>(document.find(output));
    const FormulaNode *sourceRoot = model->formula().root();
    const std::size_t sourceNodes = formulaNodeCount(sourceRoot);
    const MacroDefinition *sourceMacro = document.findMacro("Fact", 1);
    const FormulaNode *sourceMacroRoot = sourceMacro->formula().root();
    const std::size_t sourceMacroNodes = formulaNodeCount(sourceMacroRoot);
    SimulationCompileResult compiled = compileSimulation(document);
    QVERIFY2(compiled.plan, "Compilation failed");
    QVERIFY(model->formula().root() == sourceRoot);
    QCOMPARE(formulaNodeCount(sourceRoot), sourceNodes);
    QCOMPARE(model->equation(), QString("Fact(5)"));
    QVERIFY(sourceMacro->formula().root() == sourceMacroRoot);
    QCOMPARE(formulaNodeCount(sourceMacroRoot), sourceMacroNodes);
    QCOMPARE(sourceMacro->equation(), fact.equation);

    SimulationPlan moved = std::move(*compiled.plan);
    const SimulationRun run = SimulationRunner(document, moved).run();
    QCOMPARE(run.status(), SimulationRunStatus::Completed);
    QCOMPARE(run.series("output"), QVector<double> {120.0});
    QCOMPARE(model->formula().root(), sourceRoot);
    QCOMPARE(sourceMacro->formula().root(), sourceMacroRoot);
}

void TestSimulation::lookupDefinitions()
{
    const auto continuous = LookupDefinition::create({0.0, 1.0, 2.0}, {0.0, 2.0, 4.0}, LookupKind::Continuous);
    QVERIFY(continuous);
    expectNear(continuous->evaluate(-1.0), 0.0, 1e-12);
    expectNear(continuous->evaluate(0.5), 1.0, 1e-12);
    expectNear(continuous->evaluate(2.5), 4.0, 1e-12);

    const auto extrapolate = LookupDefinition::create({0.0, 1.0, 2.0}, {0.0, 2.0, 4.0}, LookupKind::Extrapolate);
    QVERIFY(extrapolate);
    expectNear(extrapolate->evaluate(-1.0), -2.0, 1e-12);
    expectNear(extrapolate->evaluate(2.5), 5.0, 1e-12);

    const auto discrete = LookupDefinition::create({0.0, 1.0, 2.0}, {0.0, 2.0, 2.0}, LookupKind::Discrete);
    QVERIFY(discrete);
    QCOMPARE(discrete->evaluate(0.5), 0.0);
    QCOMPARE(discrete->evaluate(1.5), 2.0);
    QCOMPARE(discrete->evaluate(-1.0), 0.0);
    QCOMPARE(discrete->evaluate(3.0), 2.0);

    QVERIFY(!LookupDefinition::create({}, {}, LookupKind::Continuous));
    QVERIFY(!LookupDefinition::create({0.0}, {1.0, 2.0}, LookupKind::Continuous));
    QVERIFY(!LookupDefinition::create({0.0, 0.0}, {1.0, 2.0}, LookupKind::Continuous));
    QVERIFY(!LookupDefinition::create({0.0, 1.0}, {1.0, 2.0}, LookupKind::Discrete));
    QVERIFY(!LookupDefinition::create({0.0, 1.0}, {1.0, std::numeric_limits<double>::infinity()}, LookupKind::Continuous));
    QVERIFY(std::isnan(continuous->evaluate(std::numeric_limits<double>::quiet_NaN())));
}

void TestSimulation::lookupSimulation()
{
    Document document;
    const EntityId stock = document.createVariable(ElementKind::Stock);
    const EntityId cloud = document.createCloud();
    const EntityId flow = document.createFlow(cloud, stock);
    document.setInitialEquation(stock, "0");
    const EntityId lookup = document.createLookup();
    QVERIFY(document.rename(lookup, "Rate Table"));
    QVERIFY(document.setLookup(lookup, LookupDefinition::create({0.0, 2.0}, {0.0, 4.0}, LookupKind::Continuous)));
    document.setEquation(flow, "\"Rate Table\"(TIME)");
    document.setSimulationSettings({0.0, 2.0, 0.5, 0.5});
    const SimulationRun run = runModel(document);
    QCOMPARE(run.status(), SimulationRunStatus::Completed);
    QCOMPARE(run.series("flow1"), (QVector<double> {0.0, 1.0, 2.0, 3.0, 4.0}));
    for (const RunVariableInfo &variable : run.variables())
        QVERIFY(variable.kind() != ElementKind::Lookup);

    Document inlineDocument;
    const EntityId input = inlineDocument.createVariable(ElementKind::Variable);
    inlineDocument.rename(input, "input");
    inlineDocument.setEquation(input, "TIME");
    const EntityId output = inlineDocument.createVariable(ElementKind::Variable);
    inlineDocument.rename(output, "output");
    inlineDocument.setEquation(output, "input");
    QVERIFY(inlineDocument.setLookup(output,
        LookupDefinition::create({0.0, 2.0}, {0.0, 4.0}, LookupKind::Continuous)));
    inlineDocument.setSimulationSettings({0.0, 2.0, 1.0, 1.0});
    const SimulationRun inlineRun = runModel(inlineDocument);
    QCOMPARE(inlineRun.status(), SimulationRunStatus::Completed);
    QCOMPARE(inlineRun.series("output"), (QVector<double> {0.0, 2.0, 4.0}));

    Document arrayDocument;
    arrayDocument.defineDimension("N", 2);
    const EntityId arrayInput = arrayDocument.createVariable(ElementKind::Variable);
    arrayDocument.rename(arrayInput, "input");
    arrayDocument.setDimensions(arrayInput, {"N"});
    arrayDocument.setEquation(arrayInput, "1");
    const EntityId arrayLookup = arrayDocument.createLookup();
    arrayDocument.rename(arrayLookup, "Rate Table");
    arrayDocument.setLookup(arrayLookup,
        LookupDefinition::create({0.0, 1.0}, {0.0, 2.0}, LookupKind::Continuous));
    const EntityId called = arrayDocument.createVariable(ElementKind::Variable);
    arrayDocument.rename(called, "called");
    arrayDocument.setDimensions(called, {"N"});
    arrayDocument.setEquation(called, "\"Rate Table\"(input)");
    const EntityId inlined = arrayDocument.createVariable(ElementKind::Variable);
    arrayDocument.rename(inlined, "inlined");
    arrayDocument.setDimensions(inlined, {"N"});
    arrayDocument.setEquation(inlined, "input");
    arrayDocument.setLookup(inlined,
        LookupDefinition::create({0.0, 1.0}, {0.0, 3.0}, LookupKind::Continuous));
    arrayDocument.setSimulationSettings({0.0, 0.0, 1.0, 1.0});
    const SimulationRun arrayRun = runModel(arrayDocument);
    QCOMPARE(arrayRun.status(), SimulationRunStatus::Completed);
    QCOMPARE(arrayRun.arraySeries("called").front().values, (QVector<double> {2.0, 2.0}));
    QCOMPARE(arrayRun.arraySeries("inlined").front().values, (QVector<double> {3.0, 3.0}));
}

void TestSimulation::timeFunctionBoundaries()
{
    Document document;
    const auto addVariable = [&document](const QString &name, const QString &equation) {
        const EntityId id = document.createVariable(ElementKind::Variable);
        QVERIFY(document.rename(id, name));
        document.setEquation(id, equation);
    };
    addVariable("step_output", "STEP(2, 1)");
    addVariable("pulse_output", "PULSE(1, 1)");
    addVariable("zero_width_output", "PULSE(1, 0)");
    addVariable("train_output", "pulse_train(1, 1, 2, 3)");
    addVariable("single_output", "pulse_train(1, 1, 0, 4)");
    addVariable("ramp_output", "RAMP(2, 1, 3)");
    document.setSimulationSettings({0.0, 4.0, 0.5, 0.5});
    const SimulationRun run = runModel(document);
    QCOMPARE(run.status(), SimulationRunStatus::Completed);
    QCOMPARE(run.series("step_output"), (QVector<double> {0.0, 0.0, 2.0, 2.0, 2.0, 2.0, 2.0, 2.0, 2.0}));
    QCOMPARE(run.series("pulse_output"), (QVector<double> {0.0, 0.0, 1.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0}));
    QCOMPARE(run.series("zero_width_output"), (QVector<double> {0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0}));
    QCOMPARE(run.series("train_output"), (QVector<double> {0.0, 0.0, 1.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0}));
    QCOMPARE(run.series("single_output"), (QVector<double> {0.0, 0.0, 1.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0}));
    QCOMPARE(run.series("ramp_output"), (QVector<double> {0.0, 0.0, 0.0, 1.0, 2.0, 3.0, 4.0, 4.0, 4.0}));

    document.setSimulationSettings({0.0, 4.0, 0.5, 0.5, IntegrationMethod::RungeKutta4});
    const SimulationRun rk4 = runModel(document);
    QCOMPARE(rk4.status(), SimulationRunStatus::Completed);
    for (const QString &name : {QStringLiteral("step_output"), QStringLiteral("pulse_output"), QStringLiteral("zero_width_output"),
             QStringLiteral("train_output"), QStringLiteral("single_output"), QStringLiteral("ramp_output")})
        QCOMPARE(rk4.series(name), run.series(name));

    Document array;
    array.defineDimension("N", 2);
    const EntityId height = array.createVariable(ElementKind::Variable);
    array.rename(height, "height");
    array.setDimensions(height, {"N"});
    array.setEquation(height, "2,3");
    const EntityId start = array.createVariable(ElementKind::Variable);
    array.rename(start, "start");
    array.setDimensions(start, {"N"});
    array.setEquation(start, "0,1");
    const EntityId output = array.createVariable(ElementKind::Variable);
    array.rename(output, "output");
    array.setDimensions(output, {"N"});
    array.setEquation(output, "STEP(height, start)");
    array.setSimulationSettings({0.0, 1.0, 1.0, 1.0});
    const SimulationRun arrayRun = runModel(array);
    QCOMPARE(arrayRun.status(), SimulationRunStatus::Completed);
    QCOMPARE(arrayRun.arraySeries("output").at(0).values, (QVector<double> {2.0, 0.0}));
    QCOMPARE(arrayRun.arraySeries("output").at(1).values, (QVector<double> {2.0, 3.0}));
}

void TestSimulation::randomFunctionsReproducibility()
{
    Document document = randomDocument(100);
    const SimulationRun first = runModel(document);
    const SimulationRun second = runModel(document);
    for (const QString &name : {QStringLiteral("uniform"), QStringLiteral("normal_output"), QStringLiteral("lognormal_output"),
             QStringLiteral("poisson_output"), QStringLiteral("exponential_output")}) {
        QCOMPARE(first.status(), SimulationRunStatus::Completed);
        QCOMPARE(second.status(), SimulationRunStatus::Completed);
        QCOMPARE(first.series(name), second.series(name));
    }

    const EntityId noise = document.createVariable(ElementKind::Variable);
    QVERIFY(document.rename(noise, "NOISE SEED"));
    QVERIFY(document.setEquation(noise, "123456"));
    const SimulationRun changed = runModel(document);
    QVERIFY(changed.series("uniform") != first.series("uniform"));

    document.setSimulationSettings({0.0, 99.0, 1.0, 1.0, IntegrationMethod::RungeKutta4});
    const SimulationRun rk4 = runModel(document);
    QCOMPARE(rk4.status(), SimulationRunStatus::Completed);
    QVERIFY(rk4.series("uniform") != first.series("uniform"));

    Document independent;
    const auto add = [&independent](const QString &name, const QString &equation) {
        const EntityId id = independent.createVariable(ElementKind::Variable);
        independent.rename(id, name);
        independent.setEquation(id, equation);
    };
    add("first", "RANDOM(0, 1, 7)");
    add("second", "NORMAL(0, 1, 8)");
    independent.setSimulationSettings({0.0, 10.0, 1.0, 1.0});
    const SimulationRun base = runModel(independent);
    add("third", "EXPRND(1, 9)");
    const SimulationRun extended = runModel(independent);
    QCOMPARE(extended.series("first"), base.series("first"));
    QCOMPARE(extended.series("second"), base.series("second"));

    Document arrays;
    arrays.defineDimension("N", 4);
    const EntityId values = arrays.createVariable(ElementKind::Variable);
    arrays.rename(values, "values");
    arrays.setDimensions(values, {"N"});
    arrays.setEquation(values, "RANDOM(0, 1)");
    arrays.setSimulationSettings({0.0, 0.0, 1.0, 1.0});
    const SimulationRun arrayRun = runModel(arrays);
    QCOMPARE(arrayRun.status(), SimulationRunStatus::Completed);
    const QVector<double> samples = arrayRun.arraySeries("values").front().values;
    QCOMPARE(samples.size(), 4);
    QVERIFY(samples[0] != samples[1] || samples[1] != samples[2] || samples[2] != samples[3]);

    Document macros;
    MacroDefinitionData noiseMacro;
    noiseMacro.displayName = "Noise";
    noiseMacro.parameters = {{"x", {}}};
    noiseMacro.equation = "x + RANDOM(0, 1)";
    QVERIFY(macros.defineMacro(noiseMacro));
    const EntityId output = macros.createVariable(ElementKind::Variable);
    macros.rename(output, "output");
    macros.setEquation(output, "Noise(0) + Noise(0)");
    macros.setSimulationSettings({0.0, 0.0, 1.0, 1.0});
    const SimulationRun macroFirst = runModel(macros);
    const SimulationRun macroSecond = runModel(macros);
    QCOMPARE(macroFirst.series("output"), macroSecond.series("output"));
    QVERIFY(macroFirst.series("output").front() != 0.0);
}

void TestSimulation::randomFunctionDistributionsAndErrors()
{
    const SimulationRun run = runModel(randomDocument());
    QCOMPARE(run.status(), SimulationRunStatus::Completed);
    const auto uniform = meanVariance(run.series("uniform"));
    expectNear(uniform.first, 5.0, 0.15);
    expectNear(uniform.second, 100.0 / 12.0, 0.5);
    for (const double value : run.series("uniform"))
        QVERIFY(value > 0.0 && value < 10.0);
    const auto normal = meanVariance(run.series("normal_output"));
    expectNear(normal.first, 2.0, 0.05);
    expectNear(normal.second, 1.0, 0.08);
    const auto lognormal = meanVariance(run.series("lognormal_output"));
    expectNear(lognormal.first, 10.0, 0.15);
    expectNear(lognormal.second, 1.0, 0.2);
    const auto poisson = meanVariance(run.series("poisson_output"));
    expectNear(poisson.first, 3.0, 0.08);
    expectNear(poisson.second, 3.0, 0.25);
    for (const double value : run.series("poisson_output"))
        QVERIFY(value == std::floor(value));
    const auto exponential = meanVariance(run.series("exponential_output"));
    expectNear(exponential.first, 1.0, 0.08);
    expectNear(exponential.second, 1.0, 0.2);

    const auto checkKs = [](const QString &name, double statistic) {
        QVERIFY2(statistic < 0.02, qPrintable(name + ":" + QString::number(statistic)));
    };
    checkKs("uniform", ksStatistic(run.series("uniform"), [](double value) { return value / 10.0; }));
    checkKs("normal", ksStatistic(run.series("normal_output"), [](double value) { return normalCdf(value, 2.0, 1.0); }));
    const double sigmaSquared = std::log1p(0.01);
    const double mu = std::log(10.0) - sigmaSquared / 2.0;
    checkKs("lognormal", ksStatistic(run.series("lognormal_output"), [sigmaSquared, mu](double value) {
        return value <= 0.0 ? 0.0 : normalCdf(std::log(value), mu, std::sqrt(sigmaSquared));
    }));
    checkKs("poisson", ksStatistic(run.series("poisson_output"), [](double value) { return poissonCdf(value, 3.0); }, true));
    checkKs("exponential", ksStatistic(run.series("exponential_output"), [](double value) { return 1.0 - std::exp(-value); }));

    const QVector<QString> invalid {
        "RANDOM(1, 1)", "RANDOM(2, 1)", "NORMAL(0, -1)", "LOGNORMAL(0, 1)",
        "LOGNORMAL(-1, 0)", "POISSON(-1)", "EXPRND(0)", "RANDOM(0, 1, -1)",
        "RANDOM(0, 1, 0.5)", "RANDOM(0, 1, 1e20)", "RANDOM(0, 1, 1 / 0)"
    };
    for (const QString &equation : invalid) {
        Document document;
        const EntityId output = document.createVariable(ElementKind::Variable);
        document.rename(output, "output");
        document.setEquation(output, equation);
        document.setSimulationSettings({0.0, 0.0, 1.0, 1.0});
        SimulationCompileResult compiled = compileSimulation(document);
        if (compiled.plan) {
            const SimulationRun failed = SimulationRunner(document, *compiled.plan).run();
            QCOMPARE(failed.status(), SimulationRunStatus::Failed);
        } else {
            QVERIFY(!compiled.issues.isEmpty());
        }
    }

    Document dynamicSeed;
    const EntityId seed = dynamicSeed.createVariable(ElementKind::Variable);
    dynamicSeed.rename(seed, "NOISE SEED");
    dynamicSeed.setEquation(seed, "1 + 1");
    dynamicSeed.setSimulationSettings({0.0, 0.0, 1.0, 1.0});
    QVERIFY(hasIssue(compileSimulation(dynamicSeed), "invalid-noise-seed"));

    Document rng;
    const EntityId engine = rng.createVariable(ElementKind::Variable);
    rng.rename(engine, "NOISE RNG");
    rng.setEquation(engine, "1");
    rng.setSimulationSettings({0.0, 0.0, 1.0, 1.0});
    QVERIFY(hasIssue(compileSimulation(rng), "unsupported-noise-rng"));
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
    QVERIFY(compileSimulation(array).plan);

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
    failure.rename(stock, "stock");
    failure.setInitialEquation(stock, "1");
    failure.setEquation(flow, "1 / (Stock - 1)");
    const SimulationRun failed = runModel(failure);
    QCOMPARE(failed.status(), SimulationRunStatus::Failed);
    QVERIFY(failed.times().isEmpty());
    QVERIFY(failed.series("stock").isEmpty());
    QVERIFY(failed.arraySeries("stock").isEmpty());
    QVERIFY(failed.series("missing").isEmpty());
    QVERIFY(!failed.issues().isEmpty());
    QCOMPARE(failed.issues().front().code, QStringLiteral("non-finite-simulation-value"));
}

} // namespace

QTEST_APPLESS_MAIN(TestSimulation)
#include "TestSimulation.moc"
