#include <core/Validation.h>
#include <core/Analysis.h>

namespace qds::core {

std::vector<ModelIssue> validateDocument(const Document &document)
{
    return analyzeDocument(document).issues;
}

} // namespace qds::core
