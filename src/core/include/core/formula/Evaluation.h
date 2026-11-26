#pragma once

#include <core/Dimension.h>
#include <core/formula/Formula.h>

#include <functional>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace qds::core {

struct ArrayValue {
    std::vector<std::string> dimensions;
    std::vector<std::size_t> shape;
    std::vector<double> values;

    bool operator==(const ArrayValue &other) const
    {
        return dimensions == other.dimensions && shape == other.shape && values == other.values;
    }
};

using Value = std::variant<double, ArrayValue>;
using VariableReader = std::function<std::optional<Value>(const std::string &nameKey)>;
using DimensionReader = std::function<const Dimension *(const std::string &)>;

struct EvaluationScope {
    std::vector<std::string> dimensions;
    std::vector<std::size_t> indices;
    std::string self;
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
    std::string error;
    SourceSpan errorSpan;
};

EvaluationResult evaluateFormula(const FormulaNode *node, const EvaluationContext &context);

} // namespace qds::core
