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
#include "LVSHeader.hpp"
#include "LVSInterface.hpp"
#include "LVSReporter.hpp"
#include "Logger.hpp"
#include "PDNChecker.hpp"
#include "RoutingChecker.hpp"
#include "Utility.hpp"
#include "ViolationType.hpp"

namespace ilvs {

struct DataManagerTestAccess
{
  static void buildPhysicalGraph(DataManager& data_manager) { data_manager.buildPhysicalGraph(); }
};

}  // namespace ilvs

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

ilvs::RoutingShape makeRoutingShape(const int32_t layer_idx, const int32_t ll_x, const int32_t ll_y, const int32_t ur_x, const int32_t ur_y)
{
  ilvs::RoutingShape routing_shape;
  routing_shape.set_shape(makeShape(layer_idx, ll_x, ll_y, ur_x, ur_y));
  routing_shape.set_layer_order(layer_idx);
  return routing_shape;
}

void addSupplyVia(ilvs::NetRoutingGraph& routing_graph, std::vector<int32_t>& component_id_list, int32_t& routing_shape_idx, const int32_t component_id,
                  const int32_t ll_x, const int32_t ll_y, const int32_t ur_x, const int32_t ur_y)
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

void buildPGComponentFixture(ilvs::PhysicalGraph& physical_graph)
{
  // These synthetic fixtures explicitly supply both global and net-local connectivity.
  for (const auto* net_names : {&physical_graph.get_power_net_name_set(), &physical_graph.get_ground_net_name_set()}) {
    for (const std::string& net_name : *net_names) {
      int32_t net_id = physical_graph.getOrCreateNetId(net_name);
      const auto* global_ids = physical_graph.getNetRoutingShapeComponentIdList(net_id);
      if (global_ids != nullptr) {
        physical_graph.getOrCreatePGNetRoutingShapeComponentIdList(net_id) = *global_ids;
      }
    }
  }
  physical_graph.get_pg_terminal_component_map() = physical_graph.get_terminal_component_map();
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
  ilvs::NetRoutingGraph& connected_graph = physical_graph.getOrCreateNetRoutingGraph("n_connected");
  connected_graph.set_driver_terminal_name("PIN/IN");
  connected_graph.get_routing_shape_list() = {makeRoutingShape(1, 0, 0, 10, 2), makeRoutingShape(1, 10, 0, 20, 2)};
  connected_graph.get_terminal_shape_idx_map()["PIN/IN"] = {0};
  connected_graph.get_terminal_shape_idx_map()["U1/A"] = {1};

  ilvs::NetRoutingGraph& open_graph = physical_graph.getOrCreateNetRoutingGraph("n_open");
  open_graph.set_driver_terminal_name("PIN/OUT");
  open_graph.get_routing_shape_list() = {makeRoutingShape(1, 0, 10, 10, 12), makeRoutingShape(1, 20, 10, 30, 12)};
  open_graph.get_terminal_shape_idx_map()["PIN/OUT"] = {0};
  open_graph.get_terminal_shape_idx_map()["U1/B"] = {1};

  ilvs::NetRoutingGraph& missing_driver_graph = physical_graph.getOrCreateNetRoutingGraph("n_driver_missing");
  missing_driver_graph.get_routing_shape_list() = {makeRoutingShape(1, 0, 30, 20, 32)};
  missing_driver_graph.get_terminal_shape_idx_map()["PIN/DRIVER"] = {0};
  missing_driver_graph.get_terminal_shape_idx_map()["U1/E"] = {0};

  physical_graph.get_component_net_name_map()[42] = {"n_connected", "n_open"};
  physical_graph.get_component_shape_map()[42] = {makeShape(2, 0, 20, 30, 22)};
  physical_graph.get_component_net_name_map()[300] = {"VDD", "VSS"};
  physical_graph.get_power_net_name_set().insert("VDD");
  physical_graph.get_ground_net_name_set().insert("VSS");
  ilvs::NetRoutingGraph& power_routing_graph = physical_graph.getOrCreateNetRoutingGraph("VDD");
  int32_t power_net_id = physical_graph.getNetId("VDD");
  std::vector<int32_t>& power_component_id_list = physical_graph.getOrCreateNetRoutingShapeComponentIdList(power_net_id);
  int32_t power_routing_shape_idx = 0;
  addSupplyVia(power_routing_graph, power_component_id_list, power_routing_shape_idx, 100, 490, 490, 510, 510);
  addSupplyVia(power_routing_graph, power_component_id_list, power_routing_shape_idx, 101, 0, 0, 10, 10);
  ilvs::NetRoutingGraph& ground_routing_graph = physical_graph.getOrCreateNetRoutingGraph("VSS");
  int32_t ground_net_id = physical_graph.getNetId("VSS");
  std::vector<int32_t>& ground_component_id_list = physical_graph.getOrCreateNetRoutingShapeComponentIdList(ground_net_id);
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

  int32_t connected_net_id = physical_graph.getOrCreateNetId("n_connected");
  int32_t open_net_id = physical_graph.getOrCreateNetId("n_open");
  physical_graph.get_component_net_id_map()[42] = {connected_net_id, open_net_id};
  physical_graph.get_component_net_id_map()[300] = {power_net_id, ground_net_id};
  physical_graph.get_component_shape_ref_map()[42] = {{connected_net_id, 0}};
  physical_graph.get_component_shape_ref_map()[300] = {{power_net_id, 0}, {ground_net_id, 0}};
  physical_graph.get_short_component_id_list() = {42, 300};
  physical_graph.set_optimized_component_data_valid(true);
  buildPGComponentFixture(physical_graph);
  database.set_def_data(std::move(def_data));
}

