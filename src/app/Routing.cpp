#include "Routing.h"

#include <QPainterPath>
#include <QtMath>

#include <algorithm>
#include <limits>

namespace {

bool pathIntersects(const QVector<QPointF> &points, const QRectF &rect)
{
    const QLineF edges[] = {
        QLineF(rect.topLeft(), rect.topRight()), QLineF(rect.topRight(), rect.bottomRight()),
        QLineF(rect.bottomRight(), rect.bottomLeft()), QLineF(rect.bottomLeft(), rect.topLeft())};
    for (int index = 1; index < points.size(); ++index) {
        const QLineF segment(points[index - 1], points[index]);
        if (rect.contains(segment.p1()) || rect.contains(segment.p2()))
            return true;
        for (const QLineF &edge : edges) {
            QPointF intersection;
            if (segment.intersect(edge, &intersection) == QLineF::BoundedIntersection)
                return true;
        }
    }
    return false;
}

} // namespace

qint64 ObstacleIndex::cellKey(int x, int y)
{
    return (static_cast<qint64>(x) << 32) ^ static_cast<quint32>(y);
}

QVector<qint64> ObstacleIndex::cells(const QRectF &rect)
{
    QVector<qint64> result;
    const int left = qFloor(rect.left() / cellSize);
    const int right = qFloor(rect.right() / cellSize);
    const int top = qFloor(rect.top() / cellSize);
    const int bottom = qFloor(rect.bottom() / cellSize);
    for (int x = left; x <= right; ++x) {
        for (int y = top; y <= bottom; ++y)
            result.append(cellKey(x, y));
    }
    return result;
}

void ObstacleIndex::clear()
{
    m_rectangles.clear();
    m_cells.clear();
}

void ObstacleIndex::upsert(qds::core::EntityId id, const QRectF &rect)
{
    remove(id);
    m_rectangles.insert(id, rect);
    for (qint64 cell : cells(rect))
        m_cells[cell].append(id);
}

void ObstacleIndex::remove(qds::core::EntityId id)
{
    const auto previous = m_rectangles.find(id);
    if (previous == m_rectangles.end())
        return;
    for (qint64 cell : cells(previous.value())) {
        auto cellItems = m_cells.find(cell);
        if (cellItems == m_cells.end())
            continue;
        cellItems->erase(std::remove(cellItems->begin(), cellItems->end(), id), cellItems->end());
        if (cellItems->isEmpty())
            m_cells.erase(cellItems);
    }
    m_rectangles.erase(previous);
}

QVector<ObstacleIndex::Entry> ObstacleIndex::query(const QRectF &rect) const
{
    QVector<Entry> result;
    QHash<qds::core::EntityId, bool> visited;
    for (qint64 cell : cells(rect)) {
        const auto cellItems = m_cells.constFind(cell);
        if (cellItems == m_cells.constEnd())
            continue;
        for (qds::core::EntityId id : cellItems.value()) {
            if (visited.contains(id))
                continue;
            visited.insert(id, true);
            const QRectF obstacle = m_rectangles.value(id);
            if (obstacle.intersects(rect))
                result.append({id, obstacle});
        }
    }
    return result;
}

qds::core::Route OrthogonalRouter::route(
    const QPointF &start,
    const QPointF &end,
    qds::core::EntityId from,
    qds::core::EntityId to,
    const ObstacleIndex &obstacles) const
{
    const QRectF queryBounds = QRectF(start, end).normalized().adjusted(-300.0, -300.0, 300.0, 300.0);
    const QVector<ObstacleIndex::Entry> entries = obstacles.query(queryBounds);
    double top = std::min(start.y(), end.y()) - 80.0;
    double bottom = std::max(start.y(), end.y()) + 80.0;
    double left = std::min(start.x(), end.x()) - 80.0;
    double right = std::max(start.x(), end.x()) + 80.0;
    for (const auto &entry : entries) {
        if (entry.id == from || entry.id == to)
            continue;
        top = std::min(top, entry.rect.top() - 30.0);
        bottom = std::max(bottom, entry.rect.bottom() + 30.0);
        left = std::min(left, entry.rect.left() - 30.0);
        right = std::max(right, entry.rect.right() + 30.0);
    }

    const double middleX = (start.x() + end.x()) / 2.0;
    const double middleY = (start.y() + end.y()) / 2.0;
    QVector<QVector<QPointF>> candidates {
        {start, QPointF(middleX, start.y()), QPointF(middleX, end.y()), end},
        {start, QPointF(start.x(), middleY), QPointF(end.x(), middleY), end},
        {start, QPointF(start.x(), top), QPointF(end.x(), top), end},
        {start, QPointF(start.x(), bottom), QPointF(end.x(), bottom), end},
        {start, QPointF(left, start.y()), QPointF(left, end.y()), end},
        {start, QPointF(right, start.y()), QPointF(right, end.y()), end}
    };

    double bestScore = std::numeric_limits<double>::max();
    QVector<QPointF> best = candidates.front();
    for (const auto &candidate : candidates) {
        double length = 0.0;
        for (int index = 1; index < candidate.size(); ++index)
            length += QLineF(candidate[index - 1], candidate[index]).length();
        double score = length / 10000.0;
        for (const auto &entry : entries) {
            if (entry.id != from && entry.id != to && pathIntersects(candidate, entry.rect))
                score += 100.0;
        }
        if (score < bestScore) {
            bestScore = score;
            best = candidate;
        }
    }

    qds::core::Route route;
    route.mode = qds::core::RouteMode::Orthogonal;
    for (int index = 1; index < best.size() - 1; ++index)
        route.controlPoints.push_back({best[index].x(), best[index].y()});
    return route;
}
