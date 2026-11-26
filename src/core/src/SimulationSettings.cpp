#include <core/SimulationSettings.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace qds::core {
namespace {

constexpr double maximumStepCount = 10000000.0;
constexpr double maximumSavedPointCount = 10000000.0;

bool nearInteger(double value)
{
    return std::abs(value - std::round(value))
        <= std::numeric_limits<double>::epsilon() * std::max(1.0, std::abs(value)) * 8.0;
}

} // namespace

SimulationSettings::SimulationSettings(double startTime, double stopTime, double dt, double saveInterval,
    IntegrationMethod integrationMethod, QString timeUnit)
    : m_startTime(startTime)
    , m_stopTime(stopTime)
    , m_dt(dt)
    , m_saveInterval(saveInterval)
    , m_integrationMethod(integrationMethod)
    , m_timeUnit(std::move(timeUnit))
{
}

bool SimulationSettings::isValid() const
{
    if (!std::isfinite(m_startTime) || !std::isfinite(m_stopTime) || !std::isfinite(m_dt)
        || !std::isfinite(m_saveInterval) || m_stopTime < m_startTime || m_dt <= 0.0
        || m_saveInterval < m_dt) {
        return false;
    }
    if (m_integrationMethod != IntegrationMethod::Euler && m_integrationMethod != IntegrationMethod::RungeKutta4)
        return false;
    const double saveRatio = m_saveInterval / m_dt;
    if (!std::isfinite(saveRatio) || !nearInteger(saveRatio))
        return false;
    const double stepCount = (m_stopTime - m_startTime) / m_dt;
    const double savedPointCount = (m_stopTime - m_startTime) / m_saveInterval;
    return std::isfinite(stepCount) && std::isfinite(savedPointCount)
        && stepCount <= maximumStepCount && savedPointCount <= maximumSavedPointCount;
}

bool SimulationSettings::operator==(const SimulationSettings &other) const
{
    return m_startTime == other.m_startTime && m_stopTime == other.m_stopTime && m_dt == other.m_dt
        && m_saveInterval == other.m_saveInterval && m_integrationMethod == other.m_integrationMethod
        && m_timeUnit == other.m_timeUnit;
}

} // namespace qds::core
