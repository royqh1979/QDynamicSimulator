#pragma once

#include "SimulationController.h"

#include <QAbstractTableModel>

#include <optional>

class SimulationTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit SimulationTableModel(QObject *parent = nullptr);

    void setRun(const NamedRun &run, const QString &nameKey);
    void clear();

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

private:
    std::shared_ptr<const qds::core::SimulationRun> m_run;
    std::optional<RunVariableInfo> m_variable;
};
