#include <core/Dimension.h>

#include <core/Identifier.h>

namespace qds::core {

const DimensionElement *Dimension::findElement(const std::string &name) const
{
    const std::string key = normalizeIdentifier(name);
    for (const DimensionElement &element : elements) {
        if (!element.nameKey.empty() && element.nameKey == key)
            return &element;
    }
    return nullptr;
}

} // namespace qds::core
