#include <core/Document.h>
#include <core/simulation/Simulation.h>
#include <io/DocumentFile.h>
#include <io/XmileFile.h>

#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QTextStream>

#include <cmath>

using namespace qds::core;

int main(int argc, char **argv)
{
    const int count = argc > 1 ? QString(argv[1]).toInt() : 1000;
    if (count <= 0)
        return 2;

    const SimulationSettings settings {2.0, 12.0, 0.25, 0.5, IntegrationMethod::RungeKutta4, QStringLiteral("Month")};
    const SimulationSettings xmileSettings {2.0, 12.0, 0.25, 0.25, IntegrationMethod::RungeKutta4, QStringLiteral("Month")};
    Document document;
    document.createVariable(ElementKind::Stock);
    if (!document.setSimulationSettings(settings))
        return 3;
    QTemporaryDir directory;
    if (!directory.isValid())
        return 4;
    const QString qdsPath = directory.filePath(QStringLiteral("settings.qds"));
    const QString xmilePath = directory.filePath(QStringLiteral("settings.xmile"));
    const qds::io::Workspace workspace;
    QString error;
    QElapsedTimer timer;

    timer.start();
    int validationCount = 0;
    for (int index = 0; index < count; ++index) {
        if (!document.setSimulationSettings(settings)
            || document.setSimulationSettings({0.0, 10.0, 0.0, 0.0})
            || document.simulationSettings() != settings) {
            QTextStream(stderr) << "Simulation settings validation failed\n";
            return 5;
        }
        validationCount += 2;
    }
    const qint64 validationNs = timer.nsecsElapsed();

    timer.restart();
    int qdsCount = 0;
    for (int index = 0; index < count; ++index) {
        Document loaded;
        qds::io::Workspace loadedWorkspace;
        if (!qds::io::DocumentFile::save(qdsPath, document, workspace, &error)
            || !qds::io::DocumentFile::open(qdsPath, loaded, loadedWorkspace, &error)
            || loaded.simulationSettings() != settings) {
            QTextStream(stderr) << "QDS simulation settings roundtrip failed: " << error << "\n";
            return 6;
        }
        ++qdsCount;
    }
    const qint64 qdsNs = timer.nsecsElapsed();

    timer.restart();
    int xmileCount = 0;
    for (int index = 0; index < count; ++index) {
        Document loaded;
        if (!qds::io::XmileFile::exportFile(xmilePath, document, &error)
            || !qds::io::XmileFile::importFile(xmilePath, loaded, &error)
            || loaded.simulationSettings() != xmileSettings) {
            QTextStream(stderr) << "XMILE simulation settings roundtrip failed: " << error << "\n";
            return 7;
        }
        ++xmileCount;
    }
    const qint64 xmileNs = timer.nsecsElapsed();

    Document exponential;
    const EntityId stock = exponential.createVariable(ElementKind::Stock);
    const EntityId cloud = exponential.createCloud();
    const EntityId flow = exponential.createFlow(cloud, stock);
    exponential.rename(stock, "Stock");
    exponential.setInitialEquation(stock, "1");
    exponential.setEquation(flow, "Stock");
    const int steps = count;
    exponential.setSimulationSettings({0.0, steps * 0.01, 0.01, 0.01});
    timer.restart();
    int documentCopyCount = 0;
    for (int index = 0; index < count; ++index) {
        const QByteArray xml = qds::io::DocumentFile::toXml(exponential, {}, &error);
        Document copied;
        qds::io::Workspace copiedWorkspace;
        if (xml.isEmpty() || !qds::io::DocumentFile::fromXml(xml, copied, copiedWorkspace, &error)
            || copied.simulationSettings() != exponential.simulationSettings()
            || !copied.findByName("Stock")) {
            return 16;
        }
        ++documentCopyCount;
    }
    const qint64 documentCopyNs = timer.nsecsElapsed();
    timer.restart();
    int compileCount = 0;
    for (int index = 0; index < count; ++index) {
        if (!compileSimulation(exponential).plan)
            return 8;
        ++compileCount;
    }
    const qint64 compileNs = timer.nsecsElapsed();
    const SimulationCompileResult compiled = compileSimulation(exponential);
    if (!compiled.plan)
        return 9;

    timer.restart();
    int runCount = 0;
    double runChecksum = 0.0;
    int savedPoints = 0;
    for (int index = 0; index < count; ++index) {
        const SimulationRun run = SimulationRunner(exponential, *compiled.plan).run();
        if (run.status() != SimulationRunStatus::Completed || run.times().size() != steps + 1) {
            QTextStream(stderr) << "Euler profile failed: status=" << int(run.status())
                                << " samples=" << run.times().size() << " expected=" << steps + 1 << "\n";
            for (const ModelIssue &issue : run.issues())
                QTextStream(stderr) << issue.code << ":" << issue.message << "\n";
            for (int row = qMax(0, run.times().size() - 5); row < run.times().size(); ++row)
                QTextStream(stderr) << row << ":" << QString::number(run.times()[row], 'g', 17) << "\n";
            return 10;
        }
        runChecksum += run.series("stock").back();
        savedPoints += run.times().size();
        ++runCount;
    }
    const qint64 runNs = timer.nsecsElapsed();
    const double expectedFinal = std::pow(1.01, steps);
    if (std::abs(runChecksum / runCount - expectedFinal) > 1e-9 * std::max(1.0, std::abs(expectedFinal))) {
        QTextStream(stderr) << "Euler checksum mismatch: actual=" << QString::number(runChecksum / runCount, 'g', 17)
                            << " expected=" << QString::number(expectedFinal, 'g', 17) << "\n";
        return 11;
    }

    const SimulationRun lookupRun = SimulationRunner(exponential, *compiled.plan).run();
    timer.restart();
    double lookupChecksum = 0.0;
    for (int index = 0; index < count; ++index) {
        const QVector<double> series = lookupRun.series("stock");
        if (series.isEmpty())
            return 12;
        lookupChecksum += series.back();
    }
    const qint64 lookupNs = timer.nsecsElapsed();

    exponential.setSimulationSettings({0.0, steps * 0.01, 0.01, 0.01, IntegrationMethod::RungeKutta4});
    const SimulationCompileResult rk4Compiled = compileSimulation(exponential);
    if (!rk4Compiled.plan)
        return 13;
    timer.restart();
    int rk4Count = 0;
    double rk4Checksum = 0.0;
    for (int index = 0; index < count; ++index) {
        const SimulationRun run = SimulationRunner(exponential, *rk4Compiled.plan).run();
        if (run.status() != SimulationRunStatus::Completed || run.times().size() != steps + 1)
            return 14;
        rk4Checksum += run.series("stock").back();
        ++rk4Count;
    }
    const qint64 rk4Ns = timer.nsecsElapsed();
    const double rk4Expected = std::exp(steps * 0.01);
    if (std::abs(rk4Checksum / rk4Count - rk4Expected) > 1e-9 * std::abs(rk4Expected))
        return 15;

    Document arrayDocument;
    constexpr int arraySize = 16;
    arrayDocument.defineDimension("N", arraySize);
    const EntityId arrayStock = arrayDocument.createVariable(ElementKind::Stock);
    const EntityId arrayCloud = arrayDocument.createCloud();
    const EntityId arrayFlow = arrayDocument.createFlow(arrayCloud, arrayStock);
    arrayDocument.rename(arrayStock, "Stock");
    arrayDocument.setDimensions(arrayStock, {"N"});
    arrayDocument.setDimensions(arrayFlow, {"N"});
    arrayDocument.setInitialEquation(arrayStock, "1");
    arrayDocument.setEquation(arrayFlow, "Stock * N");
    arrayDocument.setSimulationSettings({0.0, steps * 0.01, 0.01, 0.01});
    const SimulationCompileResult arrayCompiled = compileSimulation(arrayDocument);
    if (!arrayCompiled.plan) {
        for (const ModelIssue &issue : arrayCompiled.issues)
            QTextStream(stderr) << issue.code << ':' << issue.message << "\n";
        return 17;
    }
    timer.restart();
    int arrayRunCount = 0;
    int arraySavedValues = 0;
    double arrayChecksum = 0.0;
    for (int index = 0; index < count; ++index) {
        const SimulationRun run = SimulationRunner(arrayDocument, *arrayCompiled.plan).run();
        const QVector<ArrayValue> series = run.arraySeries("stock");
        if (run.status() != SimulationRunStatus::Completed || run.times().size() != steps + 1
            || series.isEmpty() || series.size() != steps + 1)
            return 18;
        for (const double value : series.back().values)
            arrayChecksum += value;
        arraySavedValues += run.times().size() * arraySize;
        ++arrayRunCount;
    }
    const qint64 arrayNs = timer.nsecsElapsed();
    double arrayExpected = 0.0;
    for (int element = 1; element <= arraySize; ++element)
        arrayExpected += std::pow(1.0 + element * 0.01, steps);
    if (std::abs(arrayChecksum / arrayRunCount - arrayExpected) > 1e-8 * arrayExpected) {
        const SimulationRun diagnostic = SimulationRunner(arrayDocument, *arrayCompiled.plan).run();
        QTextStream(stderr) << "Array checksum mismatch: actual=" << arrayChecksum / arrayRunCount
                            << " expected=" << arrayExpected << " first=" << diagnostic.arraySeries("stock").back().values.front()
                            << " last=" << diagnostic.arraySeries("stock").back().values.back() << "\n";
        return 19;
    }

    Document lookupDocument;
    const EntityId lookup = lookupDocument.createLookup();
    lookupDocument.rename(lookup, "Rate Table");
    if (!lookupDocument.setLookup(lookup,
        LookupDefinition::create({0.0, steps * 0.01}, {0.0, steps * 0.01}, LookupKind::Continuous)))
        return 20;
    const EntityId lookupOutput = lookupDocument.createVariable(ElementKind::Variable);
    lookupDocument.rename(lookupOutput, "output");
    lookupDocument.setEquation(lookupOutput, "\"Rate Table\"(TIME)");
    lookupDocument.setSimulationSettings({0.0, steps * 0.01, 0.01, 0.01});
    timer.restart();
    int lookupCompileCount = 0;
    for (int index = 0; index < count; ++index) {
        if (!compileSimulation(lookupDocument).plan)
            return 27;
        ++lookupCompileCount;
    }
    const qint64 lookupCompileNs = timer.nsecsElapsed();
    const SimulationCompileResult lookupCompiled = compileSimulation(lookupDocument);
    if (!lookupCompiled.plan)
        return 21;
    timer.restart();
    int lookupRunCount = 0;
    double solverChecksum = 0.0;
    for (int index = 0; index < count; ++index) {
        const SimulationRun run = SimulationRunner(lookupDocument, *lookupCompiled.plan).run();
        if (run.status() != SimulationRunStatus::Completed || run.times().size() != steps + 1)
            return 22;
        solverChecksum += run.series("output").back();
        ++lookupRunCount;
    }
    const qint64 solverNs = timer.nsecsElapsed();
    if (std::abs(solverChecksum / lookupRunCount - steps * 0.01) > 1e-9)
        return 23;

    Document timeInputDocument;
    const auto addTimeInput = [&timeInputDocument](const QString &name, const QString &equation) {
        const EntityId id = timeInputDocument.createVariable(ElementKind::Variable);
        timeInputDocument.rename(id, name);
        timeInputDocument.setEquation(id, equation);
    };
    addTimeInput("step_output", "STEP(2, 2)");
    addTimeInput("pulse_output", "PULSE(2, 2)");
    addTimeInput("train_output", "pulse_train(1, 1, 2, 10)");
    addTimeInput("ramp_output", "RAMP(2, 1, 3)");
    timeInputDocument.setSimulationSettings({0.0, steps * 0.01, 0.01, 0.01});
    const SimulationCompileResult timeInputCompiled = compileSimulation(timeInputDocument);
    if (!timeInputCompiled.plan)
        return 24;
    timer.restart();
    int timeInputRunCount = 0;
    double timeInputChecksum = 0.0;
    for (int index = 0; index < count; ++index) {
        const SimulationRun run = SimulationRunner(timeInputDocument, *timeInputCompiled.plan).run();
        if (run.status() != SimulationRunStatus::Completed || run.times().size() != steps + 1)
            return 25;
        for (const QString &name : {QStringLiteral("step_output"), QStringLiteral("pulse_output"),
                 QStringLiteral("train_output"), QStringLiteral("ramp_output")})
            timeInputChecksum += run.series(name).back();
        ++timeInputRunCount;
    }
    const qint64 timeInputNs = timer.nsecsElapsed();
    if (std::abs(timeInputChecksum / timeInputRunCount - 6.0) > 1e-9)
        return 26;

    Document macroDocument;
    MacroDefinitionData scale;
    scale.displayName = "Scale";
    scale.parameters = {{"x", {}}, {"factor", "2"}};
    scale.equation = "x * factor";
    macroDocument.defineMacro(scale);
    const EntityId macroOutput = macroDocument.createVariable(ElementKind::Variable);
    macroDocument.rename(macroOutput, "output");
    macroDocument.setEquation(macroOutput, "Scale(TIME)");
    macroDocument.setSimulationSettings({0.0, steps * 0.01, 0.01, 0.01});
    timer.restart();
    int macroCompileCount = 0;
    for (int index = 0; index < count; ++index) {
        if (!compileSimulation(macroDocument).plan)
            return 28;
        ++macroCompileCount;
    }
    const qint64 macroCompileNs = timer.nsecsElapsed();
    const SimulationCompileResult macroCompiled = compileSimulation(macroDocument);
    if (!macroCompiled.plan)
        return 29;
    timer.restart();
    int macroRunCount = 0;
    double macroChecksum = 0.0;
    for (int index = 0; index < count; ++index) {
        const SimulationRun run = SimulationRunner(macroDocument, *macroCompiled.plan).run();
        if (run.status() != SimulationRunStatus::Completed || run.times().size() != steps + 1)
            return 30;
        macroChecksum += run.series("output").back();
        ++macroRunCount;
    }
    const qint64 macroRunNs = timer.nsecsElapsed();
    if (std::abs(macroChecksum / macroRunCount - steps * 0.02) > 1e-9)
        return 31;

    Document randomDocument;
    const auto addRandom = [&randomDocument](const QString &name, const QString &equation) {
        const EntityId id = randomDocument.createVariable(ElementKind::Variable);
        randomDocument.rename(id, name);
        randomDocument.setEquation(id, equation);
    };
    addRandom("uniform_output", "RANDOM(0, 10)");
    addRandom("normal_output", "NORMAL(2, 1)");
    addRandom("lognormal_output", "LOGNORMAL(10, 1)");
    addRandom("poisson_output", "POISSON(3)");
    addRandom("exponential_output", "EXPRND(1)");
    randomDocument.setSimulationSettings({0.0, steps * 0.01, 0.01, 0.01});
    timer.restart();
    int randomCompileCount = 0;
    for (int index = 0; index < count; ++index) {
        if (!compileSimulation(randomDocument).plan)
            return 32;
        ++randomCompileCount;
    }
    const qint64 randomCompileNs = timer.nsecsElapsed();
    const SimulationCompileResult randomCompiled = compileSimulation(randomDocument);
    if (!randomCompiled.plan)
        return 33;
    timer.restart();
    int randomRunCount = 0;
    double randomChecksum = 0.0;
    double randomExpected = 0.0;
    for (int index = 0; index < count; ++index) {
        const SimulationRun run = SimulationRunner(randomDocument, *randomCompiled.plan).run();
        if (run.status() != SimulationRunStatus::Completed || run.times().size() != steps + 1)
            return 34;
        double checksum = 0.0;
        for (const QString &name : {QStringLiteral("uniform_output"), QStringLiteral("normal_output"),
                 QStringLiteral("lognormal_output"), QStringLiteral("poisson_output"), QStringLiteral("exponential_output")})
            checksum += run.series(name).back();
        if (randomRunCount == 0)
            randomExpected = checksum;
        else if (checksum != randomExpected)
            return 35;
        randomChecksum += checksum;
        ++randomRunCount;
    }
    const qint64 randomRunNs = timer.nsecsElapsed();

    QTextStream output(stdout);
    output << "operation,count,total_ms,per_operation_us\n";
    output << "settings_validate," << validationCount << ',' << validationNs / 1.0e6 << ','
           << validationNs / 1000.0 / validationCount << "\n";
    output << "settings_qds_roundtrip," << qdsCount << ',' << qdsNs / 1.0e6 << ','
           << qdsNs / 1000.0 / qdsCount << "\n";
    output << "settings_xmile_roundtrip," << xmileCount << ',' << xmileNs / 1.0e6 << ','
           << xmileNs / 1000.0 / xmileCount << "\n";
    output << "settings_qds_checksum," << settings.stopTime() + settings.dt() << ",\n";
    output << "settings_xmile_checksum," << xmileSettings.stopTime() + xmileSettings.dt() << ",\n";
    output << "simulation_document_copy_xml," << documentCopyCount << ',' << documentCopyNs / 1.0e6 << ','
           << documentCopyNs / 1000.0 / documentCopyCount << "\n";
    output << "simulation_compile," << compileCount << ',' << compileNs / 1.0e6 << ','
           << compileNs / 1000.0 / compileCount << "\n";
    output << "simulation_euler," << runCount << ',' << runNs / 1.0e6 << ','
           << runNs / 1000.0 / runCount << "\n";
    output << "simulation_euler_step," << runCount * steps << ',' << runNs / 1.0e6 << ','
           << runNs / 1000.0 / (runCount * steps) << "\n";
    output << "simulation_result_lookup," << count << ',' << lookupNs / 1.0e6 << ','
           << lookupNs / 1000.0 / count << "\n";
    output << "simulation_saved_points," << savedPoints << ",\n";
    output << "simulation_euler_checksum," << runChecksum << ",\n";
    output << "simulation_lookup_checksum," << lookupChecksum << ",\n";
    output << "simulation_rk4," << rk4Count << ',' << rk4Ns / 1.0e6 << ','
           << rk4Ns / 1000.0 / rk4Count << "\n";
    output << "simulation_rk4_step," << rk4Count * steps << ',' << rk4Ns / 1.0e6 << ','
           << rk4Ns / 1000.0 / (rk4Count * steps) << "\n";
    output << "simulation_rk4_evaluation," << rk4Count * steps * 4 << ',' << rk4Ns / 1.0e6 << ','
           << rk4Ns / 1000.0 / (rk4Count * steps * 4) << "\n";
    output << "simulation_rk4_checksum," << rk4Checksum << ",\n";
    output << "simulation_array_euler," << arrayRunCount << ',' << arrayNs / 1.0e6 << ','
           << arrayNs / 1000.0 / arrayRunCount << "\n";
    output << "simulation_array_element_step," << arrayRunCount * steps * arraySize << ',' << arrayNs / 1.0e6 << ','
           << arrayNs / 1000.0 / (arrayRunCount * steps * arraySize) << "\n";
    output << "simulation_array_saved_values," << arraySavedValues << ",\n";
    output << "simulation_array_checksum," << arrayChecksum << ",\n";
    output << "simulation_lookup_compile," << lookupCompileCount << ',' << lookupCompileNs / 1.0e6 << ','
           << lookupCompileNs / 1000.0 / lookupCompileCount << "\n";
    output << "simulation_lookup_solver," << lookupRunCount << ',' << solverNs / 1.0e6 << ','
           << solverNs / 1000.0 / lookupRunCount << "\n";
    output << "simulation_lookup_evaluation," << lookupRunCount * steps << ',' << solverNs / 1.0e6 << ','
           << solverNs / 1000.0 / (lookupRunCount * steps) << "\n";
    output << "simulation_lookup_solver_checksum," << solverChecksum << ",\n";
    output << "simulation_time_input_functions," << timeInputRunCount << ',' << timeInputNs / 1.0e6 << ','
           << timeInputNs / 1000.0 / timeInputRunCount << "\n";
    output << "simulation_time_input_evaluations," << timeInputRunCount * steps << ','
           << timeInputNs / 1.0e6 << ',' << timeInputNs / 1000.0 / (timeInputRunCount * steps) << "\n";
    output << "simulation_time_input_checksum," << timeInputChecksum << ",\n";
    output << "simulation_macro_compile," << macroCompileCount << ',' << macroCompileNs / 1.0e6 << ','
           << macroCompileNs / 1000.0 / macroCompileCount << "\n";
    output << "simulation_macro_solver," << macroRunCount << ',' << macroRunNs / 1.0e6 << ','
           << macroRunNs / 1000.0 / macroRunCount << "\n";
    output << "simulation_macro_evaluations," << macroRunCount * steps << ',' << macroRunNs / 1.0e6 << ','
           << macroRunNs / 1000.0 / (macroRunCount * steps) << "\n";
    output << "simulation_macro_checksum," << macroChecksum << ",\n";
    output << "simulation_random_compile," << randomCompileCount << ',' << randomCompileNs / 1.0e6 << ','
           << randomCompileNs / 1000.0 / randomCompileCount << "\n";
    output << "simulation_random_functions," << randomRunCount << ',' << randomRunNs / 1.0e6 << ','
           << randomRunNs / 1000.0 / randomRunCount << "\n";
    output << "simulation_random_evaluations," << randomRunCount * steps * 5 << ',' << randomRunNs / 1.0e6 << ','
           << randomRunNs / 1000.0 / (randomRunCount * steps * 5) << "\n";
    output << "simulation_random_checksum," << randomChecksum << ",\n";
    output << "simulation_random_checksum_per_run," << randomExpected << ",\n";
    return 0;
}
