#include "TimeSeriesChartWidget.h"

#include <QChart>
#include <QLineSeries>
#include <QLegendMarker>
#include <QMouseEvent>
#include <QValueAxis>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

using namespace QtCharts;

TimeSeriesChartWidget::TimeSeriesChartWidget(QWidget *parent)
    : QChartView(parent)
    , m_chart(new QChart)
{
    setChart(m_chart);
    setRenderHint(QPainter::Antialiasing);
    setRubberBand(QChartView::NoRubberBand);
    setRuns({}, {});
}

void TimeSeriesChartWidget::setRuns(const QVector<NamedRun> &runs, const QString &nameKey)
{
    m_chart->removeAllSeries();
    const auto axes = m_chart->axes();
    for (QAbstractAxis *axis : axes) {
        m_chart->removeAxis(axis);
        delete axis;
    }
    m_chart->legend()->setVisible(true);
    m_hasRange = false;
    m_chart->setTitle(nameKey.isEmpty() ? tr("No variable selected") : nameKey);

    double minimumTime = 0.0;
    double maximumTime = 0.0;
    double minimumValue = 0.0;
    double maximumValue = 0.0;
    bool hasSamples = false;
    QString displayName;
    for (const NamedRun &run : runs) {
        const auto variable = std::find_if(run.variables().begin(), run.variables().end(), [&nameKey](const RunVariableInfo &candidate) {
            return candidate.nameKey() == nameKey;
        });
        if (variable == run.variables().end())
            continue;
        const QVector<double> *values = run.run()->series(variable->id());
        if (!values)
            continue;
        displayName = variable->displayName();
        const QVector<double> &times = run.run()->times();
        const int count = std::min(times.size(), values->size());
        QList<QPointF> points;
        points.reserve(count);
        for (int index = 0; index < count; ++index) {
            const double time = times[index];
            const double value = (*values)[index];
            points.append(QPointF(time, value));
            if (!hasSamples) {
                minimumTime = maximumTime = time;
                minimumValue = maximumValue = value;
                hasSamples = true;
            } else {
                minimumTime = std::min(minimumTime, time);
                maximumTime = std::max(maximumTime, time);
                minimumValue = std::min(minimumValue, value);
                maximumValue = std::max(maximumValue, value);
            }
        }
        auto *series = new QLineSeries(m_chart);
        series->setName(run.name());
        series->append(points);
        m_chart->addSeries(series);
        for (QLegendMarker *marker : m_chart->legend()->markers(series)) {
            connect(marker, &QLegendMarker::clicked, series, [series]() {
                series->setVisible(!series->isVisible());
            });
        }
    }

    if (!hasSamples) {
        m_chart->setTitle(tr("No loaded data for this variable"));
        return;
    }
    m_chart->setTitle(displayName);
    m_chart->createDefaultAxes();
    auto *timeAxis = qobject_cast<QValueAxis *>(m_chart->axes(Qt::Horizontal).value(0));
    auto *valueAxis = qobject_cast<QValueAxis *>(m_chart->axes(Qt::Vertical).value(0));
    if (!timeAxis || !valueAxis)
        return;
    if (minimumTime == maximumTime) {
        minimumTime -= 0.5;
        maximumTime += 0.5;
    }
    if (minimumValue == maximumValue) {
        const double margin = std::max(1.0, std::abs(minimumValue) * 0.05);
        minimumValue -= margin;
        maximumValue += margin;
    }
    m_minimumTime = minimumTime;
    m_maximumTime = maximumTime;
    m_minimumValue = minimumValue;
    m_maximumValue = maximumValue;
    m_hasRange = true;
    resetZoom();
}

void TimeSeriesChartWidget::resetZoom()
{
    m_chart->zoomReset();
    if (!m_hasRange)
        return;
    auto *timeAxis = qobject_cast<QValueAxis *>(m_chart->axes(Qt::Horizontal).value(0));
    auto *valueAxis = qobject_cast<QValueAxis *>(m_chart->axes(Qt::Vertical).value(0));
    if (timeAxis && valueAxis) {
        timeAxis->setRange(m_minimumTime, m_maximumTime);
        valueAxis->setRange(m_minimumValue, m_maximumValue);
    }
    emitCurrentTimeRange();
}

QPair<double, double> TimeSeriesChartWidget::timeRange() const
{
    const auto *timeAxis = qobject_cast<QValueAxis *>(m_chart->axes(Qt::Horizontal).value(0));
    return timeAxis ? QPair<double, double>(timeAxis->min(), timeAxis->max()) : QPair<double, double>();
}

void TimeSeriesChartWidget::setTimeRange(double minimum, double maximum)
{
    if (m_timeRangeGuard || !std::isfinite(minimum) || !std::isfinite(maximum) || minimum >= maximum)
        return;
    auto *timeAxis = qobject_cast<QValueAxis *>(m_chart->axes(Qt::Horizontal).value(0));
    if (!timeAxis)
        return;
    m_timeRangeGuard = true;
    timeAxis->setRange(minimum, maximum);
    m_timeRangeGuard = false;
}

void TimeSeriesChartWidget::emitCurrentTimeRange()
{
    if (m_timeRangeGuard)
        return;
    const auto range = timeRange();
    if (range.second > range.first)
        emit timeRangeChanged(range.first, range.second);
}

void TimeSeriesChartWidget::wheelEvent(QWheelEvent *event)
{
    m_chart->zoom(std::pow(1.15, event->angleDelta().y() / 120.0));
    emitCurrentTimeRange();
    event->accept();
}

void TimeSeriesChartWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_panning = true;
        m_lastPanPosition = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    QChartView::mousePressEvent(event);
}

void TimeSeriesChartWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_panning) {
        const QPoint delta = event->pos() - m_lastPanPosition;
        m_chart->scroll(-delta.x(), delta.y());
        m_lastPanPosition = event->pos();
        emitCurrentTimeRange();
        event->accept();
        return;
    }
    QChartView::mouseMoveEvent(event);
}

void TimeSeriesChartWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_panning && event->button() == Qt::LeftButton) {
        m_panning = false;
        unsetCursor();
        event->accept();
        return;
    }
    QChartView::mouseReleaseEvent(event);
}

void TimeSeriesChartWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        resetZoom();
        event->accept();
        return;
    }
    QChartView::mouseDoubleClickEvent(event);
}
