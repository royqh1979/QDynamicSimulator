#pragma once

#include "SimulationController.h"

#include <QChartView>
#include <QPair>

class TimeSeriesChartWidget : public QtCharts::QChartView
{
    Q_OBJECT

public:
    explicit TimeSeriesChartWidget(QWidget *parent = nullptr);

    void setRuns(const QVector<SimulationRun> &runs, const QString &nameKey);
    void resetZoom();
    QPair<double, double> timeRange() const;
    void setTimeRange(double minimum, double maximum);

signals:
    void timeRangeChanged(double minimum, double maximum);

protected:
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    void emitCurrentTimeRange();

    QtCharts::QChart *m_chart;
    double m_minimumTime = 0.0;
    double m_maximumTime = 0.0;
    double m_minimumValue = 0.0;
    double m_maximumValue = 0.0;
    bool m_hasRange = false;
    bool m_panning = false;
    bool m_timeRangeGuard = false;
    QPoint m_lastPanPosition;
};
