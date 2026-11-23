#pragma once

#include <qds/core/Document.h>

#include <optional>
#include <vector>

namespace qds::core {

struct ChangeRecord {
    EntityId id = 0;
    ElementKind kind = ElementKind::Stock;
    std::optional<ElementData> before;
    std::optional<ElementData> after;
};

struct ChangeSet {
    std::vector<ChangeRecord> records;
};

struct ChangeNotification {
    std::vector<EntityId> added;
    std::vector<EntityId> updated;
    std::vector<EntityId> removed;
    bool documentReset = false;
};

} // namespace qds::core
