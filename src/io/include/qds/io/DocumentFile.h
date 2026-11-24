#pragma once

#include <qds/core/Document.h>

class QString;

namespace qds::io {

class DocumentFile
{
public:
    static bool save(const QString &fileName, const core::Document &document, QString *errorMessage = nullptr);
    static bool open(
        const QString &fileName,
        core::Document &document,
        QString *errorMessage = nullptr,
        bool *nativeFormat = nullptr);
};

} // namespace qds::io
