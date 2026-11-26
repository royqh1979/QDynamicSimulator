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
        runChecksum += run.series(stock)->back();
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
        const QVector<double> *series = lookupRun.series(stock);
        if (!series)
            return 12;
        lookupChecksum += series->back();
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
        rk4Checksum += run.series(stock)->back();
        ++rk4Count;
    }
    const qint64 rk4Ns = timer.nsecsElapsed();
    const double rk4Expected = std::exp(steps * 0.01);
    if (std::abs(rk4Checksum / rk4Count - rk4Expected) > 1e-9 * std::abs(rk4Expected))
        return 15;

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
    return 0;
}
