#include <qds/core/Document.h>
#include <qds/core/Validation.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QTextStream>

#include <vector>

using namespace qds::core;

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    const int count = application.arguments().value(1, QStringLiteral("10000")).toInt();
    if (count < 2)
        return 2;

    QElapsedTimer timer;
    timer.start();
    Document document;
    std::vector<EntityId> ids;
    ids.reserve(static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index)
        ids.push_back(document.createVariable(VariableType::Stock, {index * 10.0, index * 5.0}));
    const qint64 createNs = timer.nsecsElapsed();

    timer.restart();
    for (int index = 1; index < count; ++index)
        document.createConnection(ids[static_cast<std::size_t>(index - 1)], ids[static_cast<std::size_t>(index)]);
    const qint64 connectNs = timer.nsecsElapsed();

    timer.restart();
    quint64 checksum = 0;
    for (int repeat = 0; repeat < 10; ++repeat) {
        for (EntityId id : ids)
            checksum += document.variable(id) ? document.variable(id)->id : 0;
    }
    const qint64 lookupNs = timer.nsecsElapsed();

    timer.restart();
    for (int index = 0; index < count; ++index)
        document.setPosition(ids[static_cast<std::size_t>(index)], {index * 11.0, index * 6.0});
    const qint64 moveNs = timer.nsecsElapsed();

    timer.restart();
    const auto issues = validateDocument(document);
    const qint64 validationNs = timer.nsecsElapsed();

    timer.restart();
    for (EntityId id : ids)
        document.removeVariable(id);
    const qint64 removeNs = timer.nsecsElapsed();

    QTextStream(stdout) << "operation,count,total_ms,per_operation_us\n"
                        << "create," << count << "," << createNs / 1e6 << "," << createNs / 1000.0 / count << "\n"
                        << "connect," << count - 1 << "," << connectNs / 1e6 << "," << connectNs / 1000.0 / (count - 1) << "\n"
                        << "lookup," << count * 10 << "," << lookupNs / 1e6 << "," << lookupNs / 1000.0 / (count * 10) << "\n"
                        << "move," << count << "," << moveNs / 1e6 << "," << moveNs / 1000.0 / count << "\n"
                        << "validate," << count << "," << validationNs / 1e6 << "," << validationNs / 1000.0 / count << "\n"
                        << "remove," << count << "," << removeNs / 1e6 << "," << removeNs / 1000.0 / count << "\n";
    return checksum == 0 || !document.model().variables().empty() ? 1 : 0;
}
