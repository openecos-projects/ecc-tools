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
  std::vector<const Violation*> violation_list = getViolationList();
  LVSLOG.info(Loc::current(), "Writing RPT...");
  outputRPT(lr_model, summary_table_list, violation_list);
  LVSLOG.info(Loc::current(), "Writing JSON...");
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
  fort::char_table entity_summary_table;
  {
    entity_summary_table.set_cell_text_align(fort::text_align::right);
    entity_summary_table << fort::header << "Entity"
                         << "NETLIST"
                         << "DEF"
                         << "Difference" << fort::endr;
    for (LVSEntitySummaryRow& row : getEntitySummaryRowList()) {
      entity_summary_table << row.get_entity() << row.get_netlist_num() << row.get_def_num() << row.get_difference_num() << fort::endr;
    }
  }

  fort::char_table connectivity_summary_table;
  {
    connectivity_summary_table.set_cell_text_align(fort::text_align::right);
    connectivity_summary_table << fort::header << "Connectivity"
                               << "Open"
                               << "Short"
                               << "Connected"
                               << "Total" << fort::endr;
    for (LVSConnectivitySummaryRow& row : getConnectivitySummaryRowList()) {
      connectivity_summary_table << row.get_connectivity() << getCountPercentageText(row.get_open_num(), row.get_total_num())
                                 << getCountPercentageText(row.get_short_num(), row.get_total_num())
                                 << getCountPercentageText(row.get_connected_num(), row.get_total_num()) << row.get_total_num() << fort::endr;
    }
  }

  std::vector<fort::char_table> summary_table_list;
  summary_table_list.push_back(std::move(entity_summary_table));
  summary_table_list.push_back(std::move(connectivity_summary_table));
  return summary_table_list;
}

