#pragma once

#include <cstddef>
#include <QString>
#include <QVector>

namespace qds::core {

struct MacroParameter {
    QString displayName;
    QString nameKey;
    QString defaultEquation;
};

struct MacroDefinition {
    QString displayName;
    QString nameKey;
    QVector<MacroParameter> parameters;
    QString equation;

    std::size_t requiredArguments() const
    {
        std::size_t count = parameters.size();
        while (count > 0 && !parameters[count - 1].defaultEquation.isEmpty())
            --count;
        return count;
    }
};

} // namespace qds::core
