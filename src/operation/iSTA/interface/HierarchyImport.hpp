// SPDX-License-Identifier: MulanPSL-2.0
#pragma once
#include <string>
namespace idb {
class IdbLayout;
namespace verilog_import {
struct VerilogImportPlan;
}
}  // namespace idb
namespace ista {
class Database;
void bindHierarchy(Database& database, const idb::verilog_import::VerilogImportPlan& plan);
void readHierarchy(Database& database, idb::IdbLayout& layout, const std::string& file);
}  // namespace ista
