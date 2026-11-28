#pragma once

#include <QString>

namespace qds::core {

QString normalizeIdentifier(const QString &name);
QString trimDisplayName(const QString &name);
bool isReservedIdentifier(const QString &nameKey);

} // namespace qds::core
