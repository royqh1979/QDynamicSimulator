#include <core/Validation.h>
#include <core/formula/Analysis.h>

namespace qds::core {

QVector<ModelIssue> validateDocument(const Document &document)
{
    return analyzeDocument(document).issues;
}

} // namespace qds::core
