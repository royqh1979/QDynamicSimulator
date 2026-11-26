#pragma once

#include <cstddef>
#include <QString>
#include <QVector>

namespace qds::core {

struct MacroSignature {
    QString nameKey;
    int parameterCount = 0;
    int outputCount = 1;

    bool operator==(const MacroSignature &other) const
    {
        return nameKey == other.nameKey && parameterCount == other.parameterCount && outputCount == other.outputCount;
    }
    bool operator<(const MacroSignature &other) const
    {
        if (nameKey != other.nameKey)
            return nameKey < other.nameKey;
        if (parameterCount != other.parameterCount)
            return parameterCount < other.parameterCount;
        return outputCount < other.outputCount;
    }
};

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

    MacroSignature signature() const { return {nameKey, parameters.size()}; }

    int requiredArguments() const
    {
        int count = parameters.size();
        while (count > 0 && !parameters[count - 1].defaultEquation.isEmpty())
            --count;
        return count;
    }
};

} // namespace qds::core
