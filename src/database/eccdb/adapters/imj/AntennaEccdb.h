// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2026 Beijing Institute of Open Source Chip
#pragma once

#include <any>
#include <map>
#include <string>

namespace eccdb {
class DatabaseContext;
}

namespace imj {
struct NativeAntennaSummary;

// Runs antenna checking directly over EccDB's canonical net/pin/wire/via
// geometry. It deliberately never creates an iDB shadow design.
[[nodiscard]] NativeAntennaSummary checkAntennaEccdb(eccdb::DatabaseContext& context,
                                                      std::map<std::string, std::any>& config);
}  // namespace imj
