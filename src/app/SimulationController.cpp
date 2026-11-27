#include "SimulationController.h"

#include "DocumentController.h"
#include "SimulationCauses.h"
#include <core/formula/Formula.h>
#include <io/DocumentFile.h>

#include <QThread>

#include <algorithm>
#include <utility>

using namespace qds::core;

class SimulationRunRequest
{
public:
    SimulationRunRequest(std::shared_ptr<const Document> model, SimulationPlan plan, QString runName,
        quint64 modelRevision, QVector<RunVariableInfo> variables,
        QHash<EntityId, QVector<EntityId>> causes)
        : model(std::move(model))
        , plan(std::move(plan))
        , runName(std::move(runName))
        , modelRevision(modelRevision)
        , variables(std::move(variables))
        , causes(std::move(causes))
    {
    }

    std::shared_ptr<const Document> model;
    SimulationPlan plan;
    QString runName;
    quint64 modelRevision = 0;
    QVector<RunVariableInfo> variables;
    QHash<EntityId, QVector<EntityId>> causes;
};

namespace {

class SimulationWorker : public QObject
{
    Q_OBJECT

public:
    SimulationWorker(std::shared_ptr<const SimulationRunRequest> request,
        std::shared_ptr<std::atomic_bool> cancelRequested, quint64 generation)
        : m_request(std::move(request))
        , m_cancelRequested(std::move(cancelRequested))
        , m_generation(generation)
    {
    }

    void execute()
    {
        const SimulationRun result = SimulationRunner(*m_request->model, m_request->plan).run([this]() {
            return m_cancelRequested->load();
        });
        emit finished(std::make_shared<const SimulationRun>(result), m_generation);
    }

signals:
    void finished(std::shared_ptr<const SimulationRun> run, quint64 generation);

private:
    std::shared_ptr<const SimulationRunRequest> m_request;
    std::shared_ptr<std::atomic_bool> m_cancelRequested;
    quint64 m_generation;
};

} // namespace

RunVariableInfo::RunVariableInfo(EntityId id, QString displayName, QString nameKey, QString unit, ElementKind kind, bool constant)
    : m_id(id)
    , m_displayName(std::move(displayName))
    , m_nameKey(std::move(nameKey))
    , m_unit(std::move(unit))
    , m_kind(kind)
    , m_constant(constant)
{
}

NamedRun::NamedRun(RunId id, QString name, std::shared_ptr<const SimulationRun> run, quint64 modelRevision,
    QVector<RunVariableInfo> variables, QHash<EntityId, QVector<EntityId>> causes)
    : m_id(id)
    , m_name(std::move(name))
    , m_run(std::move(run))
    , m_modelRevision(modelRevision)
    , m_variables(std::move(variables))
    , m_causes(std::move(causes))
{
}

SimulationController::SimulationController(DocumentController *documentController)
    : QObject(documentController)
    , m_documentController(documentController)
{
    qRegisterMetaType<std::shared_ptr<const SimulationRun>>("std::shared_ptr<const SimulationRun>");
    qRegisterMetaType<std::shared_ptr<const SimulationRun>>("std::shared_ptr<const qds::core::SimulationRun>");
    qRegisterMetaType<QVector<ModelIssue>>();
    connect(m_documentController, &DocumentController::documentChanged, this,
        [this](const ChangeNotification &notification) {
            if (notification.documentReset) {
                reset();
                return;
            }
            if (m_workbenchVariable == 0)
                return;
            const auto *element = dynamic_cast<const ModelElement *>(m_documentController->document().find(m_workbenchVariable));
            if (!element) {
                setWorkbenchVariable(0);
                return;
            }
            if (m_workbenchNameKey != element->nameKey()) {
                m_workbenchNameKey = element->nameKey();
                emit workbenchVariableChanged();
            }
        });
}

SimulationController::~SimulationController()
{
    reset();
}

