#pragma once

#include <core/Document.h>
#include <core/formula/Formula.h>

#include <QString>
#include <QVector>

namespace qds::core {

enum class IssueSeverity {
    Warning,
    Error
};

struct ModelIssue {
    EntityId entityId = 0;
    IssueSeverity severity = IssueSeverity::Error;
    QString message;
    QString code;
    SourceSpan span;
};

QVector<ModelIssue> validateDocument(const Document &document);

} // namespace qds::core
