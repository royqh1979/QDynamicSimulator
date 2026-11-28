#pragma once

#include <core/SimulationSettings.h>
#include <core/Lookup.h>
#include <core/Validation.h>
#include <core/formula/Evaluation.h>

#include <QHash>
#include <QVector>

#include <functional>
#include <optional>

namespace qds::core {

class Document;
struct SimulationCompileResult;

using RunId = quint64;

class RunVariableInfo
{
public:
    RunVariableInfo(EntityId id, QString displayName, QString nameKey, QString unit, ElementKind kind, bool constant);

    const QString &displayName() const { return m_displayName; }
    const QString &nameKey() const { return m_nameKey; }
    const QString &unit() const { return m_unit; }
    ElementKind kind() const { return m_kind; }
    bool isConstant() const { return m_constant; }

private:
    friend class SimulationRun;

    EntityId id() const { return m_id; }

    EntityId m_id;
    QString m_displayName;
    QString m_nameKey;
    QString m_unit;
    ElementKind m_kind;
    bool m_constant;
};

struct SimulationRunMetadata {
    RunId id = 0;
    QString name;
    quint64 modelRevision = 0;
    QVector<RunVariableInfo> variables;
    QHash<EntityId, QVector<EntityId>> causes;
};

class ArrayLayout
{
public:
    const QVector<QString> &dimensions() const { return m_dimensions; }
    const QVector<std::size_t> &shape() const { return m_shape; }
    std::size_t count() const { return m_count; }
    std::optional<std::size_t> flatIndex(const QVector<std::size_t> &indices) const;
    QVector<std::size_t> mappingFrom(const ArrayLayout &source) const;

private:
    friend SimulationCompileResult compileSimulation(const Document &document);
    ArrayLayout(QVector<QString> dimensions, QVector<std::size_t> shape,
        QVector<std::size_t> strides, std::size_t count);

    QVector<QString> m_dimensions;
    QVector<std::size_t> m_shape;
    QVector<std::size_t> m_strides;
    std::size_t m_count;
};

enum class ReferenceBindingKind {
    Variable,
    Dimension,
    DimensionElement,
    Self,
    Time,
    Dt,
    StartTime,
    StopTime,
    Pi,
    Infinity,
    Lookup
};

struct ReferenceBinding {
    ReferenceBindingKind kind = ReferenceBindingKind::Variable;
    EntityId variable = 0;
    std::size_t element = 0;
    std::optional<LookupDefinition> lookup;
};

struct FlowRatePlan {
    EntityId flow = 0;
    bool subtract = false;
    bool direct = false;
    bool compatible = true;
    QVector<std::size_t> mapping;
};

struct StockFlows {
    EntityId stock = 0;
    QVector<FlowRatePlan> flows;
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
    QHash<EntityId, ArrayLayout> m_arrayLayouts;
    QHash<QString, ReferenceBinding> m_referenceBindings;
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
    RunId id() const { return m_id; }
    const QString &name() const { return m_name; }
    quint64 modelRevision() const { return m_modelRevision; }
    const QVector<RunVariableInfo> &variables() const { return m_variables; }
    QVector<QString> causes(const QString &nameKey) const;
    SimulationRun withMetadata(SimulationRunMetadata metadata) const;

    SimulationRunStatus status() const { return m_status; }
    const SimulationSettings &settings() const { return m_settings; }
    const QVector<double> &times() const { return m_times; }
    const QVector<ModelIssue> &issues() const { return m_issues; }
    std::optional<double> endTime() const { return m_endTime; }
    QVector<double> series(const QString &nameKey) const;
    QVector<ArrayValue> arraySeries(const QString &nameKey) const;

private:
    friend class SimulationRunner;
    SimulationRun() = default;

    const RunVariableInfo *variable(const QString &nameKey) const;
    const RunVariableInfo *variable(EntityId id) const;
    QVector<double> series(EntityId id) const;
    QVector<ArrayValue> arraySeries(EntityId id) const;
    QVector<EntityId> causes(EntityId dependent) const { return m_causes.value(dependent); }

    RunId m_id = 0;
    QString m_name;
    quint64 m_modelRevision = 0;
    QVector<RunVariableInfo> m_variables;
    QHash<EntityId, QVector<EntityId>> m_causes;
    SimulationRunStatus m_status = SimulationRunStatus::Failed;
    SimulationSettings m_settings;
    QVector<double> m_times;
    QHash<EntityId, QVector<double>> m_series;
    QHash<EntityId, QVector<ArrayValue>> m_arraySeries;
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
