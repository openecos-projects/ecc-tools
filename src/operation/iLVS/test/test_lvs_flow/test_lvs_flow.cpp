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
#include "DataManager.hpp"
#include "EntityChecker.hpp"
#include "Logger.hpp"
#include "LVSHeader.hpp"
#include "LVSInterface.hpp"
#include "LVSReporter.hpp"
#include "PDNChecker.hpp"
#include "RoutingChecker.hpp"
#include "Utility.hpp"
#include "ViolationType.hpp"

ilvs::Net makeNet(const std::vector<std::string>& terminal_name_list)
{
  ilvs::Net net;
  net.set_terminal_name_list(terminal_name_list);
  return net;
}

ilvs::Shape makeShape(const int32_t layer_idx, const int32_t ll_x, const int32_t ll_y, const int32_t ur_x, const int32_t ur_y)
{
  ilvs::Shape shape;
  shape.set_layer_idx(layer_idx);
  shape.set_ll_x(ll_x);
  shape.set_ll_y(ll_y);
  shape.set_ur_x(ur_x);
  shape.set_ur_y(ur_y);
  return shape;
}

ilvs::RoutingShape makeRoutingShape(const int32_t layer_idx, const int32_t ll_x, const int32_t ll_y, const int32_t ur_x,
                                    const int32_t ur_y)
{
  ilvs::RoutingShape routing_shape;
  routing_shape.set_shape(makeShape(layer_idx, ll_x, ll_y, ur_x, ur_y));
  routing_shape.set_layer_order(layer_idx);
  return routing_shape;
}

void addSupplyVia(ilvs::NetRoutingGraph& routing_graph, std::vector<int32_t>& component_id_list, int32_t& routing_shape_idx,
                  const int32_t component_id, const int32_t ll_x, const int32_t ll_y, const int32_t ur_x, const int32_t ur_y)
{
  routing_graph.get_routing_shape_list().push_back(makeRoutingShape(9, ll_x, ll_y, ur_x, ur_y));
  routing_graph.get_routing_shape_list().push_back(makeRoutingShape(10, ll_x, ll_y, ur_x, ur_y));
  routing_graph.get_via_shape_idx_pair_list().emplace_back(routing_shape_idx, routing_shape_idx + 1);
  component_id_list.push_back(component_id);
  component_id_list.push_back(component_id);
  routing_shape_idx += 2;
}

void addPowerTerminal(ilvs::DesignData& design_data, const std::string& terminal_name)
{
  design_data.get_terminal_connect_type_map()[terminal_name] = ilvs::ConnectType::kPower;
}

void buildNetlistData(ilvs::Database& database)
{
  ilvs::NetlistData netlist_data;
  netlist_data.set_design_name("netlist_top");
  netlist_data.set_io_terminal_name_list({"PIN/OUT", "PIN/IN", "PIN/VDD", "PIN/VDD"});
  addPowerTerminal(netlist_data, "PIN/VDD");
  netlist_data.get_instance_name_set().insert("U1");
  netlist_data.get_instance_name_set().insert("U2");
  netlist_data.get_net_map()["n_connected"] = makeNet({"PIN/IN", "U1/A"});
  netlist_data.get_net_map()["n_open"] = makeNet({"PIN/OUT", "U1/B"});
  netlist_data.get_net_map()["n_driver_missing"] = makeNet({"PIN/DRIVER", "U1/E"});
  netlist_data.get_net_map()["n_mismatch"] = makeNet({"U1/C"});
  netlist_data.get_net_map()["n_missing"] = makeNet({"U2/A"});
  database.set_netlist_data(std::move(netlist_data));
}

