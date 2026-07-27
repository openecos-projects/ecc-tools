// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "LVSReporter.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>
#include <vector>

#include "LVSHeader.hpp"

namespace ilvs {

// public

void LVSReporter::initInst()
{
  if (_lr_instance == nullptr) {
    _lr_instance = new LVSReporter();
  }
}

LVSReporter& LVSReporter::getInst()
{
  if (_lr_instance == nullptr) {
    LVSLOG.error(Loc::current(), "The instance not initialized!");
  }
  return *_lr_instance;
}

void LVSReporter::destroyInst()
{
  if (_lr_instance != nullptr) {
    delete _lr_instance;
    _lr_instance = nullptr;
  }
}

// private

LVSReporter* LVSReporter::_lr_instance = nullptr;

namespace {

template <typename T>
std::string getJoinedString(const std::vector<T>& value_list)
{
  if (value_list.empty()) {
    return "-";
  }
  std::ostringstream stream;
  for (size_t value_idx = 0; value_idx < value_list.size(); value_idx++) {
    if (value_idx > 0) {
      stream << " ";
    }
    stream << value_list[value_idx];
  }
  return stream.str();
}

struct LVSEntitySummaryRow
{
  std::string entity;
  uint64_t netlist_num = 0;
  uint64_t def_num = 0;
  uint64_t difference_num = 0;
};

std::vector<LVSEntitySummaryRow> getEntitySummaryRowList(const CheckResult& check_result)
{
  return {{"IO(without pg)", check_result.netlist_io_num, check_result.def_io_num,
           check_result.missing_io_num + check_result.unexpected_io_num},
          {"Instance", check_result.netlist_instance_num, check_result.def_instance_num,
           check_result.missing_instance_num + check_result.unexpected_instance_num},
          {"Net", check_result.netlist_net_num, check_result.def_net_num,
           check_result.missing_net_num + check_result.unexpected_net_num + check_result.net_pin_mismatch_num}};
}

struct LVSConnectivitySummaryRow
{
  std::string connectivity;
  std::string type;
  uint64_t count = 0;
};

std::vector<LVSConnectivitySummaryRow> getConnectivitySummaryRowList(const CheckResult& check_result)
{
  return {{"Routing", "Open Net", check_result.routing_open_net_num + check_result.routing_missing_driver_num},
          {"Routing", "Short Net", check_result.routing_short_component_num},
          {"Power", "Open VDD", check_result.disconnected_power_instance_pin_num},
          {"Power", "Open VSS", check_result.disconnected_ground_instance_pin_num}};
}

}  // namespace

#if 1  // report

std::vector<fort::char_table> LVSReporter::getSummaryTableList(const CheckResult& check_result, const Netlist& /* netlist */,
                                                                const Netlist& /* def */)
{
  fort::char_table netlist_summary_table;
  {
    netlist_summary_table.set_cell_text_align(fort::text_align::right);
    netlist_summary_table << fort::header << "Entity"
                          << "NETLIST"
                          << "DEF"
                          << "Difference" << fort::endr;
    for (const LVSEntitySummaryRow& row : getEntitySummaryRowList(check_result)) {
      netlist_summary_table << row.entity << row.netlist_num << row.def_num << row.difference_num << fort::endr;
    }
  }

  fort::char_table connectivity_table;
  {
    connectivity_table.set_cell_text_align(fort::text_align::right);
    connectivity_table << fort::header << "Connectivity"
                       << "Type"
                       << "Count" << fort::endr;
    std::string previous_connectivity;
    for (const LVSConnectivitySummaryRow& row : getConnectivitySummaryRowList(check_result)) {
      connectivity_table << (row.connectivity == previous_connectivity ? "" : row.connectivity) << row.type << row.count
                         << fort::endr;
      previous_connectivity = row.connectivity;
    }
  }

  std::vector<fort::char_table> summary_table_list;
  summary_table_list.push_back(std::move(netlist_summary_table));
  summary_table_list.push_back(std::move(connectivity_table));
  return summary_table_list;
}

void LVSReporter::report(const CheckResult& check_result, const Netlist& netlist, const Netlist& def,
                         const std::string& report_directory_path)
{
  Monitor monitor;
  LVSLOG.info(Loc::current(), "Starting...");

  std::filesystem::create_directories(report_directory_path);
  std::ofstream rpt_file(std::filesystem::path(report_directory_path) / "ilvs.rpt");

  rpt_file << "iLVS Report\n\n";
  for (const fort::char_table& summary_table : getSummaryTableList(check_result, netlist, def)) {
    rpt_file << summary_table.to_string() << "\n";
  }

  rpt_file << "[Violation Details]\n";
  if (check_result.violation_list.empty()) {
    rpt_file << "None\n";
  }
  for (size_t violation_idx = 0; violation_idx < check_result.violation_list.size(); violation_idx++) {
    const Violation& violation = check_result.violation_list[violation_idx];
    fort::char_table violation_table;
    {
      violation_table.set_cell_text_align(fort::text_align::right);
      violation_table << fort::header << "Violation"
                      << "Value" << fort::endr;
      violation_table << "Type" << violation.type << fort::endr;
      violation_table << "Net" << (violation.net_name.empty() ? "-" : violation.net_name) << fort::endr;
      if (!violation.instance_name.empty()) {
        violation_table << "Instance" << violation.instance_name << fort::endr;
      }
      if (!violation.driver_terminal_name.empty()) {
        violation_table << "Driver" << violation.driver_terminal_name << fort::endr;
      }
      if (!violation.related_net_name_list.empty()) {
        violation_table << "Net Count" << violation.related_net_name_list.size() << fort::endr;
      }
      violation_table << "Component Count" << violation.component_id_list.size() << fort::endr;
      violation_table << "Terminal Count" << violation.terminal_list.size() << fort::endr;
    }

    fort::char_table coordinate_table;
    coordinate_table.set_cell_text_align(fort::text_align::right);
    coordinate_table << fort::header << "Component"
                     << "Layer"
                     << "LLX"
                     << "LLY"
                     << "URX"
                     << "URY" << fort::endr;
    bool has_coordinate = false;
    for (const ShapeLocation& shape : violation.shape_list) {
      coordinate_table << "-" << shape.layer_id << shape.ll_x << shape.ll_y << shape.ur_x << shape.ur_y << fort::endr;
      has_coordinate = true;
    }
    for (uint64_t component_id : violation.component_id_list) {
      auto shape_iter = def.physical_graph.component_shape_map.find(component_id);
      if (shape_iter == def.physical_graph.component_shape_map.end()) continue;
      for (const ShapeLocation& shape : shape_iter->second) {
        coordinate_table << component_id << shape.layer_id << shape.ll_x << shape.ll_y << shape.ur_x << shape.ur_y << fort::endr;
        has_coordinate = true;
      }
    }

    rpt_file << "\n[" << violation_idx + 1 << "] " << violation.type << "\n";
    rpt_file << violation_table.to_string();
    rpt_file << "Components: " << getJoinedString(violation.component_id_list) << "\n";
    if (!violation.related_net_name_list.empty()) {
      rpt_file << "Nets: " << getJoinedString(violation.related_net_name_list) << "\n";
    }
    rpt_file << "Terminals: " << getJoinedString(violation.terminal_list) << "\n";
    rpt_file << "Coordinates (DBU)\n";
    if (has_coordinate) {
      rpt_file << coordinate_table.to_string();
    } else {
      rpt_file << "None\n";
    }
  }

  nlohmann::json json;
  json["entity"] = nlohmann::json::array();
  for (const LVSEntitySummaryRow& row : getEntitySummaryRowList(check_result)) {
    json["entity"].push_back({{"entity", row.entity},
                               {"netlist", row.netlist_num},
                               {"def", row.def_num},
                               {"difference", row.difference_num}});
  }

  json["connectivity"] = nlohmann::json::array();
  for (const LVSConnectivitySummaryRow& row : getConnectivitySummaryRowList(check_result)) {
    json["connectivity"].push_back(
        {{"connectivity", row.connectivity}, {"type", row.type}, {"count", row.count}});
  }

  json["violations"] = nlohmann::json::array();
  for (const Violation& violation : check_result.violation_list) {
    nlohmann::json violation_json = {{"type", violation.type}, {"net", violation.net_name}, {"terminals", violation.terminal_list},
                                     {"components", violation.component_id_list}};
    if (!violation.instance_name.empty()) {
      violation_json["instance"] = violation.instance_name;
    }
    if (!violation.driver_terminal_name.empty()) {
      violation_json["driver"] = violation.driver_terminal_name;
    }
    if (!violation.related_net_name_list.empty()) {
      violation_json["nets"] = violation.related_net_name_list;
    }
    for (const ShapeLocation& shape : violation.shape_list) {
      violation_json["shapes"].push_back({{"layer", shape.layer_id}, {"rect", {shape.ll_x, shape.ll_y, shape.ur_x, shape.ur_y}}});
    }
    for (uint64_t component_id : violation.component_id_list) {
      auto shape_iter = def.physical_graph.component_shape_map.find(component_id);
      if (shape_iter == def.physical_graph.component_shape_map.end()) continue;
      for (const ShapeLocation& shape : shape_iter->second) {
        violation_json["shapes"].push_back({{"component", component_id}, {"layer", shape.layer_id},
                                             {"rect", {shape.ll_x, shape.ll_y, shape.ur_x, shape.ur_y}}});
      }
    }
    json["violations"].push_back(std::move(violation_json));
  }
  std::ofstream json_file(std::filesystem::path(report_directory_path) / "ilvs.json");
  json_file << json.dump(2) << "\n";
  LVSLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

#endif

}  // namespace ilvs
