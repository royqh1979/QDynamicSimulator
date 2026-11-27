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

using RunId = quint64;

class RunVariableInfo
{
public:
    RunVariableInfo(qds::core::EntityId id, QString displayName, QString nameKey, QString unit, qds::core::ElementKind kind, bool constant);

    qds::core::EntityId id() const { return m_id; }
    const QString &displayName() const { return m_displayName; }
    const QString &nameKey() const { return m_nameKey; }
    const QString &unit() const { return m_unit; }
    qds::core::ElementKind kind() const { return m_kind; }
    bool isConstant() const { return m_constant; }

private:
    qds::core::EntityId m_id;
    QString m_displayName;
    QString m_nameKey;
    QString m_unit;
    qds::core::ElementKind m_kind;
    bool m_constant;
};

class NamedRun
{
public:
    NamedRun(RunId id, QString name, std::shared_ptr<const qds::core::SimulationRun> run, quint64 modelRevision,
        QVector<RunVariableInfo> variables,
        QHash<qds::core::EntityId, QVector<qds::core::EntityId>> causes = {});

    RunId id() const { return m_id; }
    const QString &name() const { return m_name; }
    std::shared_ptr<const qds::core::SimulationRun> run() const { return m_run; }
    quint64 modelRevision() const { return m_modelRevision; }
    const QVector<RunVariableInfo> &variables() const { return m_variables; }
    QVector<qds::core::EntityId> causes(qds::core::EntityId dependent) const { return m_causes.value(dependent); }

private:
    RunId m_id = 0;
    QString m_name;
    std::shared_ptr<const qds::core::SimulationRun> m_run;
    quint64 m_modelRevision = 0;
    QVector<RunVariableInfo> m_variables;
    QHash<qds::core::EntityId, QVector<qds::core::EntityId>> m_causes;
};

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
    explicit SimulationController(DocumentController *documentController);
    ~SimulationController() override;

    bool start(const QString &runName, bool overwrite = false);
    void cancel();
    void reset();

    SimulationSessionStatus status() const { return m_status; }
    bool isBusy() const;
    const QVector<NamedRun> &runs() const { return m_runs; }
    std::shared_ptr<const qds::core::SimulationRun> lastRun() const { return m_lastRun; }
    const QString &lastRunName() const { return m_lastRunName; }
    RunId lastRunId() const { return m_lastRunId; }
    bool hasRunName(const QString &name) const;
    bool isOutdated(const NamedRun &run) const;
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

    DocumentController *m_documentController;
    QVector<NamedRun> m_runs;
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
