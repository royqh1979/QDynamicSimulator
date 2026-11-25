#pragma once

#include <qds/core/Formula.h>

#include <functional>
#include <optional>
#include <string>

namespace qds::core {

using VariableReader = std::function<std::optional<double>(EntityId)>;

struct EvaluationContext {
    double time = 0.0;
    double dt = 1.0;
    double startTime = 0.0;
    double stopTime = 0.0;
    VariableReader readVariable;
};

struct EvaluationResult {
    std::optional<double> value;
    std::string error;
    SourceSpan errorSpan;
};

EvaluationResult evaluateFormula(const FormulaNode *node, const EvaluationContext &context);

} // namespace qds::core
