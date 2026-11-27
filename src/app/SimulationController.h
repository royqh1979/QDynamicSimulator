#pragma once

#include <core/simulation/Simulation.h>

#include <QObject>
#include <QHash>
#include <QPointer>
#include <QString>
#include <QSet>
#include <QVector>

#include <atomic>
#include <memory>

class DocumentController;
class QThread;
class SimulationRunRequest;

using qds::core::RunId;
using qds::core::SimulationRun;

enum class SimulationSessionStatus {
    Idle,
    Compiling,
    Running,
    Cancelling,
    Completed,
    Cancelled,
    Failed
};

class SimulationController : public QObject
{
    Q_OBJECT

public:
    explicit SimulationController(DocumentController *documentController, QObject *parent = nullptr);
    ~SimulationController() override;

    bool start(const QString &runName, bool overwrite = false);
    void cancel();
    void reset();

    SimulationSessionStatus status() const { return m_status; }
    bool isBusy() const;
    const QVector<SimulationRun> &runs() const { return m_runs; }
    std::shared_ptr<const qds::core::SimulationRun> lastRun() const { return m_lastRun; }
    const QString &lastRunName() const { return m_lastRunName; }
    RunId lastRunId() const { return m_lastRunId; }
    bool hasRunName(const QString &name) const;
    bool isOutdated(const SimulationRun &run) const;
    void setRunLoaded(RunId id, bool loaded);
    bool isRunLoaded(RunId id) const;
    qds::core::EntityId workbenchVariable() const { return m_workbenchVariable; }
    const QString &workbenchNameKey() const { return m_workbenchNameKey; }
    void setWorkbenchVariable(qds::core::EntityId id);
    void setWorkbenchVariableName(const QString &nameKey);

signals:
    void statusChanged(SimulationSessionStatus status);
    void busyChanged(bool busy);
    void runTerminated(std::shared_ptr<const qds::core::SimulationRun> run);
    void namedRunsChanged();
    void workbenchVariableChanged();
    void compilationFailed(const QVector<qds::core::ModelIssue> &issues);
    void errorOccurred(const QString &message);

private:
    void setStatus(SimulationSessionStatus status);
    void workerFinished(std::shared_ptr<const qds::core::SimulationRun> run, quint64 generation);
    void workerThreadFinished(quint64 generation);

    QPointer<DocumentController> m_documentController;
    QVector<SimulationRun> m_runs;
    QSet<RunId> m_loadedRuns;
    std::shared_ptr<const SimulationRunRequest> m_activeRequest;
    std::shared_ptr<const qds::core::SimulationRun> m_lastRun;
    QString m_lastRunName;
    RunId m_lastRunId = 0;
    qds::core::EntityId m_workbenchVariable = 0;
    QString m_workbenchNameKey;
    QPointer<QThread> m_thread;
    std::shared_ptr<std::atomic_bool> m_cancelRequested;
    SimulationSessionStatus m_status = SimulationSessionStatus::Idle;
    quint64 m_generation = 0;
    RunId m_nextRunId = 1;
};

Q_DECLARE_METATYPE(SimulationSessionStatus)
Q_DECLARE_METATYPE(std::shared_ptr<const qds::core::SimulationRun>)
Q_DECLARE_METATYPE(QVector<qds::core::ModelIssue>)