bool SimulationController::start(const QString &runName, bool overwrite)
{
    if (isBusy()) {
        emit errorOccurred(tr("A simulation is already running."));
        return false;
    }
    const QString name = runName.trimmed();
    if (name.isEmpty()) {
        emit errorOccurred(tr("Run name cannot be empty."));
        return false;
    }
    if (hasRunName(name) && !overwrite) {
        emit errorOccurred(tr("A run with this name already exists."));
        return false;
    }

    setStatus(SimulationSessionStatus::Compiling);
    m_lastRunName = name;
    QString error;
    const QByteArray xml = qds::io::DocumentFile::toXml(m_documentController->document(), {}, &error);
    Document copied;
    qds::io::Workspace ignoredWorkspace;
    if (xml.isEmpty()
        || !qds::io::DocumentFile::fromXml(xml, copied, ignoredWorkspace, &error)) {
        setStatus(SimulationSessionStatus::Failed);
        emit errorOccurred(error);
        return false;
    }
    auto model = std::make_shared<const Document>(std::move(copied));
    SimulationCompileResult compiled = compileSimulation(*model);
    if (!compiled.plan) {
        setStatus(SimulationSessionStatus::Failed);
        emit compilationFailed(compiled.issues);
        return false;
    }

    QVector<RunVariableInfo> variables;
    QHash<EntityId, QVector<EntityId>> causes;
    const AnalysisReport analysis = analyzeDocument(*model);
    for (EntityId id : compiled.plan->outputOrder()) {
        const auto *element = dynamic_cast<const ModelElement *>(model->find(id));
        if (element)
            variables.push_back(RunVariableInfo(id, element->displayName(), element->nameKey(), element->unit(), element->kind(),
                element->kind() == ElementKind::Variable && isConstantFormula(element->equation())));
        causes.insert(id, directSimulationCauses(*model, analysis, id));
    }
    m_activeRequest = std::make_shared<SimulationRunRequest>(std::move(model), std::move(*compiled.plan), name,
        m_documentController->simulationRevision(), std::move(variables), std::move(causes));
    m_cancelRequested = std::make_shared<std::atomic_bool>(false);
    const quint64 generation = ++m_generation;

    auto *thread = new QThread(this);
    auto *worker = new SimulationWorker(m_activeRequest, m_cancelRequested, generation);
    worker->moveToThread(thread);
    connect(thread, &QThread::started, worker, &SimulationWorker::execute);
    connect(worker, &SimulationWorker::finished, this,
        [this, generation](std::shared_ptr<const SimulationRun> run) {
            workerFinished(std::move(run), generation);
        });
    connect(worker, &SimulationWorker::finished, thread, &QThread::quit, Qt::DirectConnection);
    connect(thread, &QThread::finished, this, [this, generation]() {
        workerThreadFinished(generation);
    });
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    m_thread = thread;
    setStatus(SimulationSessionStatus::Running);
    thread->start();
    return true;
}

void SimulationController::cancel()
{
    if (!isBusy() || !m_cancelRequested)
        return;
    m_cancelRequested->store(true);
    setStatus(SimulationSessionStatus::Cancelling);
}

void SimulationController::reset()
{
    ++m_generation;
    if (m_cancelRequested)
        m_cancelRequested->store(true);
    if (m_thread) {
        QThread *thread = m_thread;
        m_thread.clear();
        if (thread->isRunning())
            thread->wait();
    }
    m_activeRequest.reset();
    m_cancelRequested.reset();
    m_lastRun.reset();
    m_lastRunName.clear();
    m_lastRunId = 0;
    m_workbenchVariable = 0;
    m_workbenchNameKey.clear();
    m_runs.clear();
    m_loadedRuns.clear();
    emit workbenchVariableChanged();
    m_nextRunId = 1;
    setStatus(SimulationSessionStatus::Idle);
    emit namedRunsChanged();
}

bool SimulationController::isBusy() const
{
    return m_thread != nullptr || m_status == SimulationSessionStatus::Compiling
        || m_status == SimulationSessionStatus::Running || m_status == SimulationSessionStatus::Cancelling;
}

