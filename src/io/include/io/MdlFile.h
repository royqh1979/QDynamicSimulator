#pragma once

#include <core/Validation.h>

class QString;

namespace qds::io {

class MdlFile
{
public:
    static bool importFile(const QString &fileName, core::Document &document,
        QString *errorMessage = nullptr, QVector<core::ModelIssue> *warnings = nullptr);
    static bool importText(const QString &text, core::Document &document,
        QString *errorMessage = nullptr, QVector<core::ModelIssue> *warnings = nullptr);
};

} // namespace qds::io
