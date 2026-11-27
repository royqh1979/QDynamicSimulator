#pragma once

#include "SimulationController.h"

#include <QWidget>

#include <optional>

class QComboBox;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QTableView;
class SimulationTableModel;

class SimulationResultsWidget : public QWidget
{
    Q_OBJECT

public:
    explicit SimulationResultsWidget(SimulationController *controller, QWidget *parent = nullptr);

private:
    void refreshRuns();
    void refreshWorkbench();
    std::optional<NamedRun> currentRun() const;

    SimulationController *m_controller;
    QComboBox *m_variableCombo;
    QComboBox *m_runCombo;
    QListWidget *m_datasetsList;
    QLabel *m_runStatusLabel;
    SimulationTableModel *m_tableModel;
    QSet<RunId> m_knownRuns;
    bool m_refreshing = false;
};
