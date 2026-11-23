#pragma once

#include <qds/core/Document.h>

class QString;

namespace qds::io {

bool saveXmile(const QString &fileName, const core::Document &document, QString *errorMessage = nullptr);
bool loadXmile(const QString &fileName, core::Document &document, QString *errorMessage = nullptr);

} // namespace qds::io
