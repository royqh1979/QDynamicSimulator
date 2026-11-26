#pragma once

#include <core/Document.h>

#include <optional>
#include <QVector>

namespace qds::core {

struct ChangeRecord {
    EntityId id = 0;
    ElementKind kind = ElementKind::Stock;
    std::optional<ElementData> before;
    std::optional<ElementData> after;
};

struct ChangeSet {
    QVector<ChangeRecord> records;
};

struct ChangeNotification {
    QVector<EntityId> added;
    QVector<EntityId> updated;
    QVector<EntityId> removed;
    bool documentReset = false;
};

} // namespace qds::core
