#pragma once

#include <core/SimulationSettings.h>
#include <core/Validation.h>

#include <QHash>
#include <QVector>

#include <functional>
#include <optional>

namespace qds::core {

class Document;
struct SimulationCompileResult;

struct StockFlows {
    EntityId stock = 0;
    QVector<EntityId> inflows;
    QVector<EntityId> outflows;
};

class SimulationPlan
{
public:
    const SimulationSettings &settings() const { return m_settings; }
    const QVector<EntityId> &outputOrder() const { return m_outputOrder; }

private:
    friend SimulationCompileResult compileSimulation(const Document &document);
    friend class SimulationRunner;
    SimulationPlan() = default;

    SimulationSettings m_settings;
    QVector<EntityId> m_initializationOrder;
    QVector<EntityId> m_evaluationOrder;
    QVector<EntityId> m_outputOrder;
    QVector<StockFlows> m_stockFlows;
};

struct SimulationCompileResult {
    std::optional<SimulationPlan> plan;
    QVector<ModelIssue> issues;
};

SimulationCompileResult compileSimulation(const Document &document);

enum class SimulationRunStatus {
    Completed,
    Cancelled,
    Failed
};

class SimulationRun
{
public:
    SimulationRunStatus status() const { return m_status; }
    const SimulationSettings &settings() const { return m_settings; }
    const QVector<double> &times() const { return m_times; }
    const QVector<ModelIssue> &issues() const { return m_issues; }
    std::optional<double> endTime() const { return m_endTime; }
    const QVector<double> *series(EntityId id) const;

private:
    friend class SimulationRunner;
    SimulationRun() = default;

    SimulationRunStatus m_status = SimulationRunStatus::Failed;
    SimulationSettings m_settings;
    QVector<double> m_times;
    QHash<EntityId, QVector<double>> m_series;
    QVector<ModelIssue> m_issues;
    std::optional<double> m_endTime;
};

class SimulationRunner
{
public:
    using Cancellation = std::function<bool()>;

    SimulationRunner(const Document &document, const SimulationPlan &plan);
    SimulationRun run(const Cancellation &cancelled = {});

private:
    const Document &m_document;
    const SimulationPlan &m_plan;
};

} // namespace qds::core
