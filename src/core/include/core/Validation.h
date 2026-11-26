#pragma once

#include <core/Document.h>
#include <core/formula/Formula.h>

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
    std::string code;
    SourceSpan span;
};

std::vector<ModelIssue> validateDocument(const Document &document);

} // namespace qds::core
