#pragma once

#include <cstddef>
#include <string>

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

const FunctionSignature *findFunctionSignature(const std::string &nameKey);

} // namespace qds::core
