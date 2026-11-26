#pragma once

#include <core/Dimension.h>
#include <core/formula/Formula.h>

#include <functional>
#include <optional>
#include <QString>
#include <variant>
#include <QVector>

namespace qds::core {

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
using DimensionReader = std::function<const Dimension *(const QString &)>;

struct EvaluationScope {
    QVector<QString> dimensions;
    QVector<std::size_t> indices;
    QString self;
};

struct EvaluationContext {
    double time = 0.0;
    double dt = 1.0;
    double startTime = 0.0;
    double stopTime = 0.0;
    VariableReader readVariable;
    DimensionReader readDimension;
    EvaluationScope scope;
};

struct EvaluationResult {
    std::optional<Value> value;
    QString error;
    SourceSpan errorSpan;
};

EvaluationResult evaluateFormula(const FormulaNode *node, const EvaluationContext &context);

} // namespace qds::core
