#pragma once

#include <QString>
#include <QVector>

namespace qds::core {

struct DimensionElement {
    QString displayName;
    QString nameKey;

    bool operator==(const DimensionElement &other) const
    {
        return displayName == other.displayName && nameKey == other.nameKey;
    }
    bool operator!=(const DimensionElement &other) const { return !(*this == other); }
};

struct Dimension {
    QString displayName;
    QString nameKey;
    QVector<DimensionElement> elements;

    const DimensionElement *findElement(const QString &name) const;
    bool operator==(const Dimension &other) const
    {
        return displayName == other.displayName && nameKey == other.nameKey && elements == other.elements;
    }
    bool operator!=(const Dimension &other) const { return !(*this == other); }
};

} // namespace qds::core
