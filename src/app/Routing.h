#pragma once

#include <core/Document.h>

#include <QHash>
#include <QRectF>
#include <QVector>

class ObstacleIndex
{
public:
    struct Entry {
        qds::core::EntityId id;
        QRectF rect;
    };

    void clear();
    void upsert(qds::core::EntityId id, const QRectF &rect);
    void remove(qds::core::EntityId id);
    QVector<Entry> query(const QRectF &rect) const;

private:
    static constexpr int cellSize = 128;
    static qint64 cellKey(int x, int y);
    static QVector<qint64> cells(const QRectF &rect);

    QHash<qds::core::EntityId, QRectF> m_rectangles;
    QHash<qint64, QVector<qds::core::EntityId>> m_cells;
};

class OrthogonalRouter
{
public:
    qds::core::Route route(
        const QPointF &start,
        const QPointF &end,
        qds::core::EntityId from,
        qds::core::EntityId to,
        const ObstacleIndex &obstacles) const;
};
