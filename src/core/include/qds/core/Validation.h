#pragma once

#include <qds/core/Document.h>

#include <string>
#include <vector>

namespace qds::core {

enum class IssueSeverity {
    Warning,
    Error
};

struct ModelIssue {
    EntityId entityId = 0;
    IssueSeverity severity = IssueSeverity::Error;
    std::string message;
};

std::vector<ModelIssue> validateDocument(const Document &document);

} // namespace qds::core
