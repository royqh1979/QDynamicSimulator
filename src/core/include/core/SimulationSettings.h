#pragma once

#include <QString>

namespace qds::core {

enum class IntegrationMethod {
    Euler,
    RungeKutta4
};

class SimulationSettings
{
public:
    SimulationSettings() = default;
    SimulationSettings(double startTime, double stopTime, double dt, double saveInterval,
        IntegrationMethod integrationMethod = IntegrationMethod::Euler, QString timeUnit = {});

    double startTime() const { return m_startTime; }
    double stopTime() const { return m_stopTime; }
    double dt() const { return m_dt; }
    double saveInterval() const { return m_saveInterval; }
    IntegrationMethod integrationMethod() const { return m_integrationMethod; }
    const QString &timeUnit() const { return m_timeUnit; }

    bool isValid() const;
    bool operator==(const SimulationSettings &other) const;
    bool operator!=(const SimulationSettings &other) const { return !(*this == other); }

private:
    double m_startTime = 0.0;
    double m_stopTime = 100.0;
    double m_dt = 0.25;
    double m_saveInterval = 0.25;
    IntegrationMethod m_integrationMethod = IntegrationMethod::Euler;
    QString m_timeUnit;
};

} // namespace qds::core
