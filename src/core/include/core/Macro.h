#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace qds::core {

struct MacroParameter {
    std::string displayName;
    std::string nameKey;
    std::string defaultEquation;
};

struct MacroDefinition {
    std::string displayName;
    std::string nameKey;
    std::vector<MacroParameter> parameters;
    std::string equation;

    std::size_t requiredArguments() const
    {
        std::size_t count = parameters.size();
        while (count > 0 && !parameters[count - 1].defaultEquation.empty())
            --count;
        return count;
    }
};

} // namespace qds::core
