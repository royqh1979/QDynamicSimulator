#include <core/Lookup.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace qds::core {

LookupDefinition::LookupDefinition(QVector<double> x, QVector<double> y, LookupKind kind)
    : m_kind(kind)
    , m_x(std::move(x))
    , m_y(std::move(y))
{
}

std::optional<LookupDefinition> LookupDefinition::create(QVector<double> x, QVector<double> y, LookupKind kind)
{
    if (x.size() != y.size() || x.size() < 2)
        return std::nullopt;
    for (int index = 0; index < x.size(); ++index) {
        if (!std::isfinite(x[index]) || !std::isfinite(y[index])
            || (index > 0 && x[index] <= x[index - 1]))
            return std::nullopt;
    }
    if (kind == LookupKind::Discrete && y.back() != y[y.size() - 2])
        return std::nullopt;
    return LookupDefinition(std::move(x), std::move(y), kind);
}

double LookupDefinition::evaluate(double input) const
{
    if (!std::isfinite(input))
        return std::numeric_limits<double>::quiet_NaN();
    if (input <= m_x.front()) {
        if (input == m_x.front() || m_kind != LookupKind::Extrapolate)
            return m_y.front();
        const double slope = (m_y[1] - m_y[0]) / (m_x[1] - m_x[0]);
        return m_y.front() + (input - m_x.front()) * slope;
    }
    if (input >= m_x.back()) {
        const int last = m_x.size() - 1;
        if (input == m_x.back() || m_kind != LookupKind::Extrapolate)
            return m_y.back();
        const double slope = (m_y[last] - m_y[last - 1]) / (m_x[last] - m_x[last - 1]);
        return m_y.back() + (input - m_x.back()) * slope;
    }
    const auto upper = std::upper_bound(m_x.begin(), m_x.end(), input);
    const int right = static_cast<int>(upper - m_x.begin());
    const int left = right - 1;
    if (m_kind == LookupKind::Discrete)
        return m_y[left];
    const double fraction = (input - m_x[left]) / (m_x[right] - m_x[left]);
    return m_y[left] + fraction * (m_y[right] - m_y[left]);
}

} // namespace qds::core
