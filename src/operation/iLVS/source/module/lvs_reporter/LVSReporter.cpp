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
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "LVSReporter.hpp"

#include "LVSHeader.hpp"
#include "Logger.hpp"
#include "Utility.hpp"

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

// function

void LVSReporter::report()
{
  Monitor monitor;
  LVSLOG.info(Loc::current(), "Starting...");

  LRModel lr_model = initLRModel();
  std::vector<fort::char_table> summary_table_list = getSummaryTableList();
  std::vector<Violation> violation_list = getViolationList();
  outputRPT(lr_model, summary_table_list, violation_list);
  outputJson(lr_model, violation_list);
  printSummary(summary_table_list);

  LVSLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

// private

LVSReporter* LVSReporter::_lr_instance = nullptr;

LRModel LVSReporter::initLRModel()
{
  LRModel lr_model;
  lr_model.set_rpt_file_path(LVSUTIL.getString(LVSDM.getConfig().lr_temp_directory_path, "ilvs.rpt"));
  lr_model.set_json_file_path(LVSUTIL.getString(LVSDM.getConfig().lr_temp_directory_path, "ilvs.json"));
  return lr_model;
}

std::vector<fort::char_table> LVSReporter::getSummaryTableList()
{
  Summary& summary = LVSDM.getDatabase().get_summary();

  fort::char_table entity_summary_table;
  {
    entity_summary_table.set_cell_text_align(fort::text_align::right);
    entity_summary_table << fort::header << "Entity"
                         << "NETLIST"
                         << "DEF"
                         << "Difference" << fort::endr;
    for (LVSEntitySummaryRow& row : getEntitySummaryRowList(summary)) {
      entity_summary_table << row.get_entity() << row.get_netlist_num() << row.get_def_num() << row.get_difference_num() << fort::endr;
    }
  }

  fort::char_table connectivity_summary_table;
  {
    connectivity_summary_table.set_cell_text_align(fort::text_align::right);
    connectivity_summary_table << fort::header << "Connectivity"
                               << "Type"
                               << "Count" << fort::endr;
    std::string previous_connectivity;
    for (LVSConnectivitySummaryRow& row : getConnectivitySummaryRowList(summary)) {
      connectivity_summary_table << (row.get_connectivity() == previous_connectivity ? "" : row.get_connectivity()) << row.get_type()
                                 << row.get_count() << fort::endr;
      previous_connectivity = row.get_connectivity();
    }
  }

  std::vector<fort::char_table> summary_table_list;
  summary_table_list.push_back(std::move(entity_summary_table));
  summary_table_list.push_back(std::move(connectivity_summary_table));
  return summary_table_list;
}

std::vector<LVSEntitySummaryRow> LVSReporter::getEntitySummaryRowList(const Summary& summary)
{
  const ECSummary& ec_summary = summary.ec_summary;
  LVSEntitySummaryRow io_row;
  io_row.set_entity("IO(without pg)");
  io_row.set_netlist_num(ec_summary.netlist_io_num);
  io_row.set_def_num(ec_summary.def_io_num);
  io_row.set_difference_num(ec_summary.io_difference_num);
  LVSEntitySummaryRow instance_row;
  instance_row.set_entity("Instance");
  instance_row.set_netlist_num(ec_summary.netlist_instance_num);
  instance_row.set_def_num(ec_summary.def_instance_num);
  instance_row.set_difference_num(ec_summary.instance_difference_num);
  LVSEntitySummaryRow net_row;
  net_row.set_entity("Net");
  net_row.set_netlist_num(ec_summary.netlist_net_num);
  net_row.set_def_num(ec_summary.def_net_num);
  net_row.set_difference_num(ec_summary.net_difference_num);
  return {std::move(io_row), std::move(instance_row), std::move(net_row)};
}

std::vector<LVSConnectivitySummaryRow> LVSReporter::getConnectivitySummaryRowList(const Summary& summary)
{
  const RCSummary& rc_summary = summary.rc_summary;
  const PCSummary& pc_summary = summary.pc_summary;
  LVSConnectivitySummaryRow routing_open_row;
  routing_open_row.set_connectivity("Routing");
  routing_open_row.set_type("Open Net");
  routing_open_row.set_count(rc_summary.open_net_num);
  LVSConnectivitySummaryRow routing_short_row;
  routing_short_row.set_connectivity("Routing");
  routing_short_row.set_type("Short Net");
  routing_short_row.set_count(rc_summary.short_net_num);
  LVSConnectivitySummaryRow power_vdd_row;
  power_vdd_row.set_connectivity("Power");
  power_vdd_row.set_type("Open VDD");
  power_vdd_row.set_count(pc_summary.open_vdd_num);
  LVSConnectivitySummaryRow power_vss_row;
  power_vss_row.set_connectivity("Power");
  power_vss_row.set_type("Open VSS");
  power_vss_row.set_count(pc_summary.open_vss_num);
  return {std::move(routing_open_row), std::move(routing_short_row), std::move(power_vdd_row), std::move(power_vss_row)};
}

std::vector<Violation> LVSReporter::getViolationList()
{
  Summary& summary = LVSDM.getDatabase().get_summary();
  std::vector<Violation> violation_list;
  violation_list.reserve(summary.ec_summary.violation_list.size() + summary.rc_summary.violation_list.size()
                         + summary.pc_summary.violation_list.size());
  violation_list.insert(violation_list.end(), summary.ec_summary.violation_list.begin(), summary.ec_summary.violation_list.end());
  violation_list.insert(violation_list.end(), summary.rc_summary.violation_list.begin(), summary.rc_summary.violation_list.end());
  violation_list.insert(violation_list.end(), summary.pc_summary.violation_list.begin(), summary.pc_summary.violation_list.end());
  return violation_list;
}

void LVSReporter::outputRPT(const LRModel& lr_model, const std::vector<fort::char_table>& summary_table_list,
                            const std::vector<Violation>& violation_list)
{
  std::ofstream* rpt_file = LVSUTIL.getOutputFileStream(lr_model.get_rpt_file_path());
  DefData& def_data = LVSDM.getDatabase().get_def_data();
  std::map<int32_t, std::vector<Shape>>& component_shape_map = def_data.get_physical_graph().get_component_shape_map();

  *rpt_file << "iLVS Report\n\n";
  for (const fort::char_table& summary_table : summary_table_list) {
    *rpt_file << summary_table.to_string() << "\n";
  }
  *rpt_file << "[Violation Details]\n";
  if (violation_list.empty()) {
    *rpt_file << "None\n";
  }
  for (int32_t violation_idx = 0; violation_idx < static_cast<int32_t>(violation_list.size()); violation_idx++) {
    const Violation& violation = violation_list[violation_idx];
    std::string violation_type_name = GetViolationTypeName()(violation.get_violation_type());
    fort::char_table violation_table;
    {
      violation_table.set_cell_text_align(fort::text_align::right);
      violation_table << fort::header << "Violation"
                      << "Value" << fort::endr;
      violation_table << "Type" << violation_type_name << fort::endr;
      violation_table << "Net" << (violation.get_net_name().empty() ? "-" : violation.get_net_name()) << fort::endr;
      if (!violation.get_instance_name().empty()) {
        violation_table << "Instance" << violation.get_instance_name() << fort::endr;
      }
      if (!violation.get_driver_terminal_name().empty()) {
        violation_table << "Driver" << violation.get_driver_terminal_name() << fort::endr;
      }
      if (!violation.get_related_net_name_list().empty()) {
        violation_table << "Net Count" << violation.get_related_net_name_list().size() << fort::endr;
      }
      violation_table << "Component Count" << violation.get_component_id_list().size() << fort::endr;
      violation_table << "Terminal Count" << violation.get_terminal_name_list().size() << fort::endr;
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
    for (const Shape& shape : violation.get_shape_list()) {
      coordinate_table << "-" << shape.get_layer_idx() << shape.get_ll_x() << shape.get_ll_y() << shape.get_ur_x() << shape.get_ur_y()
                       << fort::endr;
      has_coordinate = true;
    }
    for (int32_t component_id : violation.get_component_id_list()) {
      auto shape_iter = component_shape_map.find(component_id);
      if (shape_iter == component_shape_map.end()) {
        continue;
      }
      for (Shape& shape : shape_iter->second) {
        coordinate_table << component_id << shape.get_layer_idx() << shape.get_ll_x() << shape.get_ll_y() << shape.get_ur_x()
                         << shape.get_ur_y() << fort::endr;
        has_coordinate = true;
      }
    }

    *rpt_file << "\n[" << violation_idx + 1 << "] " << violation_type_name << "\n";
    *rpt_file << violation_table.to_string();
    *rpt_file << "Components: " << getJoinedString(violation.get_component_id_list()) << "\n";
    if (!violation.get_related_net_name_list().empty()) {
      *rpt_file << "Nets: " << getJoinedString(violation.get_related_net_name_list()) << "\n";
    }
    *rpt_file << "Terminals: " << getJoinedString(violation.get_terminal_name_list()) << "\n";
    *rpt_file << "Coordinates (DBU)\n";
    if (has_coordinate) {
      *rpt_file << coordinate_table.to_string();
    } else {
      *rpt_file << "None\n";
    }
  }
  LVSUTIL.closeFileStream(rpt_file);
}

std::string LVSReporter::getJoinedString(const std::vector<int32_t>& value_list)
{
  if (value_list.empty()) {
    return "-";
  }
  std::ostringstream stream;
  for (int32_t value_idx = 0; value_idx < static_cast<int32_t>(value_list.size()); value_idx++) {
    if (value_idx > 0) {
      stream << " ";
    }
    stream << value_list[value_idx];
  }
  return stream.str();
}

std::string LVSReporter::getJoinedString(const std::vector<std::string>& value_list)
{
  if (value_list.empty()) {
    return "-";
  }
  std::ostringstream stream;
  for (int32_t value_idx = 0; value_idx < static_cast<int32_t>(value_list.size()); value_idx++) {
    if (value_idx > 0) {
      stream << " ";
    }
    stream << value_list[value_idx];
  }
  return stream.str();
}

void LVSReporter::outputJson(const LRModel& lr_model, const std::vector<Violation>& violation_list)
{
  Summary& summary = LVSDM.getDatabase().get_summary();
  DefData& def_data = LVSDM.getDatabase().get_def_data();
  std::map<int32_t, std::vector<Shape>>& component_shape_map = def_data.get_physical_graph().get_component_shape_map();
  nlohmann::json json;

  json["entity"] = nlohmann::json::array();
  for (LVSEntitySummaryRow& row : getEntitySummaryRowList(summary)) {
    json["entity"].push_back({{"entity", row.get_entity()},
                               {"netlist", row.get_netlist_num()},
                               {"def", row.get_def_num()},
                               {"difference", row.get_difference_num()}});
  }

  json["connectivity"] = nlohmann::json::array();
  for (LVSConnectivitySummaryRow& row : getConnectivitySummaryRowList(summary)) {
    json["connectivity"].push_back(
        {{"connectivity", row.get_connectivity()}, {"type", row.get_type()}, {"count", row.get_count()}});
  }

  json["violations"] = nlohmann::json::array();
  for (const Violation& violation : violation_list) {
    nlohmann::json violation_json = {{"type", GetViolationTypeName()(violation.get_violation_type())},
                                     {"net", violation.get_net_name()},
                                     {"terminals", violation.get_terminal_name_list()},
                                     {"components", violation.get_component_id_list()}};
    if (!violation.get_instance_name().empty()) {
      violation_json["instance"] = violation.get_instance_name();
    }
    if (!violation.get_driver_terminal_name().empty()) {
      violation_json["driver"] = violation.get_driver_terminal_name();
    }
    if (!violation.get_related_net_name_list().empty()) {
      violation_json["nets"] = violation.get_related_net_name_list();
    }
    for (const Shape& shape : violation.get_shape_list()) {
      violation_json["shapes"].push_back(
          {{"layer", shape.get_layer_idx()}, {"rect", {shape.get_ll_x(), shape.get_ll_y(), shape.get_ur_x(), shape.get_ur_y()}}});
    }
    for (int32_t component_id : violation.get_component_id_list()) {
      auto shape_iter = component_shape_map.find(component_id);
      if (shape_iter == component_shape_map.end()) {
        continue;
      }
      for (Shape& shape : shape_iter->second) {
        violation_json["shapes"].push_back({{"component", component_id},
                                              {"layer", shape.get_layer_idx()},
                                              {"rect", {shape.get_ll_x(), shape.get_ll_y(), shape.get_ur_x(), shape.get_ur_y()}}});
      }
    }
    json["violations"].push_back(std::move(violation_json));
  }

  std::ofstream* json_file = LVSUTIL.getOutputFileStream(lr_model.get_json_file_path());
  *json_file << json.dump(2) << "\n";
  LVSUTIL.closeFileStream(json_file);
}

void LVSReporter::printSummary(const std::vector<fort::char_table>& summary_table_list)
{
  for (const fort::char_table& summary_table : summary_table_list) {
    LVSUTIL.printTableList({summary_table});
  }
}

}  // namespace ilvs
