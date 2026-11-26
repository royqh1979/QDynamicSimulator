#include <core/Document.h>
#include <io/DocumentFile.h>
#include <io/XmileFile.h>

#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QTextStream>

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
    return 0;
}
