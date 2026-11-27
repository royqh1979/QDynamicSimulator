#include "SimulationTableModel.h"

SimulationTableModel::SimulationTableModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

void SimulationTableModel::setRun(const NamedRun &run, const QString &nameKey)
{
    beginResetModel();
    m_run = run.run();
    m_variable.reset();
    for (const RunVariableInfo &variable : run.variables()) {
        if (variable.nameKey() == nameKey) {
            m_variable = variable;
            break;
        }
    }
    endResetModel();
}

void SimulationTableModel::clear()
{
    beginResetModel();
    m_run.reset();
    m_variable.reset();
    endResetModel();
}

int SimulationTableModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid() || !m_run || !m_variable || !m_run->series(m_variable->id()))
        return 0;
    return m_run->times().size();
}

int SimulationTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() || !m_run || !m_variable ? 0 : 2;
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
        return QString::number(m_run->times()[index.row()], 'g', 15);
    const QVector<double> *series = m_run->series(m_variable->id());
    if (!series || index.row() >= series->size())
        return {};
    return QString::number((*series)[index.row()], 'g', 15);
}

QVariant SimulationTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole)
        return QAbstractTableModel::headerData(section, orientation, role);
    if (orientation == Qt::Vertical)
        return section + 1;
    if (section == 0)
        return tr("Time");
    if (section == 1 && m_variable) {
        return m_variable->unit().isEmpty()
            ? m_variable->displayName()
            : QStringLiteral("%1 [%2]").arg(m_variable->displayName(), m_variable->unit());
    }
    return {};
}

