#include "SimulationTableModel.h"

#include <algorithm>

using qds::core::RunVariableInfo;

SimulationTableModel::SimulationTableModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

void SimulationTableModel::setRuns(const QVector<SimulationRun> &runs, const QString &nameKey)
{
    beginResetModel();
    m_times.clear();
    m_columns.clear();
    for (const SimulationRun &run : runs) {
        const auto variable = std::find_if(run.variables().begin(), run.variables().end(), [&nameKey](const RunVariableInfo &candidate) {
            return candidate.nameKey() == nameKey;
        });
        if (variable == run.variables().end())
            continue;
        const QVector<double> values = run.series(nameKey);
        if (values.isEmpty())
            continue;
        m_columns.push_back({run.name(), run.times(), values});
        m_times += run.times();
    }
    std::sort(m_times.begin(), m_times.end());
    m_times.erase(std::unique(m_times.begin(), m_times.end()), m_times.end());
    endResetModel();
}

void SimulationTableModel::clear()
{
    beginResetModel();
    m_times.clear();
    m_columns.clear();
    endResetModel();
}

int SimulationTableModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() || m_columns.isEmpty() ? 0 : m_times.size();
}

int SimulationTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() || m_columns.isEmpty() ? 0 : 1 + m_columns.size();
}

QVariant SimulationTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= rowCount() || index.column() >= columnCount())
        return {};
    if (role == Qt::TextAlignmentRole)
        return QVariant::fromValue(Qt::AlignRight | Qt::AlignVCenter);
    if (role != Qt::DisplayRole)
        return {};
    if (index.column() == 0)
        return QString::number(m_times[index.row()], 'g', 15);

    const Column &column = m_columns[index.column() - 1];
    const QVector<double> &runTimes = column.times;
    const auto found = std::lower_bound(runTimes.begin(), runTimes.end(), m_times[index.row()]);
    if (found == runTimes.end() || *found != m_times[index.row()])
        return {};
    const int row = static_cast<int>(found - runTimes.begin());
    if (row >= column.values.size())
        return {};
    return QString::number(column.values[row], 'g', 15);
}

QVariant SimulationTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole)
        return QAbstractTableModel::headerData(section, orientation, role);
    if (orientation == Qt::Vertical)
        return section + 1;
    if (section == 0)
        return tr("Time");
    if (section > 0 && section <= m_columns.size())
        return m_columns[section - 1].runName;
    return {};
}
