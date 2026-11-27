#pragma once

#include "SimulationController.h"

#include <QAbstractTableModel>

#include <optional>

class RunsCompareModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit RunsCompareModel(QObject *parent = nullptr);

    void setRuns(const QVector<NamedRun> &loadedRuns);
    void clear();

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

private:
    struct Row {
        QString nameKey;
        QString displayName;
        QString unit;
        std::optional<double> olderValue;
        std::optional<double> newerValue;
    };

    QVector<Row> m_rows;
    QString m_olderRunName;
    QString m_newerRunName;
};
