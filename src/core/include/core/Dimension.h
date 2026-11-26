#pragma once

#include <string>
#include <vector>

namespace qds::core {

struct DimensionElement {
    std::string displayName;
    std::string nameKey;

    bool operator==(const DimensionElement &other) const
    {
        return displayName == other.displayName && nameKey == other.nameKey;
    }
    bool operator!=(const DimensionElement &other) const { return !(*this == other); }
};

struct Dimension {
    std::string displayName;
    std::string nameKey;
    std::vector<DimensionElement> elements;

    const DimensionElement *findElement(const std::string &name) const;
    bool operator==(const Dimension &other) const
    {
        return displayName == other.displayName && nameKey == other.nameKey && elements == other.elements;
    }
    bool operator!=(const Dimension &other) const { return !(*this == other); }
};

} // namespace qds::core
