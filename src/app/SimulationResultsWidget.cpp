#include "SimulationResultsWidget.h"

#include "SimulationTableModel.h"
#include "TimeSeriesChartWidget.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTableView>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>

SimulationResultsWidget::SimulationResultsWidget(SimulationController *controller, QWidget *parent)
    : QWidget(parent)
    , m_controller(controller)
    , m_variableCombo(new QComboBox(this))
    , m_runCombo(new QComboBox(this))
    , m_datasetsList(new QListWidget(this))
    , m_runStatusLabel(new QLabel(this))
    , m_tableModel(new SimulationTableModel(this))
    , m_chart(new TimeSeriesChartWidget(this))
    , m_tabs(new QTabWidget(this))
{
    m_variableCombo->setObjectName(QStringLiteral("workbenchVariableCombo"));
    m_runCombo->setObjectName(QStringLiteral("resultsRunCombo"));
    m_datasetsList->setObjectName(QStringLiteral("datasetsList"));
    m_runStatusLabel->setObjectName(QStringLiteral("runStatusLabel"));

    auto *layout = new QVBoxLayout(this);
    auto *controls = new QHBoxLayout;
    controls->addWidget(new QLabel(tr("Variable:"), this));
    controls->addWidget(m_variableCombo, 1);
    controls->addWidget(new QLabel(tr("Run:"), this));
    controls->addWidget(m_runCombo, 1);
    layout->addLayout(controls);
    layout->addWidget(m_runStatusLabel);

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(m_datasetsList);
    auto *table = new QTableView(m_tabs);
    table->setObjectName(QStringLiteral("resultsTable"));
    table->setModel(m_tableModel);
    table->setSelectionBehavior(QAbstractItemView::SelectItems);
    table->setAlternatingRowColors(true);
    table->horizontalHeader()->setStretchLastSection(true);
    m_tabs->addTab(m_chart, tr("Chart"));
    m_tabs->addTab(table, tr("Data"));
    splitter->addWidget(m_tabs);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({180, 600});
    layout->addWidget(splitter, 1);

    connect(m_controller, &SimulationController::namedRunsChanged, this, &SimulationResultsWidget::refreshRuns);
    connect(m_controller, &SimulationController::workbenchVariableChanged, this, &SimulationResultsWidget::refreshWorkbench);
    connect(m_runCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        if (!m_refreshing)
            refreshWorkbench();
    });
    connect(m_variableCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        if (!m_refreshing)
            m_controller->setWorkbenchVariableName(m_variableCombo->currentData().toString());
    });
    connect(m_datasetsList, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
        if (!m_refreshing)
            m_controller->setRunLoaded(item->data(Qt::UserRole).toULongLong(), item->checkState() == Qt::Checked);
    });
    refreshRuns();
}

void SimulationResultsWidget::showGraph()
{
    m_tabs->setCurrentWidget(m_chart);
}

void SimulationResultsWidget::showTable()
{
    m_tabs->setCurrentIndex(1);
}

void SimulationResultsWidget::refreshRuns()
{
    if (m_refreshing)
        return;
    m_refreshing = true;
    const RunId selected = m_runCombo->currentData().toULongLong();
    m_runCombo->clear();
    m_datasetsList->clear();
    int selectedIndex = -1;
    int firstLoadedIndex = -1;
    bool hasNewRun = false;
    for (const NamedRun &run : m_controller->runs()) {
        hasNewRun = hasNewRun || !m_knownRuns.contains(run.id());
        m_runCombo->addItem(run.name(), run.id());
        if (run.id() == selected)
            selectedIndex = m_runCombo->count() - 1;
        if (firstLoadedIndex < 0 && m_controller->isRunLoaded(run.id()))
            firstLoadedIndex = m_runCombo->count() - 1;
        auto *item = new QListWidgetItem(run.name() + (m_controller->isOutdated(run) ? tr(" (older model)") : QString()), m_datasetsList);
        item->setData(Qt::UserRole, run.id());
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(m_controller->isRunLoaded(run.id()) ? Qt::Checked : Qt::Unchecked);
    }
    m_runCombo->setCurrentIndex(!hasNewRun && selectedIndex >= 0 ? selectedIndex : firstLoadedIndex);
    m_runStatusLabel->clear();
    m_knownRuns.clear();
    for (const NamedRun &run : m_controller->runs())
        m_knownRuns.insert(run.id());
    m_refreshing = false;
    refreshWorkbench();
}

void SimulationResultsWidget::refreshWorkbench()
{
    if (m_refreshing)
        return;
    m_refreshing = true;
    const std::optional<NamedRun> run = currentRun();
    m_variableCombo->clear();
    if (!run) {
        m_tableModel->clear();
        m_chart->setRuns({}, {});
        m_runStatusLabel->setText(tr("No completed runs"));
        m_refreshing = false;
        return;
    }

    int selectedIndex = -1;
    for (const RunVariableInfo &variable : run->variables()) {
        m_variableCombo->addItem(variable.displayName(), variable.nameKey());
        m_variableCombo->setItemData(m_variableCombo->count() - 1, static_cast<int>(variable.kind()), Qt::UserRole + 1);
        if (variable.nameKey() == m_controller->workbenchNameKey())
            selectedIndex = m_variableCombo->count() - 1;
        if (selectedIndex < 0 && variable.kind() == qds::core::ElementKind::Stock)
            selectedIndex = m_variableCombo->count() - 1;
    }
    if (selectedIndex < 0 && m_variableCombo->count() > 0)
        selectedIndex = 0;
    m_variableCombo->setCurrentIndex(selectedIndex);
    const QString nameKey = m_variableCombo->currentData().toString();
    if (selectedIndex >= 0 && m_controller->workbenchNameKey() != nameKey)
        m_controller->setWorkbenchVariableName(nameKey);
    if (selectedIndex >= 0)
        m_tableModel->setRun(*run, nameKey);
    else
        m_tableModel->clear();
    m_runStatusLabel->setText(tr("%1 — %2 samples%3")
        .arg(run->name()).arg(run->run()->times().size())
        .arg(m_controller->isOutdated(*run) ? tr(", older model") : QString()));
    QVector<NamedRun> loadedRuns;
    for (const NamedRun &candidate : m_controller->runs()) {
        if (m_controller->isRunLoaded(candidate.id()))
            loadedRuns.push_back(candidate);
    }
    m_chart->setRuns(loadedRuns, nameKey);
    m_refreshing = false;
}

std::optional<NamedRun> SimulationResultsWidget::currentRun() const
{
    const RunId id = m_runCombo->currentData().toULongLong();
    const auto &runs = m_controller->runs();
    const auto found = std::find_if(runs.begin(), runs.end(), [id](const NamedRun &run) {
        return run.id() == id;
    });
    return found == runs.end() ? std::nullopt : std::optional<NamedRun>(*found);
}

