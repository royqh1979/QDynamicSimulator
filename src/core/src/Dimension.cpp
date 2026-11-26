#include <core/Dimension.h>

#include <core/Identifier.h>

namespace qds::core {

const DimensionElement *Dimension::findElement(const QString &name) const
{
    const QString key = normalizeIdentifier(name);
    for (const DimensionElement &element : elements) {
        if (!element.nameKey.isEmpty() && element.nameKey == key)
            return &element;
    }
    return nullptr;
}

} // namespace qds::core
