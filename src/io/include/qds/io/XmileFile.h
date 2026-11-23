#pragma once

#include <qds/core/Model.h>

class QString;

namespace qds::io {

bool saveXmile(const QString &fileName, const core::Model &model, QString *errorMessage = nullptr);
bool loadXmile(const QString &fileName, core::Model &model, QString *errorMessage = nullptr);

} // namespace qds::io