void buildDefData(ilvs::Database& database)
{
  ilvs::DefData def_data;
  def_data.set_design_name("def_top");
  def_data.get_die().set_real_ll(0, 0);
  def_data.get_die().set_real_ur(1000, 1000);
  def_data.set_io_terminal_name_list({"PIN/IN", "PIN/OUT", "PIN/VDD", "PIN/EXTRA"});
  addPowerTerminal(def_data, "PIN/VDD");
  def_data.get_instance_name_set().insert("U1");
  def_data.get_instance_name_set().insert("U3");
  def_data.get_net_map()["n_connected"] = makeNet({"PIN/IN", "U1/A"});
  def_data.get_net_map()["n_open"] = makeNet({"PIN/OUT", "U1/B"});
  def_data.get_net_map()["n_driver_missing"] = makeNet({"PIN/DRIVER", "U1/E"});
  def_data.get_net_map()["n_mismatch"] = makeNet({"U1/D"});
  def_data.get_net_map()["n_unexpected"] = makeNet({"U3/A"});

  ilvs::PhysicalGraph& physical_graph = def_data.get_physical_graph();
  ilvs::NetRoutingGraph& connected_graph = physical_graph.get_net_routing_graph_map()["n_connected"];
  connected_graph.set_driver_terminal_name("PIN/IN");
  connected_graph.get_routing_shape_list() = {makeRoutingShape(1, 0, 0, 10, 2), makeRoutingShape(1, 10, 0, 20, 2)};
  connected_graph.get_terminal_shape_idx_map()["PIN/IN"] = {0};
  connected_graph.get_terminal_shape_idx_map()["U1/A"] = {1};

  ilvs::NetRoutingGraph& open_graph = physical_graph.get_net_routing_graph_map()["n_open"];
  open_graph.set_driver_terminal_name("PIN/OUT");
  open_graph.get_routing_shape_list() = {makeRoutingShape(1, 0, 10, 10, 12), makeRoutingShape(1, 20, 10, 30, 12)};
  open_graph.get_terminal_shape_idx_map()["PIN/OUT"] = {0};
  open_graph.get_terminal_shape_idx_map()["U1/B"] = {1};

  ilvs::NetRoutingGraph& missing_driver_graph = physical_graph.get_net_routing_graph_map()["n_driver_missing"];
  missing_driver_graph.get_routing_shape_list() = {makeRoutingShape(1, 0, 30, 20, 32)};
  missing_driver_graph.get_terminal_shape_idx_map()["PIN/DRIVER"] = {0};
  missing_driver_graph.get_terminal_shape_idx_map()["U1/E"] = {0};

  physical_graph.get_component_net_name_map()[42] = {"n_connected", "n_open"};
  physical_graph.get_component_shape_map()[42] = {makeShape(2, 0, 20, 30, 22)};
  physical_graph.get_component_net_name_map()[300] = {"VDD", "VSS"};
  physical_graph.get_power_net_name_set().insert("VDD");
  physical_graph.get_ground_net_name_set().insert("VSS");
  ilvs::NetRoutingGraph& power_routing_graph = physical_graph.get_net_routing_graph_map()["VDD"];
  std::vector<int32_t>& power_component_id_list = physical_graph.get_net_routing_shape_component_id_list_map()["VDD"];
  int32_t power_routing_shape_idx = 0;
  addSupplyVia(power_routing_graph, power_component_id_list, power_routing_shape_idx, 100, 490, 490, 510, 510);
  addSupplyVia(power_routing_graph, power_component_id_list, power_routing_shape_idx, 101, 0, 0, 10, 10);
  ilvs::NetRoutingGraph& ground_routing_graph = physical_graph.get_net_routing_graph_map()["VSS"];
  std::vector<int32_t>& ground_component_id_list = physical_graph.get_net_routing_shape_component_id_list_map()["VSS"];
  int32_t ground_routing_shape_idx = 0;
  addSupplyVia(ground_routing_graph, ground_component_id_list, ground_routing_shape_idx, 200, 520, 490, 540, 510);
  addSupplyVia(ground_routing_graph, ground_component_id_list, ground_routing_shape_idx, 201, 20, 20, 30, 30);
  physical_graph.get_power_instance_pin_net_map()["U1/VDD"] = "VDD";
  physical_graph.get_power_instance_pin_net_map()["U2/VDD"] = "VDD";
  physical_graph.get_power_instance_pin_net_map()["U3/VDD"] = "VDD";
  physical_graph.get_ground_instance_pin_net_map()["U1/VSS"] = "VSS";
  physical_graph.get_ground_instance_pin_net_map()["U2/VSS"] = "VSS";
  physical_graph.get_ground_instance_pin_net_map()["U3/VSS"] = "VSS";
  physical_graph.get_terminal_component_map()["U1/VDD"] = 300;
  physical_graph.get_terminal_component_map()["U2/VDD"] = 100;
  physical_graph.get_terminal_component_map()["U3/VDD"] = 101;
  physical_graph.get_terminal_component_map()["U1/VSS"] = 300;
  physical_graph.get_terminal_component_map()["U2/VSS"] = 200;
  physical_graph.get_terminal_component_map()["U3/VSS"] = 201;
  database.set_def_data(std::move(def_data));
}

