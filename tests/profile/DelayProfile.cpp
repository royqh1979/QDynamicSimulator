#include <core/Document.h>
#include <core/simulation/Simulation.h>

#include <QElapsedTimer>
#include <QTextStream>

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <new>

using namespace qds::core;

namespace {

std::atomic<std::size_t> allocationCount {0};
std::atomic<std::size_t> allocationBytes {0};

struct Scenario {
    QString name;
    QString equation;
    IntegrationMethod method = IntegrationMethod::Euler;
    bool array = false;
    int order = 8;
};

} // namespace

void *operator new(std::size_t size)
{
    if (void *memory = std::malloc(size)) {
        allocationCount.fetch_add(1, std::memory_order_relaxed);
        allocationBytes.fetch_add(size, std::memory_order_relaxed);
        return memory;
    }
    throw std::bad_alloc();
}

void *operator new[](std::size_t size)
{
    if (void *memory = std::malloc(size)) {
        allocationCount.fetch_add(1, std::memory_order_relaxed);
        allocationBytes.fetch_add(size, std::memory_order_relaxed);
        return memory;
    }
    throw std::bad_alloc();
}

void operator delete(void *memory) noexcept
{
    std::free(memory);
}

void operator delete[](void *memory) noexcept
{
    std::free(memory);
}

void operator delete(void *memory, std::size_t) noexcept
{
    std::free(memory);
}

void operator delete[](void *memory, std::size_t) noexcept
{
    std::free(memory);
}

namespace {

Document delayDocument(const Scenario &scenario, int steps, int arraySize, int &elements)
{
    Document document;
    elements = scenario.array ? arraySize : 1;
    if (scenario.array)
        document.defineDimension("N", elements);
    const EntityId input = document.createVariable(ElementKind::Variable);
    document.rename(input, "input");
    const EntityId output = document.createVariable(ElementKind::Variable);
    document.rename(output, "output");
    if (scenario.array) {
        document.setDimensions(input, {"N"});
        document.setDimensions(output, {"N"});
    }
    document.setEquation(input, scenario.array ? "N" : "1");
    document.setEquation(output, scenario.equation);
    const double dt = 0.01;
    const double stop = steps * dt;
    document.setSimulationSettings({0.0, stop, dt, stop, scenario.method});
    return document;
}

double finalChecksum(const SimulationRun &run, bool array)
{
    if (!array)
        return run.series("output").back();
    double result = 0.0;
    for (const double value : run.arraySeries("output").back().values)
        result += value;
    return result;
}

void printMeasurement(QTextStream &out, const Scenario &scenario, const QString &phase, int runs, int steps,
    int elements, qint64 elapsedNs, double checksum, std::size_t allocations, std::size_t bytes)
{
    const double totalMs = elapsedNs / 1e6;
    const double perRun = elapsedNs / double(runs);
    const double perStep = steps > 0 ? elapsedNs / double(runs * steps) : 0.0;
    const double perElementStep = steps > 0 ? elapsedNs / double(runs * steps * elements) : 0.0;
    out << scenario.name << ',' << phase << ',' << runs << ',' << steps << ',' << elements << ','
        << scenario.order * elements << ',' << QString::number(totalMs, 'f', 6) << ','
        << QString::number(perRun, 'f', 3) << ',' << QString::number(perStep, 'f', 3) << ','
        << QString::number(perElementStep, 'f', 3) << ',' << QString::number(checksum, 'g', 17) << ','
        << allocations << ',' << bytes << '\n';
}

} // namespace

int main(int argc, char **argv)
{
    const int runs = argc > 1 ? QString(argv[1]).toInt() : 100;
    const int steps = argc > 2 ? QString(argv[2]).toInt() : 1000;
    const int arraySize = argc > 3 ? QString(argv[3]).toInt() : 16;
    if (runs <= 0 || steps <= 0 || arraySize <= 0)
        return 2;

    const QVector<Scenario> scenarios {
        {"material_n", "delayn(input, 8, 0, 8)", IntegrationMethod::Euler, false, 8},
        {"information_n", "smthn(input, 8, 0, 8)", IntegrationMethod::Euler, false, 8},
        {"material_n_rk4", "delayn(input, 8, 0, 8)", IntegrationMethod::RungeKutta4, false, 8},
        {"random_initial", "delayn(input, 8, RANDOM(0, 1), 8)", IntegrationMethod::Euler, false, 8},
        {"fixed", "delay(input, 0.5, 0)", IntegrationMethod::Euler, false, 50},
        {"continuous_delay3_rk4", "delay3(input, 8, 0)", IntegrationMethod::RungeKutta4, false, 3},
        {"material_n_array", "delayn(input, 8, 0, 8)", IntegrationMethod::Euler, true, 8},
        {"information_n_array", "smthn(input, 8, 0, 8)", IntegrationMethod::Euler, true, 8}
    };

    QTextStream out(stdout);
    out << "scenario,phase,runs,steps,elements,state_levels,total_ms,ns_per_run,ns_per_step,"
           "ns_per_element_step,checksum,allocations,allocated_bytes\n";
    for (const Scenario &scenario : scenarios) {
        int elements = 1;
        Document document = delayDocument(scenario, steps, arraySize, elements);

        allocationCount.store(0, std::memory_order_relaxed);
        allocationBytes.store(0, std::memory_order_relaxed);
        QElapsedTimer timer;
        timer.start();
        for (int run = 0; run < runs; ++run) {
            if (!compileSimulation(document).plan) {
                QTextStream(stderr) << "Compilation failed for " << scenario.name << '\n';
                return 3;
            }
        }
        const qint64 compileNs = timer.nsecsElapsed();
        printMeasurement(out, scenario, "compile", runs, 0, elements, compileNs, 0.0,
            allocationCount.load(std::memory_order_relaxed), allocationBytes.load(std::memory_order_relaxed));

        SimulationCompileResult compiled = compileSimulation(document);
        if (!compiled.plan)
            return 4;
        allocationCount.store(0, std::memory_order_relaxed);
        allocationBytes.store(0, std::memory_order_relaxed);
        double checksum = 0.0;
        timer.restart();
        for (int runIndex = 0; runIndex < runs; ++runIndex) {
            const SimulationRun run = SimulationRunner(document, *compiled.plan).run();
            if (run.status() != SimulationRunStatus::Completed || run.times().size() != 2) {
                QTextStream(stderr) << "Run failed for " << scenario.name << '\n';
                for (const ModelIssue &issue : run.issues())
                    QTextStream(stderr) << issue.code << ':' << issue.message << '\n';
                return 5;
            }
            const double final = finalChecksum(run, scenario.array);
            if (!std::isfinite(final))
                return 6;
            checksum += final;
        }
        const qint64 runNs = timer.nsecsElapsed();
        printMeasurement(out, scenario, "run", runs, steps, elements, runNs, checksum / runs,
            allocationCount.load(std::memory_order_relaxed), allocationBytes.load(std::memory_order_relaxed));
    }
    return 0;
}
