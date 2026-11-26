#pragma once

#include <cstddef>
#include <QString>

namespace qds::core {

enum class FunctionCategory {
    Mathematical,
    Statistical,
    Delay,
    TestInput,
    Time,
    Miscellaneous,
    Array,
    Compatibility
};

struct FunctionSignature {
    std::size_t minArguments = 0;
    std::size_t maxArguments = 0;
    FunctionCategory category = FunctionCategory::Miscellaneous;
};

const FunctionSignature *findFunctionSignature(const QString &nameKey);

} // namespace qds::core