std::string getFileContent(const std::filesystem::path& file_path)
{
  std::ifstream* file = LVSUTIL.getInputFileStream(file_path.string());
  std::string file_content{std::istreambuf_iterator<char>(*file), std::istreambuf_iterator<char>()};
  LVSUTIL.closeFileStream(file);
  return file_content;
}

int main()
{
  ilvs::DataManager::initInst();
  ilvs::Database& database = LVSDM.getDatabase();
  database.reset();
  std::filesystem::path empty_report_directory_path = std::filesystem::current_path() / "test_lvs_flow_empty_output";
  LVSUTIL.removeDir(empty_report_directory_path.string());
  LVSUTIL.createDir((empty_report_directory_path / "lvs_reporter").string());
  LVSDM.getConfig().lr_temp_directory_path = LVSUTIL.getString((empty_report_directory_path / "lvs_reporter").string(), "/");
  database.get_summary().ec_summary.netlist_io_num = 1;
  ilvs::LVSReporter::initInst();
  LVSLR.report();
  ilvs::LVSReporter::destroyInst();
  assert(database.get_summary().ec_summary.netlist_io_num == 1);
  assert(std::filesystem::exists(empty_report_directory_path / "lvs_reporter" / "ilvs.rpt"));
  assert(std::filesystem::exists(empty_report_directory_path / "lvs_reporter" / "ilvs.json"));
  LVSUTIL.removeDir(empty_report_directory_path.string());
  database.get_summary().reset();
  buildNetlistData(database);
  buildDefData(database);

  std::filesystem::path report_directory_path = std::filesystem::current_path() / "test_lvs_flow_output" / "lvs_reporter";
  LVSUTIL.createDir(report_directory_path.string());
  LVSDM.getConfig().lr_temp_directory_path = LVSUTIL.getString(report_directory_path.string(), "/");

  ilvs::EntityChecker::initInst();
  LVSEC.check();
  ilvs::EntityChecker::destroyInst();

  ilvs::RoutingChecker::initInst();
  LVSRC.check();
  ilvs::RoutingChecker::destroyInst();

  ilvs::PDNChecker::initInst();
  LVSPC.check();
  ilvs::PDNChecker::destroyInst();

  ilvs::Summary& summary = database.get_summary();
  assert(summary.ec_summary.netlist_io_num == 2);
  assert(summary.ec_summary.def_io_num == 3);
  assert(summary.ec_summary.io_difference_num == 1);
  assert(summary.ec_summary.instance_difference_num == 2);
  assert(summary.ec_summary.net_difference_num == 3);
  assert(summary.rc_summary.open_net_num == 2);
  assert(summary.rc_summary.short_net_num == 2);
  assert(summary.pc_summary.open_vdd_num == 2);
  assert(summary.pc_summary.open_vss_num == 2);

  std::map<ilvs::ViolationType, int32_t> violation_type_num_map;
  for (ilvs::Violation& violation : summary.ec_summary.violation_list) {
    violation_type_num_map[violation.get_violation_type()]++;
  }
  for (ilvs::Violation& violation : summary.rc_summary.violation_list) {
    violation_type_num_map[violation.get_violation_type()]++;
  }
  for (ilvs::Violation& violation : summary.pc_summary.violation_list) {
    violation_type_num_map[violation.get_violation_type()]++;
  }
  assert(violation_type_num_map[ilvs::ViolationType::kIO] == 1);
  assert(violation_type_num_map[ilvs::ViolationType::kInstance] == 2);
  assert(violation_type_num_map[ilvs::ViolationType::kNet] == 3);
  assert(violation_type_num_map[ilvs::ViolationType::kRoutingOpen] == 2);
  assert(violation_type_num_map[ilvs::ViolationType::kRoutingShort] == 2);
  assert(violation_type_num_map[ilvs::ViolationType::kPowerOpenVDD] == 2);
  assert(violation_type_num_map[ilvs::ViolationType::kPowerOpenVSS] == 2);
  assert(ilvs::GetViolationTypeName()(ilvs::ViolationType::kIO) == "IO");
  assert(ilvs::GetViolationTypeName()(ilvs::ViolationType::kInstance) == "Instance");
  assert(ilvs::GetViolationTypeName()(ilvs::ViolationType::kNet) == "Net");
  assert(ilvs::GetViolationTypeName()(ilvs::ViolationType::kRoutingOpen) == "RoutingOpen");
  assert(ilvs::GetViolationTypeName()(ilvs::ViolationType::kRoutingShort) == "RoutingShort");
  assert(ilvs::GetViolationTypeName()(ilvs::ViolationType::kPowerOpenVDD) == "PowerOpenVDD");
  assert(ilvs::GetViolationTypeName()(ilvs::ViolationType::kPowerOpenVSS) == "PowerOpenVSS");

  ilvs::LVSReporter::initInst();
  LVSLR.report();
  ilvs::LVSReporter::destroyInst();

  std::filesystem::path rpt_file_path = report_directory_path / "ilvs.rpt";
  std::filesystem::path json_file_path = report_directory_path / "ilvs.json";
  assert(std::filesystem::exists(rpt_file_path));
  assert(std::filesystem::exists(json_file_path));
  std::string rpt_content = getFileContent(rpt_file_path);
  std::string json_content = getFileContent(json_file_path);
  assert(rpt_content.find("iLVS Report") != std::string::npos);
  assert(rpt_content.find("[Violation Details]") != std::string::npos);
  assert(rpt_content.find("RoutingOpen") != std::string::npos);
  assert(rpt_content.find("RoutingShort") != std::string::npos);
  assert(rpt_content.find("PowerOpenVDD") != std::string::npos);
  assert(rpt_content.find("PowerOpenVSS") != std::string::npos);
  assert(rpt_content.find("RoutingDriverMissing") == std::string::npos);
  assert(rpt_content.find("Connectivity") != std::string::npos);
  assert(rpt_content.find("Open") != std::string::npos);
  assert(rpt_content.find("Short") != std::string::npos);
  assert(rpt_content.find("Connected") != std::string::npos);
  assert(rpt_content.find("Total") != std::string::npos);
  assert(rpt_content.find("Routing") != std::string::npos);
  assert(rpt_content.find("Power VDD") != std::string::npos);
  assert(rpt_content.find("Power VSS") != std::string::npos);
  assert(rpt_content.find("2 (40.00%)") != std::string::npos);
  assert(rpt_content.find("1 (20.00%)") != std::string::npos);
  assert(rpt_content.find("1 (33.33%)") != std::string::npos);
  assert(json_content.find("\"entity\"") != std::string::npos);
  assert(json_content.find("\"connectivity\"") != std::string::npos);
  assert(json_content.find("\"open\"") != std::string::npos);
  assert(json_content.find("\"short\"") != std::string::npos);
  assert(json_content.find("\"connected\"") != std::string::npos);
  assert(json_content.find("\"total\"") != std::string::npos);
  assert(json_content.find("33.33") != std::string::npos);
  assert(json_content.find("\"violations\"") != std::string::npos);

  ilvs::DataManager::destroyInst();
  ilvs::LVSInterface::destroyInst();
  ilvs::Logger::destroyInst();
  return 0;
}
