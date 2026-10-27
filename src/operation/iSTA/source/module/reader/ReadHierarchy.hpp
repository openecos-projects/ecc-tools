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
class Network;
void linkHierarchy(Network& network, const idb::verilog_import::VerilogImportPlan& plan);
void readHierarchy(Network& network, idb::IdbLayout& layout, const std::string& file);
}  // namespace ista
