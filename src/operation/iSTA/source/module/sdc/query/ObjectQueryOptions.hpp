// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

#include <optional>
#include <string>

#include "query/ObjectQuery.hpp"
namespace ista::sdc {
class SdcTclCmd;
void addObjectQueryOptions(SdcTclCmd& command, bool hierarchical, bool exact, bool of_objects);
ObjectQueryOptions getObjectQueryOptions(SdcTclCmd& command);
std::optional<std::string> getObjectQueryError(const ObjectQueryOptions& options, bool has_patterns);
}  // namespace ista::sdc
