#include "RunsCompareModel.h"

#include <QMap>

#include <algorithm>

namespace {

std::optional<double> initialValue(const NamedRun &run, const QString &nameKey)
{
    const auto variable = std::find_if(run.variables().begin(), run.variables().end(), [&nameKey](const RunVariableInfo &candidate) {
        return candidate.nameKey() == nameKey;
    });
    if (variable == run.variables().end())
        return std::nullopt;
    const QVector<double> *values = run.run()->series(variable->id());
    return values && !values->isEmpty() ? std::optional<double>(values->front()) : std::nullopt;
}

} // namespace

RunsCompareModel::RunsCompareModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

void RunsCompareModel::setRuns(const QVector<NamedRun> &loadedRuns)
{
    beginResetModel();
    m_rows.clear();
    m_olderRunName.clear();
    m_newerRunName.clear();
    if (loadedRuns.size() >= 2) {
        const NamedRun &newer = loadedRuns[0];
        const NamedRun &older = loadedRuns[1];
        m_newerRunName = newer.name();
        m_olderRunName = older.name();
        QMap<QString, QString> displayNames;
        QMap<QString, QString> units;
        for (const NamedRun *run : {&older, &newer}) {
            for (const RunVariableInfo &variable : run->variables()) {
                if (!variable.isConstant())
                    continue;
                displayNames.insert(variable.nameKey(), variable.displayName());
                units.insert(variable.nameKey(), variable.unit());
            }
        }
        for (const QString &nameKey : displayNames.keys()) {
            m_rows.push_back({nameKey, displayNames.value(nameKey), units.value(nameKey),
                initialValue(older, nameKey), initialValue(newer, nameKey)});
        }
        std::sort(m_rows.begin(), m_rows.end(), [](const Row &left, const Row &right) {
            const int order = QString::localeAwareCompare(left.displayName, right.displayName);
            return order != 0 ? order < 0 : left.nameKey < right.nameKey;
        });
    }
    endResetModel();
}

void RunsCompareModel::clear()
{
    setRuns({});
}

int RunsCompareModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

int RunsCompareModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() || m_rows.isEmpty() ? 0 : 5;
}

QVariant RunsCompareModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size() || index.column() >= columnCount())
        return {};
    const Row &row = m_rows[index.row()];
    if (role == Qt::TextAlignmentRole)
        return QVariant::fromValue(Qt::AlignRight | Qt::AlignVCenter);
    if (role != Qt::DisplayRole)
        return {};
    if (index.column() == 0)
        return row.displayName;
    if (index.column() == 1)
        return row.unit;
    const auto number = [](const std::optional<double> &value) -> QVariant {
        return value ? QString::number(*value, 'g', 15) : QVariant();
    };
    if (index.column() == 2)
        return number(row.olderValue);
    if (index.column() == 3)
        return number(row.newerValue);
    if (row.olderValue && row.newerValue)
        return QString::number(*row.newerValue - *row.olderValue, 'g', 15);
    return {};
}

QVariant RunsCompareModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole)
        return QAbstractTableModel::headerData(section, orientation, role);
    if (orientation == Qt::Vertical)
        return section + 1;
    if (section == 0)
        return tr("Variable");
    if (section == 1)
        return tr("Unit");
    if (section == 2)
        return m_olderRunName;
    if (section == 3)
        return m_newerRunName;
    if (section == 4)
        return tr("Difference");
    return {};
}