void test_io_supply_ring_waiver(ilvs::Database& database)
{
  database.reset();

  ilvs::DefData def_data;
  def_data.set_design_name("io_supply");
  def_data.get_die().set_real_ll(0, 0);
  def_data.get_die().set_real_ur(1000, 1000);
  ilvs::PhysicalGraph& physical_graph = def_data.get_physical_graph();
  physical_graph.get_power_net_name_set() = {"VDD", "VDDIO"};
  physical_graph.get_ground_net_name_set() = {"VSS", "VSSIO"};

  ilvs::NetRoutingGraph& power_graph = physical_graph.getOrCreateNetRoutingGraph("VDD");
  int32_t power_net_id = physical_graph.getNetId("VDD");
  std::vector<int32_t>& power_component_id_list = physical_graph.getOrCreateNetRoutingShapeComponentIdList(power_net_id);
  int32_t power_routing_shape_idx = 0;
  addSupplyVia(power_graph, power_component_id_list, power_routing_shape_idx, 100, 490, 490, 510, 510);

  ilvs::NetRoutingGraph& ground_graph = physical_graph.getOrCreateNetRoutingGraph("VSS");
  int32_t ground_net_id = physical_graph.getNetId("VSS");
  std::vector<int32_t>& ground_component_id_list = physical_graph.getOrCreateNetRoutingShapeComponentIdList(ground_net_id);
  int32_t ground_routing_shape_idx = 0;
  addSupplyVia(ground_graph, ground_component_id_list, ground_routing_shape_idx, 200, 490, 490, 510, 510);

  physical_graph.get_power_instance_pin_net_map()["U1/VDD"] = "VDD";
  physical_graph.get_power_instance_pin_net_map()["U2/VDD"] = "VDD";
  physical_graph.get_power_instance_pin_net_map()["IO_POWER_vddio_left_0/VDDIO"] = "VDDIO";
  physical_graph.get_power_instance_pin_net_map()["g_iocell_x/VDDIO"] = "VDDIO";
  physical_graph.get_ground_instance_pin_net_map()["U1/VSS"] = "VSS";
  physical_graph.get_ground_instance_pin_net_map()["U2/VSS"] = "VSS";
  physical_graph.get_ground_instance_pin_net_map()["IO_POWER_vssio_left_0/VSSIO"] = "VSSIO";
  physical_graph.get_ground_instance_pin_net_map()["IO_POWER_vssio_right_0/VSSIO"] = "VSSIO";
  physical_graph.get_io_supply_terminal_set() = {"IO_POWER_vddio_left_0/VDDIO", "g_iocell_x/VDDIO",
                                                "IO_POWER_vssio_left_0/VSSIO", "IO_POWER_vssio_right_0/VSSIO"};

  physical_graph.get_terminal_component_map()["U1/VDD"] = 100;
  physical_graph.get_terminal_component_map()["U2/VDD"] = 101;
  physical_graph.get_terminal_component_map()["IO_POWER_vddio_left_0/VDDIO"] = 400;
  physical_graph.get_terminal_component_map()["g_iocell_x/VDDIO"] = 400;
  physical_graph.get_terminal_component_map()["U1/VSS"] = 200;
  physical_graph.get_terminal_component_map()["U2/VSS"] = 201;
  physical_graph.get_terminal_component_map()["IO_POWER_vssio_left_0/VSSIO"] = 500;
  physical_graph.get_terminal_component_map()["IO_POWER_vssio_right_0/VSSIO"] = 501;

  buildPGComponentFixture(physical_graph);
  database.set_def_data(std::move(def_data));

  ilvs::PDNChecker::initInst();
  LVSPC.check();
  ilvs::PDNChecker::destroyInst();

  ilvs::Summary& summary = database.get_summary();
  assert(summary.pc_summary.open_vdd_num == 1);
  assert(summary.pc_summary.open_vss_num == 3);
  std::map<ilvs::ViolationType, int32_t> violation_type_num_map;
  for (ilvs::Violation& violation : summary.pc_summary.violation_list) {
    violation_type_num_map[violation.get_violation_type()]++;
  }
  assert(violation_type_num_map[ilvs::ViolationType::kPowerOpenVDD] == 1);
  assert(violation_type_num_map[ilvs::ViolationType::kPowerOpenVSS] == 3);
}

