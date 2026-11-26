#include <core/Document.h>

#include <QtTest>

#include <limits>
#include <utility>

using namespace qds::core;

namespace {

SimulationSettings customSettings()
{
    return {2.0, 10.0, 0.5, 1.0, IntegrationMethod::RungeKutta4, QStringLiteral("Year")};
}

class TestSimulationSettings : public QObject
{
    Q_OBJECT

private slots:
    void defaultsAndValues();
    void validationIsAtomic();
    void documentLifecycle();
};

void TestSimulationSettings::defaultsAndValues()
{
    const SimulationSettings defaults;
    QVERIFY(defaults.isValid());
    QCOMPARE(defaults.startTime(), 0.0);
    QCOMPARE(defaults.stopTime(), 100.0);
    QCOMPARE(defaults.dt(), 0.25);
    QCOMPARE(defaults.saveInterval(), defaults.dt());
    QCOMPARE(defaults.integrationMethod(), IntegrationMethod::Euler);
    QVERIFY(defaults.timeUnit().isEmpty());

    const SimulationSettings custom = customSettings();
    QVERIFY(custom.isValid());
    QCOMPARE(custom.startTime(), 2.0);
    QCOMPARE(custom.stopTime(), 10.0);
    QCOMPARE(custom.dt(), 0.5);
    QCOMPARE(custom.saveInterval(), 1.0);
    QCOMPARE(custom.integrationMethod(), IntegrationMethod::RungeKutta4);
    QCOMPARE(custom.timeUnit(), QStringLiteral("Year"));
    QVERIFY(SimulationSettings(0.0, 0.0, 0.25, 0.25).isValid());
    QVERIFY(SimulationSettings(0.0, 1.0, 0.1, 0.3).isValid());
}

void TestSimulationSettings::validationIsAtomic()
{
    const double infinity = std::numeric_limits<double>::infinity();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const QVector<SimulationSettings> invalid {
        {nan, 10.0, 0.5, 1.0}, {0.0, infinity, 0.5, 1.0}, {10.0, 2.0, 0.5, 1.0},
        {0.0, 10.0, 0.0, 1.0}, {0.0, 10.0, -0.5, 1.0}, {0.0, 10.0, nan, 1.0},
        {0.0, 10.0, 0.5, 0.25}, {0.0, 10.0, 0.5, 0.75}, {0.0, 10.0, 0.5, infinity},
        {0.0, 10.0, 0.5, 1.0, static_cast<IntegrationMethod>(99)},
        {0.0, 10.0, 0.000000001, 0.000000001}
    };
    Document document;
    const SimulationSettings original = customSettings();
    QVERIFY(document.setSimulationSettings(original));
    for (const SimulationSettings &settings : invalid) {
        QVERIFY(!settings.isValid());
        QVERIFY(!document.setSimulationSettings(settings));
        QCOMPARE(document.simulationSettings(), original);
    }
}

void TestSimulationSettings::documentLifecycle()
{
    Document document;
    const SimulationSettings custom = customSettings();
    QVERIFY(document.setSimulationSettings(custom));
    Document moved(std::move(document));
    QCOMPARE(moved.simulationSettings(), custom);
    Document assigned;
    assigned = std::move(moved);
    QCOMPARE(assigned.simulationSettings(), custom);
    assigned.clear();
    QCOMPARE(assigned.simulationSettings(), SimulationSettings {});
}

} // namespace

QTEST_APPLESS_MAIN(TestSimulationSettings)
#include "TestSimulationSettings.moc"
