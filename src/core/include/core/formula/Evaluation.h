#pragma once

#include <core/Dimension.h>
#include <core/Element.h>
#include <core/formula/Formula.h>

#include <functional>
#include <memory>
#include <optional>
#include <QString>
#include <variant>
#include <QVector>
#include <QHash>
#include <random>
#include <utility>

namespace qds::core {

struct MacroDefinition;
class CompiledMacroDefinition;

struct ArrayData {
    QVector<QString> dimensions;
    QVector<std::size_t> shape;
    QVector<double> values;
};

class ArrayValue
{
public:
    ArrayValue() : ArrayValue({}, {}, {}) {}
    ArrayValue(QVector<QString> dimensions, QVector<std::size_t> shape, QVector<double> values);

    const QVector<QString> &dimensions() const { return m_data->dimensions; }
    const QVector<std::size_t> &shape() const { return m_data->shape; }
    const QVector<double> &values() const { return m_data->values; }
    QVector<double> &values();

    bool operator==(const ArrayValue &other) const;

private:
    std::shared_ptr<ArrayData> m_data;
};

using Value = std::variant<double, ArrayValue>;
using VariableReader = std::function<std::optional<Value>(const QString &nameKey)>;
using EntityReader = std::function<std::optional<Value>(EntityId id)>;
using LookupReader = std::function<std::optional<double>(const QString &nameKey, double input)>;
using DimensionReader = std::function<const Dimension *(const QString &)>;

struct MacroEvaluation {
    const MacroDefinition *definition = nullptr;
};
using MacroReader = std::function<std::optional<MacroEvaluation>(const QString &, int, int)>;

class ExecutionStack
{
public:
    static constexpr int maximumFrameDepth = 64;
    static constexpr int runValueStorageBytes = 4 * 1024 * 1024;
    static constexpr std::size_t maximumTimeFunctionArguments = 4;

    struct Mark {
        std::size_t values = 0;
        std::size_t frames = 0;
    };
    struct Frame {
        std::size_t base = 0;
        const MacroDefinition *source = nullptr;
        const CompiledMacroDefinition *compiled = nullptr;
    };

    ExecutionStack();
    void reserveValues(int capacity) { m_values.reserve(capacity); }
    Mark mark() const { return {static_cast<std::size_t>(m_values.size()), static_cast<std::size_t>(m_frames.size())}; }
    void restore(const Mark &mark);
    void pushValue(Value value) { m_values.push_back(std::move(value)); }
    Value &value(std::size_t index) { return m_values[index]; }
    const Value &value(std::size_t index) const { return m_values[index]; }
    void pushFrame(std::size_t base, const MacroDefinition *source);
    void pushFrame(std::size_t base, const CompiledMacroDefinition *compiled);
    std::size_t frameCount() const { return m_frames.size(); }
    const Frame &currentFrame() const { return m_frames.back(); }

private:
    QVector<Value> m_values;
    QVector<Frame> m_frames;
};

class RandomStreamState
{
public:
    static constexpr quint32 defaultNoiseSeed = 4487556;

    void setDefaultSeed(quint32 seed);
    std::mt19937 &stream(quint32 id);
    std::size_t streamCount() const { return m_streams.size(); }

private:
    quint32 m_defaultSeed = defaultNoiseSeed;
    QHash<quint32, std::mt19937> m_streams;
};

enum class DelayKind {
    Fixed,
    Material,
    Information
};

struct DelayPlan {
    static constexpr int maximumOrder = 64;

    DelayKind kind = DelayKind::Material;
    std::size_t count = 1;
    std::size_t base = 0;
    int order = 1;
    bool initializeOrder = false;
    bool discrete = false;
    QVector<QString> dimensions;
    QVector<std::size_t> shape;
};

struct EvaluationScope {
    QVector<QString> dimensions;
    QVector<std::size_t> indices;
    QString self;
    std::size_t elementIndex = 0;
};

struct EvaluationContext {
    double time = 0.0;
    double dt = 1.0;
    double inputTime = 0.0;
    double inputDt = 1.0;
    double startTime = 0.0;
    double stopTime = 0.0;
    VariableReader readVariable;
    EntityReader readEntity;
    LookupReader readLookup;
    DimensionReader readDimension;
    MacroReader readMacro;
    mutable ExecutionStack executionStack;
    mutable RandomStreamState randomStreams;
    const QVector<DelayPlan> *delayPlans = nullptr;
    QVector<double> *delayLevels = nullptr;
    QVector<double> *delayInputs = nullptr;
    QVector<double> *delayTimes = nullptr;
    QVector<double> *delayCurrentTimes = nullptr;
    QVector<double> *delayOutputs = nullptr;
    QVector<int> *delayOrders = nullptr;
    QVector<int> *delayPointers = nullptr;
    QVector<bool> *delayInitialized = nullptr;
    QVector<bool> *delayTouched = nullptr;
    QVector<bool> *delayOutputValid = nullptr;
    mutable EvaluationScope scope;
};

struct EvaluationResult {
    std::optional<Value> value;
    QString error;
    SourceSpan errorSpan;
};

EvaluationResult evaluateFormula(const FormulaNode *node, const EvaluationContext &context);

} // namespace qds::core
