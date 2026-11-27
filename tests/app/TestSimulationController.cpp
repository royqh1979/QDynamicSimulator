#include "DocumentController.h"
#include "SimulationController.h"
#include "SimulationSettingsDialog.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFont>
#include <QLabel>
#include <QSignalSpy>
#include <QtTest>

#include <algorithm>
#include <utility>

using namespace qds::core;

namespace {

void createModel(DocumentController &document, double stop = 2.0)
{
    Document model;
    const EntityId stock = model.createVariable(ElementKind::Stock, {});
    const EntityId cloud = model.createCloud({100.0, 0.0});
    model.createFlow(cloud, stock);
    model.rename(stock, "Stock");
    model.setInitialEquation(stock, "0");
    model.setEquation(model.findByName("Flow1")->id(), "1");
    model.setSimulationSettings({0.0, stop, 1.0, 1.0});
    document.replaceDocument(std::move(model));
}

const RunVariableInfo &variable(const NamedRun &run, const QString &nameKey)
{
    const auto found = std::find_if(run.variables().begin(), run.variables().end(), [&nameKey](const RunVariableInfo &info) {
        return info.nameKey() == nameKey;
    });
    return *found;
}

void waitCompleted(SimulationController &controller)
{
    QTRY_COMPARE(controller.status(), SimulationSessionStatus::Completed);
    QTRY_VERIFY(!controller.isBusy());
}

class TestSimulationController : public QObject
{
    Q_OBJECT

private slots:
    void settingsDialog();
    void settingsUndoRedo();
    void namedRunsAndOverwrite();
    void revisionsAndReset();
    void compilationFailure();
    void cancellation();
    void resetDuringRun();
    void destructionDuringRun();
};

void TestSimulationController::settingsDialog()
{
    SimulationSettingsDialog dialog(SimulationSettings {});
    auto *dt = dialog.findChild<QDoubleSpinBox *>("dt");
    auto *save = dialog.findChild<QDoubleSpinBox *>("saveInterval");
    auto *start = dialog.findChild<QDoubleSpinBox *>("startTime");
    auto *stop = dialog.findChild<QDoubleSpinBox *>("stopTime");
    auto *method = dialog.findChild<QComboBox *>("integrationMethod");
    auto *unit = dialog.findChild<QComboBox *>("timeUnit");
    QVERIFY(dt && save && start && stop && method && unit);
    dt->setValue(0.5);
    QCOMPARE(save->value(), 0.5);
    start->setValue(2.0);
    stop->setValue(4.0);
    save->setValue(1.0);
    method->setCurrentIndex(1);
    unit->setCurrentText("Year");
    dialog.accept();
    QCOMPARE(dialog.result(), QDialog::Accepted);
    QCOMPARE(dialog.settings(), SimulationSettings(2.0, 4.0, 0.5, 1.0, IntegrationMethod::RungeKutta4, "Year"));

    SimulationSettingsDialog invalidDialog(dialog.settings());
    auto *invalidSave = invalidDialog.findChild<QDoubleSpinBox *>("saveInterval");
    invalidSave->setValue(0.75);
    invalidDialog.accept();
    QVERIFY(invalidDialog.result() != QDialog::Accepted);
    auto *error = invalidDialog.findChild<QLabel *>("settingsError");
    QVERIFY(error && !error->isHidden());
    invalidSave->setValue(1.0);
    invalidDialog.accept();
    QCOMPARE(invalidDialog.result(), QDialog::Accepted);
}

void TestSimulationController::settingsUndoRedo()
{
    DocumentController document;
    const SimulationSettings original = document.document().simulationSettings();
    const SimulationSettings custom {2.0, 4.0, 0.5, 1.0, IntegrationMethod::RungeKutta4, "Year"};
    QVERIFY(document.setSimulationSettings(custom));
    QCOMPARE(document.document().simulationSettings(), custom);
    QCOMPARE(document.simulationRevision(), quint64(1));
    document.undoStack()->undo();
    QCOMPARE(document.document().simulationSettings(), original);
    QCOMPARE(document.simulationRevision(), quint64(2));
    document.undoStack()->redo();
    QCOMPARE(document.document().simulationSettings(), custom);
    QCOMPARE(document.simulationRevision(), quint64(3));
    const int commands = document.undoStack()->count();
    QVERIFY(!document.setSimulationSettings({0.0, 1.0, 0.0, 0.0}));
    QCOMPARE(document.document().simulationSettings(), custom);
    QCOMPARE(document.simulationRevision(), quint64(3));
    QCOMPARE(document.undoStack()->count(), commands);
}

void TestSimulationController::namedRunsAndOverwrite()
{
    DocumentController document;
    createModel(document);
    SimulationController controller(&document);
    QSignalSpy terminated(&controller, &SimulationController::runTerminated);

    QVERIFY(controller.start("equilib"));
    waitCompleted(controller);
    QCOMPARE(terminated.count(), 1);
    QCOMPARE(controller.runs().size(), 1);
    QCOMPARE(controller.lastRunName(), QStringLiteral("equilib"));
    QCOMPARE(controller.lastRun()->status(), SimulationRunStatus::Completed);
    const NamedRun equilibrium = controller.runs().front();
    QCOMPARE(*equilibrium.run()->series(variable(equilibrium, "stock").id()), (QVector<double> {0.0, 1.0, 2.0}));
    QVERIFY(controller.isRunLoaded(equilibrium.id()));

    QVERIFY(controller.start("growth"));
    waitCompleted(controller);
    QCOMPARE(controller.runs().size(), 2);
    QCOMPARE(controller.runs().front().name(), QStringLiteral("growth"));
    QVERIFY(controller.isRunLoaded(controller.runs().front().id()));
    QVERIFY(controller.isRunLoaded(equilibrium.id()));

    QSignalSpy errors(&controller, &SimulationController::errorOccurred);
    QVERIFY(!controller.start("equilib"));
    QCOMPARE(errors.count(), 1);
    QCOMPARE(controller.runs().size(), 2);

    const RunId oldId = equilibrium.id();
    QVERIFY(controller.start("equilib", true));
    waitCompleted(controller);
    QCOMPARE(controller.runs().size(), 2);
    const QVector<NamedRun> runs = controller.runs();
    const auto replaced = std::find_if(runs.begin(), runs.end(), [](const NamedRun &run) {
        return run.name() == "equilib";
    });
    QVERIFY(replaced != runs.end());
    QVERIFY(replaced->id() != oldId);
    QVERIFY(replaced->id() > oldId);
    QVERIFY(controller.isRunLoaded(replaced->id()));
    QCOMPARE(*equilibrium.run()->series(variable(equilibrium, "stock").id()), (QVector<double> {0.0, 1.0, 2.0}));
}

void TestSimulationController::revisionsAndReset()
{
    DocumentController document;
    createModel(document);
    SimulationController controller(&document);
    controller.start("initial");
    waitCompleted(controller);
    const NamedRun run = controller.runs().front();
    QVERIFY(!controller.isOutdated(run));

    const EntityId stock = document.document().findByName("Stock")->id();
    document.moveElements({{stock, QPointF(50.0, 50.0)}});
    document.setFontStyle(stock, QFont("Arial", 18));
    document.setComment(stock, "Documentation only");
    QVERIFY(!controller.isOutdated(run));

    document.setEquation(document.document().findByName("Flow1")->id(), "2");
    QVERIFY(controller.isOutdated(run));
    QCOMPARE(*run.run()->series(variable(run, "stock").id()), (QVector<double> {0.0, 1.0, 2.0}));
    document.undoStack()->undo();
    QVERIFY(controller.isOutdated(run));

    document.setUnit(stock, "rabbit");
    QVERIFY(controller.isOutdated(run));
    QCOMPARE(variable(run, "stock").unit(), QString());

    document.replaceDocument(Document());
    QCOMPARE(controller.status(), SimulationSessionStatus::Idle);
    QVERIFY(controller.runs().isEmpty());
    QVERIFY(!controller.lastRun());
}

void TestSimulationController::compilationFailure()
{
    DocumentController document;
    SimulationController controller(&document);
    QSignalSpy errors(&controller, &SimulationController::errorOccurred);
    QVERIFY(!controller.start("  "));
    QCOMPARE(errors.count(), 1);

    document.createVariable(ElementKind::Stock, QPointF());
    QSignalSpy failures(&controller, &SimulationController::compilationFailed);
    QVERIFY(!controller.start("invalid"));
    QCOMPARE(controller.status(), SimulationSessionStatus::Failed);
    QCOMPARE(failures.count(), 1);
    QVERIFY(controller.runs().isEmpty());
    QVERIFY(!controller.isBusy());
}

void TestSimulationController::cancellation()
{
    DocumentController document;
    createModel(document, 100000.0);
    SimulationController controller(&document);
    QVERIFY(controller.start("long"));
    QVERIFY(!controller.start("second"));
    controller.cancel();
    QTRY_COMPARE(controller.status(), SimulationSessionStatus::Cancelled);
    QTRY_VERIFY(!controller.isBusy());
    QVERIFY(controller.lastRun());
    QCOMPARE(controller.lastRun()->status(), SimulationRunStatus::Cancelled);
    QVERIFY(controller.lastRun()->times().size() < 100001);
    QVERIFY(controller.runs().isEmpty());
}

void TestSimulationController::resetDuringRun()
{
    DocumentController document;
    createModel(document, 100000.0);
    SimulationController controller(&document);
    QSignalSpy terminated(&controller, &SimulationController::runTerminated);
    QVERIFY(controller.start("long"));
    controller.reset();
    QCOMPARE(controller.status(), SimulationSessionStatus::Idle);
    QVERIFY(controller.runs().isEmpty());
    QVERIFY(!controller.lastRun());
    QCoreApplication::processEvents();
    QCOMPARE(terminated.count(), 0);
}

void TestSimulationController::destructionDuringRun()
{
    auto *document = new DocumentController;
    createModel(*document, 100000.0);
    auto *controller = new SimulationController(document);
    QVERIFY(controller->start("long"));
    delete controller;
    QCoreApplication::processEvents();
    delete document;
    QCoreApplication::processEvents();
}

} // namespace

QTEST_MAIN(TestSimulationController)
#include "TestSimulationController.moc"
