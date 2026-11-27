#pragma once

#include "SimulationController.h"

#include <QWidget>

class QScrollArea;
class QVBoxLayout;
class TimeSeriesChartWidget;

class CausesStripWidget : public QWidget
{
    Q_OBJECT

public:
    explicit CausesStripWidget(QWidget *parent = nullptr);

    void setRuns(const QVector<SimulationRun> &runs, const QString &rootNameKey, const QVector<QString> &causeNameKeys);
    const QVector<TimeSeriesChartWidget *> &charts() const { return m_charts; }
    void synchronizeTimeRange(TimeSeriesChartWidget *source, double minimum, double maximum);

private:
    QScrollArea *m_scrollArea;
    QWidget *m_container;
    QVBoxLayout *m_layout;
    QVector<TimeSeriesChartWidget *> m_charts;
    bool m_updatingRange = false;
};
