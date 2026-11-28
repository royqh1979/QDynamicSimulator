#pragma once

#include <core/Dimension.h>
#include <core/Element.h>
#include <core/formula/Formula.h>

#include <functional>
#include <optional>
#include <QString>
#include <variant>
#include <QVector>
#include <utility>

namespace qds::core {

struct MacroDefinition;
class CompiledMacroDefinition;

struct ArrayValue {
    QVector<QString> dimensions;
    QVector<std::size_t> shape;
    QVector<double> values;

    bool operator==(const ArrayValue &other) const
    {
        return dimensions == other.dimensions && shape == other.shape && values == other.values;
    }
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

struct EvaluationScope {
    QVector<QString> dimensions;
    QVector<std::size_t> indices;
    QString self;
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
    EvaluationScope scope;
};

struct EvaluationResult {
    std::optional<Value> value;
    QString error;
    SourceSpan errorSpan;
};

EvaluationResult evaluateFormula(const FormulaNode *node, const EvaluationContext &context);

} // namespace qds::core
