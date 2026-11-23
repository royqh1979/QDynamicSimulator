#include "DiagramItems.h"
#include "DiagramScene.h"
#include "DocumentController.h"
#include <qds/io/XmileFile.h>

#include <QApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QTextStream>

#include <vector>

using namespace qds::core;

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    const int count = application.arguments().value(1, QStringLiteral("1000")).toInt();
    if (count < 2)
        return 2;

    DocumentController controller;
    DiagramScene scene(&controller);
    std::vector<EntityId> ids;
    ids.reserve(static_cast<std::size_t>(count));
    QElapsedTimer timer;
    timer.start();
    const VariableType types[] = {
        VariableType::Stock, VariableType::Flow, VariableType::Auxiliary, VariableType::Constant,
        VariableType::Exogenous, VariableType::Cloud, VariableType::Annotation};
    for (int index = 0; index < count; ++index) {
        ids.push_back(controller.createVariable(types[index % 7], QPointF((index % 40) * 180.0, (index / 40) * 120.0)));
    }
    const qint64 createNs = timer.nsecsElapsed();

    timer.restart();
    int connectionCount = 0;
    for (int index = 1; index < count; ++index) {
        if (controller.createConnection(
                ids[static_cast<std::size_t>(index - 1)], ids[static_cast<std::size_t>(index)])
            != 0)
            ++connectionCount;
    }
    const qint64 connectNs = timer.nsecsElapsed();

    timer.restart();
    controller.moveVariables({{ids.front(), QPointF(20.0, 20.0)}, {ids.back(), QPointF(30.0, 30.0)}});
    const qint64 moveNs = timer.nsecsElapsed();

    timer.restart();
    controller.undoStack()->undo();
    controller.undoStack()->redo();
    const qint64 undoRedoNs = timer.nsecsElapsed();

    QTemporaryDir directory;
    if (!directory.isValid())
        return 3;
    const QString path = directory.filePath(QStringLiteral("profile.xmile"));
    QString error;
    timer.restart();
    if (!qds::io::saveXmile(path, controller.document(), &error))
        return 4;
    const qint64 saveNs = timer.nsecsElapsed();

    Document loaded;
    timer.restart();
    if (!qds::io::loadXmile(path, loaded, &error))
        return 5;
    const qint64 loadNs = timer.nsecsElapsed();

    const qint64 commands = controller.undoStack()->count();
    QTextStream(stdout) << "operation,count,total_ms,per_operation_us\n"
                        << "editor_create," << count << "," << createNs / 1e6 << "," << createNs / 1000.0 / count << "\n"
                        << "editor_connect," << connectionCount << "," << connectNs / 1e6 << "," << connectNs / 1000.0 / connectionCount << "\n"
                        << "editor_move_two,2," << moveNs / 1e6 << "," << moveNs / 1000.0 / 2 << "\n"
                        << "editor_undo_redo_macro,2," << undoRedoNs / 1e6 << "," << undoRedoNs / 1000.0 / 2 << "\n"
                        << "xmile_save," << count << "," << saveNs / 1e6 << "," << saveNs / 1000.0 / count << "\n"
                        << "xmile_load," << count << "," << loadNs / 1e6 << "," << loadNs / 1000.0 / count << "\n";
    return commands == count + connectionCount + 1
            && loaded.model().variables().size() == static_cast<std::size_t>(count)
        ? 0
        : 1;
}