std::string getFileContent(const std::filesystem::path& file_path)
{
  std::ifstream* file = LVSUTIL.getInputFileStream(file_path.string());
  std::string file_content{std::istreambuf_iterator<char>(*file), std::istreambuf_iterator<char>()};
  LVSUTIL.closeFileStream(file);
  return file_content;
}

namespace {

ilvs::PhysicalGraph& graph()
{
  return LVSDM.getDatabase().get_def_data().get_physical_graph();
}

int wire(const std::string& net, ilvs::RoutingShape shape)
{
  auto& shapes = graph().getOrCreateNetRoutingGraph(net).get_routing_shape_list();
  shapes.push_back(shape);
  return static_cast<int>(shapes.size()) - 1;
}

void via(const std::string& net, int bottom_layer, int top_layer, int x1, int y1, int x2, int y2)
{
  int bottom = wire(net, makeRoutingShape(bottom_layer, x1, y1, x2, y2));
  int top = wire(net, makeRoutingShape(top_layer, x1, y1, x2, y2));
  graph().getOrCreateNetRoutingGraph(net).get_via_shape_idx_pair_list().emplace_back(bottom, top);
}

void terminal(const std::string& net, const std::string& name, ilvs::RoutingShape shape, bool io_cell = false)
{
  if (io_cell) {
    graph().get_io_supply_terminal_set().insert(name);
  }
  auto& routing = graph().getOrCreateNetRoutingGraph(net);
  int index = static_cast<int>(routing.get_routing_shape_list().size());
  routing.get_terminal_shape_idx_map()[name].push_back(index);
  routing.get_routing_shape_list().push_back(shape);
  routing.set_terminal_routing_shape_num(index + 1);
  if (graph().get_power_net_name_set().count(net)) {
    graph().get_power_instance_pin_net_map()[name] = net;
  } else if (graph().get_ground_net_name_set().count(net)) {
    graph().get_ground_instance_pin_net_map()[name] = net;
  }
}

void supplies()
{
  via("VDD", 9, 10, 490, 490, 510, 510);
  via("VSS", 9, 10, 520, 490, 540, 510);
}

void expect(bool condition, const std::string& message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void check(int open_vdd, int open_vss, int shorts)
{
  ilvs::DataManagerTestAccess::buildPhysicalGraph(LVSDM);
  LVSRC.check();
  LVSPC.check();
  const auto& summary = LVSDM.getDatabase().get_summary();
  expect(summary.pc_summary.open_vdd_num == open_vdd,
         "VDD open: expected " + std::to_string(open_vdd) + ", got " + std::to_string(summary.pc_summary.open_vdd_num));
  expect(summary.pc_summary.open_vss_num == open_vss,
         "VSS open: expected " + std::to_string(open_vss) + ", got " + std::to_string(summary.pc_summary.open_vss_num));
  expect(summary.rc_summary.short_net_num == shorts,
         "short components: expected " + std::to_string(shorts) + ", got " + std::to_string(summary.rc_summary.short_net_num));
}

using OpenGroups = std::multiset<std::pair<std::string, std::vector<std::string>>>;

OpenGroups openGroups()
{
  OpenGroups groups;
  for (const auto& violation : LVSDM.getDatabase().get_summary().pc_summary.violation_list) {
    auto terminals = violation.get_terminal_name_list();
    std::sort(terminals.begin(), terminals.end());
    groups.emplace(violation.get_net_name(), std::move(terminals));
  }
  return groups;
}

void expectOpenGroups(const OpenGroups& expected)
{
  expect(openGroups() == expected, "wrong PG open grouping or terminal ownership");
}

void expectReport(const std::string& name, int opens, int shorts, int connected, int total)
{
  auto output_dir = std::filesystem::current_path() / "test_lvs_flow_output" / "pg_connectivity";
  std::filesystem::create_directories(output_dir);
  LVSDM.getConfig().lr_temp_directory_path = output_dir.string() + "/";
  LVSLR.report();
  auto report = nlohmann::json::parse(getFileContent(output_dir / "ilvs.json"));
  for (const auto& row : report.at("connectivity")) {
    if (row.at("connectivity") == name) {
      expect(row.at("open").at("count") == opens, "report dropped PG opens that also short");
      expect(row.at("short").at("count") == shorts, "incorrect PG short count");
      expect(row.at("connected").at("count") == connected, "report double-subtracted overlapping PG faults");
      expect(row.at("total") == total, "incorrect PG total count");
      return;
    }
  }
  throw std::runtime_error("missing PG connectivity row");
}

int test_pg_connectivity()
{
  ilvs::RoutingChecker::initInst();
  ilvs::PDNChecker::initInst();
  ilvs::LVSReporter::initInst();
  const std::vector<std::pair<std::string, std::function<void()>>> tests = {
      {"own_net_vias_connect_both_supplies", [] {
         terminal("VDD", "U/VDD", makeRoutingShape(8, 490, 490, 510, 510));
         terminal("VSS", "U/VSS", makeRoutingShape(8, 520, 490, 540, 510));
         supplies();
         via("VDD", 8, 9, 490, 490, 510, 510);
         via("VSS", 8, 9, 520, 490, 540, 510);
         check(0, 0, 0);
       }},
      {"overlapping_layers_without_pg_via_are_open", [] {
         terminal("VDD", "U/VDD", makeRoutingShape(8, 490, 490, 510, 510));
         supplies();
         check(1, 0, 0);
       }},
      {"disconnected_power_and_ground_pins_are_open", [] {
         terminal("VDD", "U/VDD", makeRoutingShape(9, 100, 100, 110, 110));
         terminal("VSS", "U/VSS", makeRoutingShape(9, 200, 100, 210, 110));
         supplies();
         check(1, 1, 0);
         expectOpenGroups({{"VDD", {"U/VDD"}}, {"VSS", {"U/VSS"}}});
       }},
      {"signal_wire_bypass_cannot_hide_vdd_open", [] {
         terminal("VDD", "U/VDD", makeRoutingShape(9, 100, 495, 110, 505));
         supplies();
         wire("signal", makeRoutingShape(9, 105, 499, 500, 501));
         check(1, 0, 1);
         expectOpenGroups({{"VDD", {"U/VDD"}}});
       }},
      {"signal_pin_bypass_cannot_hide_vss_open", [] {
         terminal("VSS", "U/VSS", makeRoutingShape(9, 700, 495, 710, 505));
         terminal("signal", "U/A", makeRoutingShape(9, 530, 499, 705, 501));
         supplies();
         check(0, 1, 1);
         expectOpenGroups({{"VSS", {"U/VSS"}}});
       }},
      {"pg_short_does_not_create_open_on_complete_supply", [] {
         terminal("VDD", "U/VDD", makeRoutingShape(9, 100, 495, 110, 505));
         supplies();
         wire("VDD", makeRoutingShape(9, 105, 499, 500, 501));
         wire("signal", makeRoutingShape(9, 104, 498, 106, 500));
         check(0, 0, 1);
       }},
      {"same_pg_terminal_multilayer_rects_keep_internal_connectivity", [] {
         terminal("VDD", "U/VDD", makeRoutingShape(8, 0, 0, 10, 10));
         terminal("VDD", "U/VDD", makeRoutingShape(9, 490, 490, 510, 510));
         terminal("VDD", "V/VDD", makeRoutingShape(8, 10, 0, 20, 10));
         supplies();
         check(0, 0, 0);
       }},
      {"missing_pg_terminal_geometry_is_open", [] {
         graph().get_power_instance_pin_net_map()["MISSING/VDD"] = "VDD";
         supplies();
         check(1, 0, 0);
         expectOpenGroups({{"VDD", {"MISSING/VDD"}}});
       }},
      {"missing_supply_reference_reports_connected_pg_pins_as_open", [] {
         terminal("VDD", "U/VDD", makeRoutingShape(9, 0, 0, 10, 10));
         terminal("VDD", "V/VDD", makeRoutingShape(9, 10, 0, 20, 10));
         check(2, 0, 0);
         expectOpenGroups({{"VDD", {"U/VDD", "V/VDD"}}});
       }},
      {"supply_reference_tie_does_not_depend_on_foreign_short", [] {
         // A foreign shape is inserted first so the right island has a lower global component ID.
         wire("signal", makeRoutingShape(9, 512, 497, 514, 503));
         terminal("VDD", "LEFT/VDD", makeRoutingShape(9, 480, 495, 490, 505));
         terminal("VDD", "RIGHT/VDD", makeRoutingShape(9, 510, 495, 520, 505));
         via("VDD", 9, 10, 480, 495, 490, 505);
         via("VDD", 9, 10, 510, 495, 520, 505);
         check(1, 0, 1);
         expectOpenGroups({{"VDD", {"RIGHT/VDD"}}});
       }},
      {"another_power_net_cannot_inherit_supply_through_short", [] {
         graph().get_power_net_name_set().insert("VDD_AUX");
         terminal("VDD", "CORE/VDD", makeRoutingShape(9, 490, 490, 510, 510));
         terminal("VDD_AUX", "AUX/VDD", makeRoutingShape(9, 510, 490, 515, 510));
         supplies();
         check(1, 0, 1);
         expectOpenGroups({{"VDD_AUX", {"AUX/VDD"}}});
       }},
      {"complete_io_rings_do_not_depend_on_supply_names", [] {
         graph().get_power_net_name_set().insert("VDDD");
         graph().get_ground_net_name_set().insert("POC");
         terminal("VDDD", "IO1/PWR", makeRoutingShape(1, 0, 0, 10, 10), true);
         terminal("VDDD", "IO2/PWR", makeRoutingShape(1, 10, 0, 20, 10), true);
         terminal("POC", "IO1/POC", makeRoutingShape(1, 0, 20, 10, 30), true);
         terminal("POC", "IO2/POC", makeRoutingShape(1, 10, 20, 20, 30), true);
         supplies();
         check(0, 0, 0);
       }},
      {"io_and_core_island_still_requires_core_supply", [] {
         terminal("VDD", "IO/VDD", makeRoutingShape(1, 0, 0, 10, 10), true);
         terminal("VDD", "CORE/VDD", makeRoutingShape(1, 10, 0, 20, 10));
         supplies();
         check(2, 0, 0);
       }},
      {"io_ring_with_missing_terminal_geometry_is_not_waived", [] {
         graph().get_power_net_name_set().insert("VDDIO");
         terminal("VDDIO", "IO1/VDDIO", makeRoutingShape(1, 0, 0, 10, 10), true);
         graph().get_power_instance_pin_net_map()["IO2/VDDIO"] = "VDDIO";
         graph().get_io_supply_terminal_set().insert("IO2/VDDIO");
         supplies();
         check(2, 0, 0);
       }},
      {"signal_bypass_cannot_waive_broken_io_ring", [] {
         graph().get_power_net_name_set().insert("VDDIO");
         terminal("VDDIO", "IO1/VDDIO", makeRoutingShape(1, 0, 0, 10, 10), true);
         terminal("VDDIO", "IO2/VDDIO", makeRoutingShape(1, 20, 0, 30, 10), true);
         supplies();
         wire("signal", makeRoutingShape(1, 9, 4, 21, 6));
         check(2, 0, 1);
         expectOpenGroups({{"VDDIO", {"IO1/VDDIO"}}, {"VDDIO", {"IO2/VDDIO"}}});
       }},
      {"signal_pin_bypass_cannot_waive_broken_io_ground_ring", [] {
         graph().get_ground_net_name_set().insert("VSSIO");
         terminal("VSSIO", "IO1/VSSIO", makeRoutingShape(1, 0, 0, 10, 10), true);
         terminal("VSSIO", "IO2/VSSIO", makeRoutingShape(1, 20, 0, 30, 10), true);
         terminal("signal", "U/A", makeRoutingShape(1, 9, 4, 21, 6));
         supplies();
         check(0, 2, 1);
         expectOpenGroups({{"VSSIO", {"IO1/VSSIO"}}, {"VSSIO", {"IO2/VSSIO"}}});
       }},
      {"shorted_global_component_preserves_separate_open_islands", [] {
         terminal("VDD", "LEFT/VDD", makeRoutingShape(9, 100, 495, 110, 505));
         terminal("VDD", "RIGHT/VDD", makeRoutingShape(9, 300, 495, 310, 505));
         supplies();
         wire("signal", makeRoutingShape(9, 105, 499, 500, 501));
         check(2, 0, 1);
         expectOpenGroups({{"VDD", {"LEFT/VDD"}}, {"VDD", {"RIGHT/VDD"}}});
       }},
      {"report_keeps_open_short_overlap_and_counts_union", [] {
         graph().get_power_net_name_set().insert("VDDIO");
         terminal("VDD", "ISLAND/VDD", makeRoutingShape(9, 100, 495, 110, 505));
         terminal("VDD", "SUPPLIED/VDD", makeRoutingShape(9, 490, 490, 510, 510));
         terminal("VDDIO", "CLEAN/VDDIO", makeRoutingShape(8, 700, 700, 710, 710), true);
         terminal("VSS", "U/VSS", makeRoutingShape(9, 520, 490, 540, 510));
         supplies();
         wire("VSS", makeRoutingShape(9, 105, 499, 530, 501));
         check(1, 0, 1);
         expectReport("Power VDD", 1, 2, 1, 3);
         expectReport("Power VSS", 0, 1, 0, 1);
         auto report = nlohmann::json::parse(
             getFileContent(std::filesystem::current_path() / "test_lvs_flow_output" / "pg_connectivity" / "ilvs.json"));
         auto open = std::find_if(report.at("violations").begin(), report.at("violations").end(), [](const auto& violation) {
           return violation.at("type") == "PowerOpenVDD";
         });
         expect(open != report.at("violations").end(), "missing PG open detail in JSON report");
         expect(!open->contains("pg_net_component"), "unexpected PG component field in JSON report");
         expect(open->at("components").size() == 1, "missing global component for short correlation");
         const auto& shapes = open->at("shapes");
         for (const auto& rect : {nlohmann::json::array({100, 495, 110, 505}), nlohmann::json::array({105, 499, 530, 501})}) {
           expect(std::any_of(shapes.begin(), shapes.end(), [&](const auto& shape) { return shape.at("rect") == rect; }),
                  "PG open detail lost geometry from its global component");
         }
         const std::string rpt = getFileContent(std::filesystem::current_path() / "test_lvs_flow_output" / "pg_connectivity" / "ilvs.rpt");
         const auto open_section = rpt.find("Type: PowerOpenVDD");
         expect(open_section != std::string::npos, "missing PG open detail in RPT report");
         const auto section_end = rpt.find("\n[", open_section);
         const auto detail = rpt.substr(open_section, section_end - open_section);
         expect(detail.find("PG Net Component:") == std::string::npos, "unexpected PG component label in RPT report");
         expect(detail.find("100 495 110 505") != std::string::npos, "missing PG island coordinates in RPT report");
         expect(detail.find("105 499 530 501") != std::string::npos, "missing global component geometry in RPT report");
       }},
      {"graph_rebuild_discards_previous_pg_open_components", [] {
         terminal("VDD", "U/VDD", makeRoutingShape(9, 100, 495, 110, 505));
         supplies();
         check(1, 0, 0);
         wire("VDD", makeRoutingShape(9, 105, 499, 500, 501));
         check(0, 0, 0);
       }},
  };
  int failed = 0;
  std::map<std::string, OpenGroups> single_thread_groups;
  omp_set_dynamic(0);
  for (int threads : {1, 3}) {
    omp_set_num_threads(threads);
    for (const auto& [name, run] : tests) {
      LVSDM.getDatabase().reset();
      LVSDM.getConfig().thread_number = threads;
      auto& def = LVSDM.getDatabase().get_def_data();
      def.get_die().set_real_ll(0, 0);
      def.get_die().set_real_ur(1000, 1000);
      graph().reserveNetNum(10);
      graph().get_power_net_name_set().insert("VDD");
      graph().get_ground_net_name_set().insert("VSS");
      try {
        run();
        if (threads == 1) {
          single_thread_groups[name] = openGroups();
        } else if (single_thread_groups.count(name)) {
          expect(openGroups() == single_thread_groups.at(name), "PG open groups differ between one and three threads");
        }
        std::cout << "RESULT PASS " << name << " threads=" << threads << '\n';
      } catch (const std::exception& error) {
        ++failed;
        std::cout << "RESULT FAIL " << name << " threads=" << threads << ": " << error.what() << '\n';
      }
    }
  }
  ilvs::LVSReporter::destroyInst();
  ilvs::PDNChecker::destroyInst();
  ilvs::RoutingChecker::destroyInst();
  return failed;
}

}  // namespace

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

  test_io_supply_ring_waiver(database);
  int failed = test_pg_connectivity();

  ilvs::DataManager::destroyInst();
  ilvs::LVSInterface::destroyInst();
  ilvs::Logger::destroyInst();
  return failed == 0 ? 0 : 1;
}
