#include "CausesStripWidget.h"

#include "TimeSeriesChartWidget.h"

#include <QScrollArea>
#include <QSet>
#include <QVBoxLayout>

CausesStripWidget::CausesStripWidget(QWidget *parent)
    : QWidget(parent)
    , m_scrollArea(new QScrollArea(this))
    , m_container(new QWidget(m_scrollArea))
    , m_layout(new QVBoxLayout(m_container))
{
    m_scrollArea->setObjectName(QStringLiteral("causesStripScrollArea"));
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setWidget(m_container);
    m_layout->setContentsMargins(4, 4, 4, 4);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_scrollArea);
}

void CausesStripWidget::setRuns(const QVector<NamedRun> &runs, const QString &rootNameKey,
    const QVector<QString> &causeNameKeys)
{
    for (TimeSeriesChartWidget *chart : m_charts) {
        m_layout->removeWidget(chart);
        chart->hide();
        chart->deleteLater();
    }
    m_charts.clear();

    QVector<QString> nameKeys;
    QSet<QString> seen;
    if (!rootNameKey.isEmpty()) {
        nameKeys.push_back(rootNameKey);
        seen.insert(rootNameKey);
    }
    for (const QString &nameKey : causeNameKeys) {
        if (!nameKey.isEmpty() && !seen.contains(nameKey)) {
            nameKeys.push_back(nameKey);
            seen.insert(nameKey);
        }
    }
    for (const QString &nameKey : nameKeys) {
        auto *chart = new TimeSeriesChartWidget(m_container);
        chart->setObjectName(QStringLiteral("causesStripChart"));
        chart->setMinimumHeight(180);
        chart->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        chart->setRuns(runs, nameKey);
        m_layout->addWidget(chart);
        chart->show();
        m_charts.push_back(chart);
        connect(chart, &TimeSeriesChartWidget::timeRangeChanged, this,
            [this, chart](double minimum, double maximum) {
                synchronizeTimeRange(chart, minimum, maximum);
            });
    }
    if (!m_charts.isEmpty()) {
        const auto range = m_charts.front()->timeRange();
        if (range.second > range.first)
            synchronizeTimeRange(m_charts.front(), range.first, range.second);
    }
}

void CausesStripWidget::synchronizeTimeRange(TimeSeriesChartWidget *source, double minimum, double maximum)
{
    if (m_updatingRange)
        return;
    m_updatingRange = true;
    for (TimeSeriesChartWidget *chart : m_charts) {
        if (chart != source)
            chart->setTimeRange(minimum, maximum);
    }
    m_updatingRange = false;
}
