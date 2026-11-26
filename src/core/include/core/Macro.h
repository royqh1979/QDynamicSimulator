#pragma once

#include <core/formula/Formula.h>

#include <QString>
#include <QVector>

#include <cstddef>
#include <map>
#include <memory>
#include <utility>
#include <vector>

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

struct MacroParameterData {
    QString displayName;
    QString defaultEquation;
};

struct MacroDefinitionData {
    QString displayName;
    std::vector<MacroParameterData> parameters;
    QString equation;
};

class MacroParameter
{
public:
    MacroParameter(QString displayName, QString nameKey, Formula defaultFormula = {})
        : m_displayName(std::move(displayName))
        , m_nameKey(std::move(nameKey))
        , m_defaultFormula(std::move(defaultFormula))
    {
    }

    const QString &displayName() const { return m_displayName; }
    const QString &nameKey() const { return m_nameKey; }
    const Formula &defaultFormula() const { return m_defaultFormula; }
    const QString &defaultEquation() const { return m_defaultFormula.source(); }

private:
    QString m_displayName;
    QString m_nameKey;
    Formula m_defaultFormula;
};

class MacroDefinition
{
public:
    MacroDefinition(QString displayName, QString nameKey, std::vector<MacroParameter> parameters, Formula formula)
        : m_displayName(std::move(displayName))
        , m_nameKey(std::move(nameKey))
        , m_parameters(std::move(parameters))
        , m_formula(std::move(formula))
    {
    }

    MacroDefinition(const MacroDefinition &) = delete;
    MacroDefinition &operator=(const MacroDefinition &) = delete;
    MacroDefinition(MacroDefinition &&) noexcept = default;
    MacroDefinition &operator=(MacroDefinition &&) noexcept = default;

    const QString &displayName() const { return m_displayName; }
    const QString &nameKey() const { return m_nameKey; }
    const std::vector<MacroParameter> &parameters() const { return m_parameters; }
    const Formula &formula() const { return m_formula; }
    const QString &equation() const { return m_formula.source(); }

    MacroSignature signature() const { return {nameKey(), int(parameters().size()), 1}; }

    int requiredArguments() const
    {
        int count = parameters().size();
        while (count > 0 && !m_parameters[count - 1].defaultFormula().source().isEmpty())
            --count;
        return count;
    }

    MacroDefinitionData data() const
    {
        MacroDefinitionData result;
        result.displayName = displayName();
        result.equation = equation();
        for (const MacroParameter &parameter : parameters())
            result.parameters.push_back({parameter.displayName(), parameter.defaultEquation()});
        return result;
    }

private:
    QString m_displayName;
    QString m_nameKey;
    std::vector<MacroParameter> m_parameters;
    Formula m_formula;
};

class MacroRange
{
    using Store = std::map<MacroSignature, std::unique_ptr<MacroDefinition>>;

public:
    class Iterator
    {
    public:
        explicit Iterator(Store::const_iterator current)
            : m_current(current)
        {
        }
        const MacroDefinition &operator*() const { return *m_current->second; }
        const MacroDefinition *operator->() const { return m_current->second.get(); }
        Iterator &operator++()
        {
            ++m_current;
            return *this;
        }
        bool operator==(const Iterator &other) const { return m_current == other.m_current; }
        bool operator!=(const Iterator &other) const { return !(*this == other); }

    private:
        Store::const_iterator m_current;
    };

    explicit MacroRange(const Store &macros)
        : m_macros(macros)
    {
    }

    Iterator begin() const { return Iterator(m_macros.begin()); }
    Iterator end() const { return Iterator(m_macros.end()); }
    bool isEmpty() const { return m_macros.empty(); }
    bool empty() const { return isEmpty(); }
    std::size_t size() const { return m_macros.size(); }

private:
    const Store &m_macros;
};

} // namespace qds::core
