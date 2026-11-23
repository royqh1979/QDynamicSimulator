#pragma once

#include <qds/core/Model.h>

#include <vector>

namespace qds::core {

struct ChangeSet {
    std::vector<EntityId> addedEntities;
    std::vector<EntityId> updatedEntities;
    std::vector<EntityId> removedEntities;
    std::vector<ConnectionId> addedConnections;
    std::vector<ConnectionId> updatedConnections;
    std::vector<ConnectionId> removedConnections;
    bool documentReset = false;
};

} // namespace qds::core
