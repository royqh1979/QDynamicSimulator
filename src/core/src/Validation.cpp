#include <qds/core/Validation.h>
#include <qds/core/Analysis.h>

namespace qds::core {

std::vector<ModelIssue> validateDocument(const Document &document)
{
    return analyzeDocument(document).issues;
}

} // namespace qds::core
