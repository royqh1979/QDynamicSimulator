#pragma once

#include <core/Document.h>

class QString;

namespace qds::io {

class XmileFile
{
public:
    static bool importFile(const QString &fileName, core::Document &document, QString *errorMessage = nullptr);
    static bool exportFile(const QString &fileName, const core::Document &document, QString *errorMessage = nullptr);
};

} // namespace qds::io
