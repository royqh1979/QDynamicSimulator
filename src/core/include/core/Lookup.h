#pragma once

#include <QVector>

#include <cstddef>
#include <optional>

namespace qds::core {

enum class LookupKind {
    Continuous,
    Extrapolate,
    Discrete
};

class LookupDefinition
{
public:
    static std::optional<LookupDefinition> create(QVector<double> x, QVector<double> y, LookupKind kind);

    LookupKind kind() const { return m_kind; }
    const QVector<double> &x() const { return m_x; }
    const QVector<double> &y() const { return m_y; }
    double evaluate(double input) const;

    bool operator==(const LookupDefinition &other) const
    {
        return m_kind == other.m_kind && m_x == other.m_x && m_y == other.m_y;
    }
    bool operator!=(const LookupDefinition &other) const { return !(*this == other); }

private:
    LookupDefinition(QVector<double> x, QVector<double> y, LookupKind kind);

    LookupKind m_kind;
    QVector<double> m_x;
    QVector<double> m_y;
};

} // namespace qds::core
