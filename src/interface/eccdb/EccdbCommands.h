// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2026 Peng Cheng Laboratory

#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace eccdb {
class Database;
}

namespace ecc::scripting {

[[nodiscard]] std::vector<std::string> supportedTools();

// Runs one complete native stage: initialize, execute, publish, and detach.
// `config` is a tool configuration JSON path when that tool has one. For
// map-configured tools, `options_json` augments that configuration and has
// precedence. iFP and iCTS retain ownership of their native config documents;
// they only accept explicit frontend inputs such as liberty_files, sdc_file,
// spef_file and vcd_file. LVS imports reference_verilog (and optional
// top_module) into a separate logical design sharing the loaded library.
// Unsupported or insufficient inputs raise an error
// instead of falling through to the legacy iDB path.
void runTool(eccdb::Database& database, const std::string& tool,
             const std::filesystem::path& config = {}, const std::string& options_json = "{}");

// Returns a compact JSON summary of the currently owned EccDB.  Counts are
// snapshots for scripting/UI diagnostics, not an alternate persistence model.
[[nodiscard]] std::string databaseInfo(const eccdb::Database& database);

}  // namespace ecc::scripting
