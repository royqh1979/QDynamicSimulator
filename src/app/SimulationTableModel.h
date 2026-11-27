#pragma once

#include "SimulationController.h"

#include <QAbstractTableModel>

class SimulationTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit SimulationTableModel(QObject *parent = nullptr);

    void setRuns(const QVector<NamedRun> &runs, const QString &nameKey);
    void clear();

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

private:
    struct Column {
        QString runName;
        std::shared_ptr<const qds::core::SimulationRun> run;
        const QVector<double> *values = nullptr;
    };

    QVector<double> m_times;
    QVector<Column> m_columns;
};
