#include <core/Document.h>
#include <core/simulation/Simulation.h>

#include <QTextStream>

using namespace qds::core;

int main(int argc, char **argv)
{
    const int count = argc > 1 ? QString(argv[1]).toInt() : 20000;
    if (count <= 0)
        return 2;
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
    document.setSimulationSettings({0.0, count - 1.0, 1.0, 1.0});
    const SimulationCompileResult compiled = compileSimulation(document);
    if (!compiled.plan)
        return 3;
    const SimulationRun run = SimulationRunner(document, *compiled.plan).run();
    if (run.status() != SimulationRunStatus::Completed || run.times().size() != count)
        return 4;

    QTextStream output(stdout);
    output.setRealNumberPrecision(17);
    output << "series,index,value\n";
    RandomStreamState state;
    auto &raw = state.stream(0);
    for (int index = 0; index < 16; ++index)
        output << "raw," << index << ',' << raw() << "\n";
    for (const QString &name : {QStringLiteral("uniform"), QStringLiteral("normal_output"),
             QStringLiteral("lognormal_output"), QStringLiteral("poisson_output"), QStringLiteral("exponential_output")}) {
        const QVector<double> series = run.series(name);
        for (int index = 0; index < series.size(); ++index)
            output << name << ',' << index << ',' << series[index] << "\n";
    }
    return 0;
}