std::vector<LVSEntitySummaryRow> LVSReporter::getEntitySummaryRowList()
{
  ECSummary& ec_summary = LVSDM.getDatabase().get_summary().ec_summary;
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

std::vector<LVSConnectivitySummaryRow> LVSReporter::getConnectivitySummaryRowList()
{
  RCSummary& rc_summary = LVSDM.getDatabase().get_summary().rc_summary;
  PhysicalGraph& physical_graph = LVSDM.getDatabase().get_def_data().get_physical_graph();
  int64_t routing_total_num = static_cast<int64_t>(LVSDM.getDatabase().get_def_data().get_net_map().size());
  int64_t routing_connected_num = routing_total_num - rc_summary.open_net_num - rc_summary.short_net_num;
  if (routing_connected_num < 0) {
    routing_connected_num = 0;
  }

  std::vector<LVSConnectivitySummaryRow> row_list;
  addConnectivitySummaryRow(row_list, "Routing", rc_summary.open_net_num, rc_summary.short_net_num, routing_connected_num, routing_total_num);

  std::map<std::string, std::string>& power_instance_pin_net_map = physical_graph.get_power_instance_pin_net_map();
  std::set<std::string> power_open_terminal_name_set = getPowerOpenTerminalNameSet(ConnectType::kPower);
  std::set<std::string> power_short_terminal_name_set = getPowerShortTerminalNameSet(ConnectType::kPower);
  int64_t power_open_num = 0;
  for (const std::string& terminal_name : power_open_terminal_name_set) {
    if (!LVSUTIL.exist(power_short_terminal_name_set, terminal_name)) {
      power_open_num++;
    }
  }
  int64_t power_total_num = static_cast<int64_t>(power_instance_pin_net_map.size());
  int64_t power_short_num = static_cast<int64_t>(power_short_terminal_name_set.size());
  int64_t power_connected_num = power_total_num - power_open_num - power_short_num;
  if (power_connected_num < 0) {
    power_connected_num = 0;
  }
  addConnectivitySummaryRow(row_list, "Power VDD", power_open_num, power_short_num, power_connected_num, power_total_num);

  std::map<std::string, std::string>& ground_instance_pin_net_map = physical_graph.get_ground_instance_pin_net_map();
  std::set<std::string> ground_open_terminal_name_set = getPowerOpenTerminalNameSet(ConnectType::kGround);
  std::set<std::string> ground_short_terminal_name_set = getPowerShortTerminalNameSet(ConnectType::kGround);
  int64_t ground_open_num = 0;
  for (const std::string& terminal_name : ground_open_terminal_name_set) {
    if (!LVSUTIL.exist(ground_short_terminal_name_set, terminal_name)) {
      ground_open_num++;
    }
  }
  int64_t ground_total_num = static_cast<int64_t>(ground_instance_pin_net_map.size());
  int64_t ground_short_num = static_cast<int64_t>(ground_short_terminal_name_set.size());
  int64_t ground_connected_num = ground_total_num - ground_open_num - ground_short_num;
  if (ground_connected_num < 0) {
    ground_connected_num = 0;
  }
  addConnectivitySummaryRow(row_list, "Power VSS", ground_open_num, ground_short_num, ground_connected_num, ground_total_num);
  return row_list;
}

void LVSReporter::addConnectivitySummaryRow(std::vector<LVSConnectivitySummaryRow>& row_list, const std::string& connectivity, int64_t open_num,
                                            int64_t short_num, int64_t connected_num, int64_t total_num)
{
  LVSConnectivitySummaryRow row;
  row.set_connectivity(connectivity);
  row.set_open_num(open_num);
  row.set_short_num(short_num);
  row.set_connected_num(connected_num);
  row.set_total_num(total_num);
  row_list.push_back(std::move(row));
}

std::string LVSReporter::getCountPercentageText(int64_t count, int64_t total_count)
{
  std::ostringstream stream;
  stream << count << " (" << std::fixed << std::setprecision(2) << getPercentage(count, total_count) << "%)";
  return stream.str();
}

double LVSReporter::getPercentage(int64_t count, int64_t total_count)
{
  if (total_count == 0) {
    return 0.0;
  }
  double percentage = static_cast<double>(count) * 100.0 / static_cast<double>(total_count);
  return std::round(percentage * 100.0) / 100.0;
}

std::set<std::string> LVSReporter::getPowerOpenTerminalNameSet(ConnectType connect_type)
{
  Summary& summary = LVSDM.getDatabase().get_summary();
  PhysicalGraph& physical_graph = LVSDM.getDatabase().get_def_data().get_physical_graph();
  std::map<std::string, std::string>& instance_pin_net_map
      = connect_type == ConnectType::kPower ? physical_graph.get_power_instance_pin_net_map() : physical_graph.get_ground_instance_pin_net_map();
  ViolationType violation_type = connect_type == ConnectType::kPower ? ViolationType::kPowerOpenVDD : ViolationType::kPowerOpenVSS;
  std::set<std::string> terminal_name_set;
  for (Violation& violation : summary.pc_summary.violation_list) {
    if (violation.get_violation_type() != violation_type) {
      continue;
    }
    for (const std::string& terminal_name : violation.get_terminal_name_list()) {
      if (LVSUTIL.exist(instance_pin_net_map, terminal_name)) {
        terminal_name_set.insert(terminal_name);
      }
    }
  }
  return terminal_name_set;
}

std::set<std::string> LVSReporter::getPowerShortTerminalNameSet(ConnectType connect_type)
{
  PhysicalGraph& physical_graph = LVSDM.getDatabase().get_def_data().get_physical_graph();
  std::set<std::string>& power_net_name_set = physical_graph.get_power_net_name_set();
  std::set<std::string>& ground_net_name_set = physical_graph.get_ground_net_name_set();
  std::set<int32_t> power_ground_short_component_id_set;
  for (auto& [component_id, net_name_list] : physical_graph.get_component_net_name_map()) {
    bool has_power_net = false;
    bool has_ground_net = false;
    for (const std::string& net_name : net_name_list) {
      if (LVSUTIL.exist(power_net_name_set, net_name)) {
        has_power_net = true;
      }
      if (LVSUTIL.exist(ground_net_name_set, net_name)) {
        has_ground_net = true;
      }
    }
    if (has_power_net && has_ground_net) {
      power_ground_short_component_id_set.insert(component_id);
    }
  }

  std::map<std::string, std::string>& instance_pin_net_map
      = connect_type == ConnectType::kPower ? physical_graph.get_power_instance_pin_net_map() : physical_graph.get_ground_instance_pin_net_map();
  std::map<std::string, int32_t>& terminal_component_map = physical_graph.get_terminal_component_map();
  std::set<std::string> terminal_name_set;
  for (auto& [terminal_name, net_name] : instance_pin_net_map) {
    (void) net_name;
    std::map<std::string, int32_t>::iterator component_iter = terminal_component_map.find(terminal_name);
    if (component_iter != terminal_component_map.end() && LVSUTIL.exist(power_ground_short_component_id_set, component_iter->second)) {
      terminal_name_set.insert(terminal_name);
    }
  }
  return terminal_name_set;
}

std::vector<const Violation*> LVSReporter::getViolationList()
{
  Summary& summary = LVSDM.getDatabase().get_summary();
  std::vector<const Violation*> violation_list;
  violation_list.reserve(summary.ec_summary.violation_list.size() + summary.rc_summary.violation_list.size() + summary.pc_summary.violation_list.size());
  for (const Violation& violation : summary.ec_summary.violation_list) {
    violation_list.push_back(&violation);
  }
  for (const Violation& violation : summary.rc_summary.violation_list) {
    violation_list.push_back(&violation);
  }
  for (const Violation& violation : summary.pc_summary.violation_list) {
    violation_list.push_back(&violation);
  }
  return violation_list;
}

void LVSReporter::outputRPT(const LRModel& lr_model, const std::vector<fort::char_table>& summary_table_list,
                            const std::vector<const Violation*>& violation_list)
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
    const Violation& violation = *violation_list[violation_idx];
    std::string violation_type_name = GetViolationTypeName()(violation.get_violation_type());
    *rpt_file << "\n[" << violation_idx + 1 << "] " << violation_type_name << "\n";
    *rpt_file << "Type: " << violation_type_name << "\n";
    *rpt_file << "Net: " << (violation.get_net_name().empty() ? "-" : violation.get_net_name()) << "\n";
    if (!violation.get_instance_name().empty()) {
      *rpt_file << "Instance: " << violation.get_instance_name() << "\n";
    }
    if (!violation.get_driver_terminal_name().empty()) {
      *rpt_file << "Driver: " << violation.get_driver_terminal_name() << "\n";
    }
    if (!violation.get_related_net_name_list().empty()) {
      *rpt_file << "Net Count: " << violation.get_related_net_name_list().size() << "\n";
    }
    *rpt_file << "Component Count: " << violation.get_component_id_list().size() << "\n";
    *rpt_file << "Terminal Count: " << violation.get_terminal_name_list().size() << "\n";
    *rpt_file << "Components: " << getJoinedString(violation.get_component_id_list()) << "\n";
    if (!violation.get_related_net_name_list().empty()) {
      *rpt_file << "Nets: " << getJoinedString(violation.get_related_net_name_list()) << "\n";
    }
    *rpt_file << "Terminals: " << getJoinedString(violation.get_terminal_name_list()) << "\n";
    *rpt_file << "Coordinates (DBU)\n";
    *rpt_file << "Component Layer LLX LLY URX URY\n";
    bool has_coordinate = false;
    for (const Shape& shape : violation.get_shape_list()) {
      *rpt_file << "- " << shape.get_layer_idx() << " " << shape.get_ll_x() << " " << shape.get_ll_y() << " " << shape.get_ur_x() << " " << shape.get_ur_y()
                << "\n";
      has_coordinate = true;
    }
    for (int32_t component_id : violation.get_component_id_list()) {
      std::map<int32_t, std::vector<Shape>>::iterator shape_iter = component_shape_map.find(component_id);
      if (shape_iter == component_shape_map.end()) {
        continue;
      }
      for (Shape& shape : shape_iter->second) {
        *rpt_file << component_id << " " << shape.get_layer_idx() << " " << shape.get_ll_x() << " " << shape.get_ll_y() << " " << shape.get_ur_x() << " "
                  << shape.get_ur_y() << "\n";
        has_coordinate = true;
      }
    }
    if (!has_coordinate) {
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

void LVSReporter::outputJson(const LRModel& lr_model, const std::vector<const Violation*>& violation_list)
{
  DefData& def_data = LVSDM.getDatabase().get_def_data();
  std::map<int32_t, std::vector<Shape>>& component_shape_map = def_data.get_physical_graph().get_component_shape_map();

  nlohmann::json entity_json = nlohmann::json::array();
  for (LVSEntitySummaryRow& row : getEntitySummaryRowList()) {
    entity_json.push_back(
        {{"entity", row.get_entity()}, {"netlist", row.get_netlist_num()}, {"def", row.get_def_num()}, {"difference", row.get_difference_num()}});
  }

  nlohmann::json connectivity_json = nlohmann::json::array();
  for (LVSConnectivitySummaryRow& row : getConnectivitySummaryRowList()) {
    connectivity_json.push_back(
        {{"connectivity", row.get_connectivity()},
         {"open", {{"count", row.get_open_num()}, {"percentage", getPercentage(row.get_open_num(), row.get_total_num())}}},
         {"short", {{"count", row.get_short_num()}, {"percentage", getPercentage(row.get_short_num(), row.get_total_num())}}},
         {"connected", {{"count", row.get_connected_num()}, {"percentage", getPercentage(row.get_connected_num(), row.get_total_num())}}},
         {"total", row.get_total_num()}});
  }

  std::ofstream* json_file = LVSUTIL.getOutputFileStream(lr_model.get_json_file_path());
  *json_file << "{\n";
  *json_file << "  \"entity\": " << entity_json.dump(2) << ",\n";
  *json_file << "  \"connectivity\": " << connectivity_json.dump(2) << ",\n";
  *json_file << "  \"violations\": [";
  for (int32_t violation_idx = 0; violation_idx < static_cast<int32_t>(violation_list.size()); violation_idx++) {
    const Violation& violation = *violation_list[violation_idx];
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
    if (violation_idx > 0) {
      *json_file << ",";
    }
    *json_file << "\n" << violation_json.dump(2);
  }
  if (!violation_list.empty()) {
    *json_file << "\n";
  }
  *json_file << "  ]\n";
  *json_file << "}\n";
  LVSUTIL.closeFileStream(json_file);
}

void LVSReporter::printSummary(const std::vector<fort::char_table>& summary_table_list)
{
  for (const fort::char_table& summary_table : summary_table_list) {
    LVSUTIL.printTableList({summary_table});
  }
}

}  // namespace ilvs
