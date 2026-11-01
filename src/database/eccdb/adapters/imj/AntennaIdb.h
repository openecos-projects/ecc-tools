// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2026 Beijing Institute of Open Source Chip

#pragma once

#include <filesystem>

#include "MJInterface.hpp"

namespace imj {

// Runs the legacy iDB antenna checker once and projects its official
// -step_json result into the adapter-facing summary used by differential
// probes. The caller owns the surrounding iMJ session lifecycle.
[[nodiscard]] NativeAntennaSummary checkAntennaIdb(MJConfigMap config, const std::filesystem::path& step_json);

}  // namespace imj
