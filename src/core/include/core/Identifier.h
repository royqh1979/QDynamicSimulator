#pragma once

#include <string>

namespace qds::core {

std::string normalizeIdentifier(const std::string &name);
std::string trimDisplayName(const std::string &name);

} // namespace qds::core
