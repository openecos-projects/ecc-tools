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
#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "LVSReporter.hpp"
#include "json.hpp"

int main()
{
  ilvs::LVSReporter::initInst();

  const std::filesystem::path output_dir = std::filesystem::temp_directory_path() / "ilvs_lvs_reporter_test";
  std::filesystem::remove_all(output_dir);

  ilvs::CheckResult result;
  result.netlist_io_num = 2;
  result.def_io_num = 2;
  result.netlist_power_ground_io_num = 1;
  result.def_power_ground_io_num = 2;
  result.missing_io_num = 1;
  result.unexpected_io_num = 2;
  result.netlist_instance_num = 3;
  result.def_instance_num = 3;
  result.missing_instance_num = 1;
  result.unexpected_instance_num = 2;
  result.netlist_net_num = 1;
  result.def_net_num = 1;
  result.missing_net_num = 1;
  result.unexpected_net_num = 2;
  result.net_pin_mismatch_num = 3;
  result.routing_checked_net_num = 4;
  result.routing_connected_net_num = 2;
  result.routing_open_net_num = 2;
  result.routing_open_load_pin_num = 3;
  result.routing_missing_driver_num = 1;
  result.routing_short_component_num = 1;
  result.power_supply_point_num = 1;
  result.ground_supply_point_num = 1;
  result.power_instance_pin_num = 3;
  result.ground_instance_pin_num = 4;
  result.connected_power_instance_pin_num = 2;
  result.connected_ground_instance_pin_num = 3;
  result.disconnected_power_instance_pin_num = 1;
  result.disconnected_ground_instance_pin_num = 1;
  result.supply_point_list = {{"VDD", 1, 5, 5, 50, 10, true}, {"VSS", 2, 5, 5, 50, 90, false}};
  ilvs::Violation routing_violation;
  routing_violation.type = "RoutingOpen";
  routing_violation.net_name = "n1";
  routing_violation.driver_terminal_name = "u1/A";
  routing_violation.terminal_list = {"u1/B"};
  routing_violation.shape_list = {{1, 0, 0, 10, 10}};
  result.violation_list.push_back(std::move(routing_violation));
  ilvs::Violation short_violation;
  short_violation.type = "RoutingShort";
  short_violation.component_id_list = {0};
  short_violation.related_net_name_list = {"n1", "n2"};
  result.violation_list.push_back(std::move(short_violation));
  ilvs::Violation instance_violation;
  instance_violation.type = "MissingInstance";
  instance_violation.instance_name = "u2";
  result.violation_list.push_back(std::move(instance_violation));
  ilvs::Violation power_violation;
  power_violation.type = "PowerDisconnected";
  power_violation.net_name = "VDD";
  power_violation.terminal_list = {"u3/VDD"};
  power_violation.component_id_list = {1};
  result.violation_list.push_back(std::move(power_violation));
  ilvs::Netlist netlist;
  ilvs::Net netlist_net;
  netlist_net.name = "n1";
  netlist_net.terminal_list = {"u1/A", "u1/B"};
  netlist.net_map[netlist_net.name] = netlist_net;
  netlist.logical_graph.io_pin_list = {"PIN/clk", "PIN/rst"};
  netlist.logical_graph.instance_map["u1"] = {"u1", {"A", "B"}, "NAND2_X1"};
  netlist.logical_graph.net_edge_num = 2;
  ilvs::Netlist def;
  ilvs::Net def_net;
  def_net.name = "n1";
  def_net.terminal_list = {"u1/A"};
  def_net.wire_segment_num = 5;
  def_net.via_num = 6;
  def.net_map[def_net.name] = def_net;
  def.physical_graph.node_num = 1;
  def.physical_graph.edge_num = 2;
  def.physical_graph.candidate_pair_num = 3;
  def.physical_graph.max_active_shape_num = 4;
  def.physical_graph.component_num = 1;
  def.physical_graph.component_shape_map[0] = {{1, 0, 0, 10, 10}};
  def.physical_graph.component_shape_map[1] = {{5, 40, 0, 60, 20}};
  def.physical_graph.io_pin_list = {"PIN/clk", "PIN/rst"};
  def.physical_graph.instance_map["u1"] = {"u1", {}, "NAND2_X1"};

  const std::vector<fort::char_table> summary_table_list = LVSLR.getSummaryTableList(result, netlist, def);
  assert(summary_table_list.size() == 2);
  assert(summary_table_list.front().to_string().find("Entity") != std::string::npos);
  assert(summary_table_list.front().to_string().find("Entity Comparison") == std::string::npos);
  assert(summary_table_list.front().to_string().find("NETLIST") != std::string::npos);
  assert(summary_table_list.front().to_string().find("DEF") != std::string::npos);
  assert(summary_table_list.front().to_string().find("Difference") != std::string::npos);
  assert(summary_table_list.front().to_string().find("IO(without pg)") != std::string::npos);
  assert(summary_table_list.front().to_string().find("Instance") != std::string::npos);
  assert(summary_table_list.front().to_string().find("Net") != std::string::npos);
  assert(summary_table_list.front().to_string().find("Graph") == std::string::npos);
  assert(summary_table_list.front().to_string().find("Total") == std::string::npos);
  assert(summary_table_list[1].to_string().find("Connectivity") != std::string::npos);
  assert(summary_table_list[1].to_string().find("Type") != std::string::npos);
  assert(summary_table_list[1].to_string().find("Routing") != std::string::npos);
  assert(summary_table_list[1].to_string().find("Open Net") != std::string::npos);
  assert(summary_table_list[1].to_string().find("Short Net") != std::string::npos);
  assert(summary_table_list[1].to_string().find("Power") != std::string::npos);
  assert(summary_table_list[1].to_string().find("Open VDD") != std::string::npos);
  assert(summary_table_list[1].to_string().find("Open VSS") != std::string::npos);
  assert(summary_table_list[1].to_string().find("Open Net |     3") != std::string::npos);
  assert(summary_table_list[1].to_string().find("Short Net |     1") != std::string::npos);
  assert(summary_table_list[1].to_string().find("Open VDD |     1") != std::string::npos);
  assert(summary_table_list[1].to_string().find("Open VSS |     1") != std::string::npos);
  assert(summary_table_list[1].to_string().find("Checked Net") == std::string::npos);
  assert(summary_table_list[1].to_string().find("Disconnected Instance Pin") == std::string::npos);

  LVSLR.report(result, netlist, def, output_dir.string());
  assert(std::filesystem::exists(output_dir / "ilvs.rpt"));
  assert(std::filesystem::exists(output_dir / "ilvs.json"));
  std::ifstream rpt_file(output_dir / "ilvs.rpt");
  std::string rpt((std::istreambuf_iterator<char>(rpt_file)), std::istreambuf_iterator<char>());
  const size_t report_title_pos = rpt.find("iLVS Report");
  const size_t entity_table_pos = rpt.find("Entity");
  const size_t violation_details_pos = rpt.find("[Violation Details]");
  assert(report_title_pos != std::string::npos);
  assert(entity_table_pos != std::string::npos);
  assert(rpt.find("Entity Comparison") == std::string::npos);
  assert(rpt.find("IO(without pg)") != std::string::npos);
  assert(rpt.find("NETLIST") != std::string::npos);
  assert(rpt.find("DEF") != std::string::npos);
  assert(rpt.find("Connectivity") != std::string::npos);
  assert(rpt.find("Type") != std::string::npos);
  assert(rpt.find("Routing") != std::string::npos);
  assert(rpt.find("Open Net") != std::string::npos);
  assert(rpt.find("Short Net") != std::string::npos);
  assert(rpt.find("Power") != std::string::npos);
  assert(rpt.find("Open VDD") != std::string::npos);
  assert(rpt.find("Open VSS") != std::string::npos);
  assert(rpt.find("Checked Net") == std::string::npos);
  assert(rpt.find("Open Load Pin") == std::string::npos);
  assert(rpt.find("Missing Driver Pin") == std::string::npos);
  assert(rpt.find("Short Component") == std::string::npos);
  assert(rpt.find("Power Connectivity") == std::string::npos);
  assert(rpt.find("Disconnected Instance Pin") == std::string::npos);
  assert(rpt.find("[Power Supply Points]") == std::string::npos);
  assert(rpt.find("Check Summary") == std::string::npos);
  assert(rpt.find("Net Pin Mismatch") == std::string::npos);
  assert(rpt.find("[Statistics]") == std::string::npos);
  assert(rpt.find("IO comparison excludes power/ground ports") == std::string::npos);
  assert(rpt.find("Graph Candidate Pair") == std::string::npos);
  assert(rpt.find("Total") == std::string::npos);
  assert(violation_details_pos != std::string::npos);
  assert(report_title_pos < entity_table_pos);
  assert(entity_table_pos < violation_details_pos);
  assert(rpt.find("[1] RoutingOpen") != std::string::npos);
  assert(rpt.find("[2] RoutingShort") != std::string::npos);
  assert(rpt.find("[3] MissingInstance") != std::string::npos);
  assert(rpt.find("[4] PowerDisconnected") != std::string::npos);
  assert(rpt.find("Driver") != std::string::npos);
  assert(rpt.find("Nets: n1 n2") != std::string::npos);
  assert(rpt.find("NETLIST Master") == std::string::npos);
  assert(rpt.find("DEF Master") == std::string::npos);
  assert(rpt.find("Coordinates (DBU)") != std::string::npos);
  assert(rpt.find("Component") != std::string::npos);
  assert(rpt.find("LLX") != std::string::npos);
  rpt_file.close();
  std::ifstream json_file(output_dir / "ilvs.json");
  nlohmann::json json;
  json_file >> json;
  assert(json["entity"].size() == 3);
  assert(json["entity"][0]["entity"] == "IO(without pg)");
  assert(json["entity"][0]["netlist"] == 2);
  assert(json["entity"][0]["def"] == 2);
  assert(json["entity"][0]["difference"] == 3);
  assert(json["entity"][1]["entity"] == "Instance");
  assert(json["entity"][1]["difference"] == 3);
  assert(json["entity"][2]["entity"] == "Net");
  assert(json["entity"][2]["difference"] == 6);
  assert(json["connectivity"].size() == 4);
  assert(json["connectivity"][0]["connectivity"] == "Routing");
  assert(json["connectivity"][0]["type"] == "Open Net");
  assert(json["connectivity"][0]["count"] == 3);
  assert(json["connectivity"][1]["type"] == "Short Net");
  assert(json["connectivity"][1]["count"] == 1);
  assert(json["connectivity"][2]["connectivity"] == "Power");
  assert(json["connectivity"][2]["type"] == "Open VDD");
  assert(json["connectivity"][2]["count"] == 1);
  assert(json["connectivity"][3]["type"] == "Open VSS");
  assert(json["connectivity"][3]["count"] == 1);
  assert(!json.contains("summary"));
  assert(!json.contains("power_supply_points"));
  assert(!json.contains("physical_graph"));
  assert(json["violations"][0]["driver"] == "u1/A");
  assert(json["violations"][0]["shapes"][0]["layer"] == 1);
  assert((json["violations"][1]["nets"] == std::vector<std::string>{"n1", "n2"}));
  assert(json["violations"][2]["instance"] == "u2");
  assert(!json["violations"][2].contains("expected_master"));
  assert(!json["violations"][2].contains("def_master"));
  json_file.close();

  result.violation_list.clear();
  LVSLR.report(result, netlist, def, output_dir.string());
  std::ifstream no_violation_rpt_file(output_dir / "ilvs.rpt");
  std::string no_violation_rpt((std::istreambuf_iterator<char>(no_violation_rpt_file)), std::istreambuf_iterator<char>());
  assert(no_violation_rpt.find("[Violation Details]\nNone") != std::string::npos);
  std::ifstream no_violation_json_file(output_dir / "ilvs.json");
  nlohmann::json no_violation_json;
  no_violation_json_file >> no_violation_json;
  assert(no_violation_json["violations"].empty());
  std::filesystem::remove_all(output_dir);
  ilvs::LVSReporter::destroyInst();
  return 0;
}