bool SimulationController::hasRunName(const QString &name) const
{
    const QString trimmed = name.trimmed();
    return std::any_of(m_runs.begin(), m_runs.end(), [&trimmed](const NamedRun &run) {
        return run.name() == trimmed;
    });
}

bool SimulationController::isOutdated(const NamedRun &run) const
{
    return run.modelRevision() != m_documentController->simulationRevision();
}

void SimulationController::setRunLoaded(RunId id, bool loaded)
{
    const auto found = std::find_if(m_runs.begin(), m_runs.end(), [id](const NamedRun &run) {
        return run.id() == id;
    });
    if (found == m_runs.end())
        return;
    const bool old = m_loadedRuns.contains(id);
    if (loaded)
        m_loadedRuns.insert(id);
    else
        m_loadedRuns.remove(id);
    if (old != loaded)
        emit namedRunsChanged();
}

bool SimulationController::isRunLoaded(RunId id) const
{
    return m_loadedRuns.contains(id);
}

void SimulationController::setWorkbenchVariable(EntityId id)
{
    const auto *element = dynamic_cast<const ModelElement *>(m_documentController->document().find(id));
    const EntityId normalized = element ? id : 0;
    const QString nameKey = element ? element->nameKey() : QString();
    if (m_workbenchVariable == normalized && m_workbenchNameKey == nameKey)
        return;
    m_workbenchVariable = normalized;
    m_workbenchNameKey = nameKey;
    emit workbenchVariableChanged();
}

void SimulationController::setWorkbenchVariableName(const QString &nameKey)
{
    const ModelElement *element = nameKey.isEmpty() ? nullptr : m_documentController->document().findByName(nameKey);
    const EntityId id = element ? element->id() : 0;
    if (m_workbenchVariable == id && m_workbenchNameKey == nameKey)
        return;
    m_workbenchVariable = id;
    m_workbenchNameKey = nameKey;
    emit workbenchVariableChanged();
}

void SimulationController::setStatus(SimulationSessionStatus status)
{
    if (m_status == status)
        return;
    m_status = status;
    emit statusChanged(m_status);
    emit busyChanged(isBusy());
}

void SimulationController::workerFinished(std::shared_ptr<const SimulationRun> run, quint64 generation)
{
    if (generation != m_generation || !m_activeRequest)
        return;
    m_lastRun = std::move(run);
    m_lastRunName = m_activeRequest->runName;
    switch (m_lastRun->status()) {
    case SimulationRunStatus::Completed: {
        NamedRun named(m_nextRunId++, m_activeRequest->runName, m_lastRun, m_activeRequest->modelRevision,
            m_activeRequest->variables, m_activeRequest->causes);
        m_lastRunId = named.id();
        auto existing = std::find_if(m_runs.begin(), m_runs.end(), [this](const NamedRun &candidate) {
            return candidate.name() == m_activeRequest->runName;
        });
        if (existing != m_runs.end()) {
            const bool loaded = m_loadedRuns.contains(existing->id());
            m_loadedRuns.remove(existing->id());
            *existing = std::move(named);
            if (loaded)
                m_loadedRuns.insert(existing->id());
        } else {
            m_loadedRuns.insert(named.id());
            m_runs.prepend(std::move(named));
        }
        setStatus(SimulationSessionStatus::Completed);
        emit namedRunsChanged();
        break;
    }
    case SimulationRunStatus::Cancelled:
        setStatus(SimulationSessionStatus::Cancelled);
        break;
    case SimulationRunStatus::Failed:
        setStatus(SimulationSessionStatus::Failed);
        break;
    }
    emit runTerminated(m_lastRun);
}

void SimulationController::workerThreadFinished(quint64 generation)
{
    if (generation != m_generation)
        return;
    m_thread.clear();
    m_activeRequest.reset();
    m_cancelRequested.reset();
    emit busyChanged(isBusy());
}

#include "SimulationController.moc"
