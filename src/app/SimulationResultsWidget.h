#pragma once

#include "SimulationController.h"

#include <QWidget>

#include <optional>

class QComboBox;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QTableView;
class QTabWidget;
class CausesStripWidget;
class RunsCompareModel;
class SimulationTableModel;
class TimeSeriesChartWidget;

class SimulationResultsWidget : public QWidget
{
    Q_OBJECT

public:
    explicit SimulationResultsWidget(SimulationController *controller, QWidget *parent = nullptr);
    void showGraph();
    void showTable();
    void showCausesStrip();
    void showRunsCompare();
    void showRun(RunId id);

private:
    void refreshRuns();
    void refreshWorkbench();
    void refreshCauses(const QVector<NamedRun> &loadedRuns, const QString &rootNameKey);
    std::optional<NamedRun> currentRun() const;

    SimulationController *m_controller;
    QComboBox *m_variableCombo;
    QComboBox *m_runCombo;
    QListWidget *m_datasetsList;
    QLabel *m_runStatusLabel;
    SimulationTableModel *m_tableModel;
    TimeSeriesChartWidget *m_chart;
    CausesStripWidget *m_causesStrip;
    RunsCompareModel *m_runsCompareModel;
    QTableView *m_runsCompareTable;
    QTabWidget *m_tabs;
    QSet<RunId> m_knownRuns;
    bool m_refreshing = false;
};
