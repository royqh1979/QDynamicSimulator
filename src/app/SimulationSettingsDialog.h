#pragma once

#include <core/SimulationSettings.h>

#include <QDialog>

class QComboBox;
class QDoubleSpinBox;
class QLabel;

class SimulationSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SimulationSettingsDialog(const qds::core::SimulationSettings &settings, QWidget *parent = nullptr);

    qds::core::SimulationSettings settings() const;
    void accept() override;

private:
    QDoubleSpinBox *m_startTime;
    QDoubleSpinBox *m_stopTime;
    QDoubleSpinBox *m_dt;
    QDoubleSpinBox *m_saveInterval;
    QComboBox *m_method;
    QComboBox *m_timeUnit;
    QLabel *m_error;
    double m_previousDt;
};
