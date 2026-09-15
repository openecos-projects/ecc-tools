// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of the Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include <cmath>
#include <filesystem>
#include <fstream>

#include "DataManager.hpp"
#include "EMAnalyzer.hpp"
#include "EMIRReporter.hpp"
#include "GraphBuilder.hpp"
#include "IRAnalyzer.hpp"
#include "InstancePower.hpp"
#include "Logger.hpp"
#include "PowerEdge.hpp"
#include "PowerEdgeType.hpp"
#include "PowerGraph.hpp"
#include "PowerNet.hpp"
#include "PowerNetType.hpp"
#include "PowerNode.hpp"
#include "PowerNodeType.hpp"
#include "PowerPin.hpp"
#include "PowerSource.hpp"
#include "PowerVia.hpp"
#include "PowerWireSegment.hpp"
#include "PTPXPowerReader.hpp"
#include "RedHawkResNetworkReader.hpp"

namespace {

iemir::PowerGraph buildPowerGraph(const std::string& net_name, iemir::PowerNetType power_net_type)
{
  iemir::PowerGraph power_graph;
  power_graph.set_net_name(net_name);
  power_graph.set_net_type(power_net_type);

  iemir::PowerNode source_node;
  source_node.set_node_id(0);
  source_node.set_type(iemir::PowerNodeType::kSource);
  source_node.set_is_source(true);
  power_graph.get_node_list().push_back(source_node);
  power_graph.get_source_node_id_list().push_back(source_node.get_node_id());

  iemir::PowerNode instance_node;
  instance_node.set_node_id(1);
  instance_node.set_type(iemir::PowerNodeType::kInstancePin);
  instance_node.get_instance_id_set().insert(1);
  power_graph.get_node_list().push_back(instance_node);
  power_graph.get_instance_node_id_list_map()[1].push_back(instance_node.get_node_id());
  if (power_net_type == iemir::PowerNetType::kPower) {
    power_graph.get_instance_pin_node_id_list_map()[std::make_pair(1, "VDD")].push_back(instance_node.get_node_id());
  }

  iemir::PowerEdge power_edge;
  power_edge.set_edge_id(0);
  power_edge.set_type(iemir::PowerEdgeType::kWire);
  power_edge.set_first_node_id(source_node.get_node_id());
  power_edge.set_second_node_id(instance_node.get_node_id());
  power_edge.set_layer_idx(1);
  power_edge.set_layer_name("MET1");
  power_edge.set_width(1000);
  power_edge.set_resistance(1.0);
  power_graph.get_edge_list().push_back(power_edge);
  return power_graph;
}

bool checkPowerGraph(iemir::PowerGraph& power_graph, double expected_voltage)
{
  double voltage = power_graph.get_node_list()[1].get_voltage();
  double current = power_graph.get_edge_list()[0].get_current();
  return std::abs(voltage - expected_voltage) <= EMIR_ERROR && std::abs(current - 1.0) <= EMIR_ERROR;
}

bool checkSubMicroampLoad()
{
  constexpr double kLoadCurrent = 5.0e-7;
  iemir::InstancePower instance_power;
  instance_power.set_instance_id(1);
  instance_power.set_voltage(1.0);
  instance_power.set_average_current(kLoadCurrent);

  EMIRDM.getDatabase().get_instance_power_map().clear();
  EMIRDM.getDatabase().get_power_graph_map().clear();
  EMIRDM.getDatabase().get_instance_power_map()[1] = instance_power;
  EMIRDM.getDatabase().get_power_graph_map()["VDD"] = buildPowerGraph("VDD", iemir::PowerNetType::kPower);

  iemir::IRAnalyzer::initInst();
  EMIRIA.analyze();
  iemir::IRAnalyzer::destroyInst();

  iemir::PowerGraph& power_graph = EMIRDM.getDatabase().get_power_graph_map().at("VDD");
  const double voltage = power_graph.get_node_list()[1].get_voltage();
  const double current = power_graph.get_node_list()[1].get_current();
  const bool is_pass = std::abs(voltage - (1.0 - kLoadCurrent)) <= 1.0e-12
                       && std::abs(current + kLoadCurrent) <= 1.0e-12;
  EMIRDM.getDatabase().get_instance_power_map().clear();
  EMIRDM.getDatabase().get_power_graph_map().clear();
  return is_pass;
}

bool checkReport(const std::string& report_file_path, const std::string& content)
{
  std::ifstream report_file(report_file_path);
  std::string report_content((std::istreambuf_iterator<char>(report_file)), std::istreambuf_iterator<char>());
  return report_content.find(content) != std::string::npos;
}

bool checkShiftedViaGraph(const std::filesystem::path& directory)
{
  iemir::PowerNet power_net;
  power_net.set_net_name("SHIFTED_VIA");
  power_net.set_type(iemir::PowerNetType::kPower);
  power_net.set_has_explicit_parasitics(true);

  iemir::PowerWireSegment bottom_wire;
  bottom_wire.set_layer_idx(1);
  bottom_wire.set_layer_name("MET1");
  bottom_wire.set_first_x(0);
  bottom_wire.set_first_y(0);
  bottom_wire.set_second_x(10);
  bottom_wire.set_second_y(0);
  bottom_wire.set_width(2);
  bottom_wire.set_resistance(1.0);
  power_net.get_wire_segment_list().push_back(bottom_wire);

  iemir::PowerWireSegment top_wire;
  top_wire.set_layer_idx(2);
  top_wire.set_layer_name("MET2");
  top_wire.set_first_x(20);
  top_wire.set_first_y(0);
  top_wire.set_second_x(30);
  top_wire.set_second_y(0);
  top_wire.set_width(2);
  top_wire.set_resistance(1.0);
  power_net.get_wire_segment_list().push_back(top_wire);

  iemir::PowerVia power_via;
  power_via.set_bottom_layer_idx(1);
  power_via.set_top_layer_idx(2);
  power_via.set_via_name("VIA_SHIFTED");
  power_via.set_x(12);
  power_via.set_y(0);
  power_via.set_bottom_x(10);
  power_via.set_bottom_y(0);
  power_via.set_top_x(20);
  power_via.set_top_y(0);
  power_via.set_resistance(2.0);
  power_net.get_via_list().push_back(power_via);

  iemir::PowerPin source_pin;
  source_pin.set_layer_idx(1);
  source_pin.set_x(0);
  source_pin.set_y(0);
  source_pin.set_is_source(true);
  power_net.get_pin_list().push_back(source_pin);

  EMIRDM.getDatabase().get_power_net_map().clear();
  EMIRDM.getDatabase().get_power_net_map()[power_net.get_net_name()] = power_net;
  iemir::GraphBuilder::initInst();
  EMIRGB.build();
  iemir::GraphBuilder::destroyInst();

  iemir::PowerGraph& power_graph = EMIRDM.getDatabase().get_power_graph_map().at(power_net.get_net_name());
  auto& coordinate_node_map = power_graph.get_coordinate_node_id_map();
  bool is_pass = power_graph.get_is_connected() && power_graph.get_node_list().size() == 4 && power_graph.get_edge_list().size() == 3
                 && coordinate_node_map.count(std::make_tuple(1, 10, 0)) == 1 && coordinate_node_map.count(std::make_tuple(2, 20, 0)) == 1
                 && coordinate_node_map.count(std::make_tuple(1, 15, 0)) == 0 && coordinate_node_map.count(std::make_tuple(2, 15, 0)) == 0;
  EMIRDM.getDatabase().set_design_name("shifted_via");
  EMIRDM.getDatabase().set_micron_dbu(1000);
  EMIRDM.getConfig().er_temp_directory_path = directory.string() + "/";
  for (auto& edge : power_graph.get_edge_list()) {
    if (edge.get_type() == iemir::PowerEdgeType::kVia) {
      edge.set_current(1.0e-6);
    }
  }
  iemir::EMIRReporter::initInst();
  EMIRER.report();
  iemir::EMIRReporter::destroyInst();
  is_pass = is_pass && checkReport((directory / "shifted_via.em.worst").string(), "via VIA_SHIFTED  (0.012,0.000)")
            && checkReport((directory / "em.rpt").string(), "via VIA_SHIFTED  (0.012,0.000)")
            && checkReport((directory / "shifted_via.res_network").string(), "VIA_SHIFTED SHIFTED_VIA (0.012 0.000)")
            && checkReport((directory / "shifted_via.em.worst").string(), "1.000000e-06")
            && coordinate_node_map.count(std::make_tuple(1, 12, 0)) == 0 && coordinate_node_map.count(std::make_tuple(2, 12, 0)) == 0;
  EMIRDM.getDatabase().get_power_net_map().clear();
  EMIRDM.getDatabase().get_power_graph_map().clear();
  return is_pass;
}

bool checkPTPXPowerReader(const std::filesystem::path& directory)
{
  std::filesystem::path file_path = directory / "ptpx_instance_power.tsv";
  std::ofstream output(file_path);
  output << "# iEMIR_PTPX_INSTANCE_POWER_V1\n";
  output << "instance_name\tvoltage_v\tinternal_power_w\tswitching_power_w\tleakage_power_w\ttotal_power_w\taverage_current_a\n";
  output << "U1\t1.2\t1e-6\t2e-6\t3e-6\t6e-6\t5e-6\n";
  output.close();
  std::vector<iemir::PTPXPowerRecord> records = iemir::PTPXPowerReader::read(file_path.string());
  return records.size() == 1 && records.front().instance_name == "U1" && std::abs(records.front().total_power - 6.0e-6) <= 1.0e-18
         && std::abs(records.front().average_current - 5.0e-6) <= 1.0e-18;
}

bool checkPointSourceDoesNotShortNearbySegment()
{
  bool is_pass = true;
  // Exercise an interior contact, a shared endpoint, and an off-grid PLOC
  // just outside the wire end. Segment order must not affect the contact.
  for (bool reverse_order : {false, true}) {
    for (int32_t source_x : {12, 10, -1}) {
      iemir::PowerNet net;
      net.set_net_name("POINT_SOURCE");
      net.set_type(iemir::PowerNetType::kPower);
      net.set_has_explicit_parasitics(true);
      for (int32_t index : {0, 1}) {
        int32_t start = 10 * (reverse_order ? 1 - index : index);
        iemir::PowerWireSegment wire;
        wire.set_layer_idx(1);
        wire.set_layer_name("MET1");
        wire.set_first_x(start);
        wire.set_first_y(0);
        wire.set_second_x(start + 10);
        wire.set_second_y(0);
        wire.set_width(6);
        wire.set_resistance(1.0);
        net.get_wire_segment_list().push_back(wire);
      }
      iemir::PowerSource source;
      source.set_name("PLOC");
      source.set_net_name(net.get_net_name());
      source.set_net_type(net.get_type());
      source.set_layer_name("MET1");
      source.set_x(source_x);
      source.set_y(0);
      EMIRDM.getDatabase().get_power_net_map()[net.get_net_name()] = net;
      EMIRDM.getDatabase().get_power_source_list().push_back(source);
      iemir::GraphBuilder::initInst();
      EMIRGB.build();
      iemir::GraphBuilder::destroyInst();
      auto& graph = EMIRDM.getDatabase().get_power_graph_map().at(net.get_net_name());
      auto contact = graph.get_coordinate_node_id_map().at(std::make_tuple(1, std::max(source_x, 0), 0));
      is_pass = is_pass && graph.get_is_connected() && graph.get_source_node_id_list() == std::vector<std::size_t>{contact};
      double total_resistance = 0.0;
      for (auto& edge : graph.get_edge_list()) {
        total_resistance += edge.get_resistance();
      }
      is_pass = is_pass && std::abs(total_resistance - 2.0) < 1e-12;
      EMIRDM.getDatabase().get_power_source_list().clear();
      EMIRDM.getDatabase().get_power_net_map().clear();
      EMIRDM.getDatabase().get_power_graph_map().clear();
    }
  }
  return is_pass;
}

bool checkRedHawkResNetworkReader(const std::filesystem::path& directory)
{
  std::filesystem::path file_path = directory / "redhawk.res_network";
  std::ofstream output(file_path);
  output << "W 10 MET1 VDD 0 0 10 0 10 2 0 2 2 1 h\n";
  output << "WS 10_1 0 1 10 1 0 0 0 r 0.25 1\n";
  output << "W 11 MET2 VSS 20 0 22 0 22 8 20 8 2 0.75 v\n";
  output << "V 7 VIA1 VIA1_TEST VDD 1 2 2 0.1 0.2 0.5 7 0 0 0 r RULE LANDING 10_1\n";
  output.close();

  iemir::RedHawkResNetwork network = iemir::RedHawkResNetworkReader::read(file_path.string());
  const iemir::RedHawkWireSegmentRecord& explicit_segment = network.wire_segments.at(0);
  const iemir::RedHawkViaRecord& via = network.vias.at(0);
  return network.wire_segments.size() == 1 && network.vias.size() == 1 && explicit_segment.id == "10_1"
         && explicit_segment.layer_name == "MET1" && explicit_segment.net_name == "VDD"
         && std::abs(explicit_segment.resistance_ohm - 0.25) <= 1.0e-18 && via.id == "7" && via.layer_name == "VIA1"
         && via.net_name == "VDD" && via.cut_num == 2 && via.connected_wire_segment_ids == std::vector<std::string>{"10_1"}
         && std::abs(via.resistance_ohm - 0.5) <= 1.0e-18;
}

bool checkImportedPinAreaInjection()
{
  bool is_pass = true;
  // Nonuniform extraction intervals must yield 1:4:3 load weights at 10, 11,
  // and 14. A second pin introduces a subdivision at 12; its insertion order
  // must not alter the first pin's physical load distribution.
  for (bool filler_first : {false, true}) {
    for (bool pin_specific : {false, true}) {
      for (int32_t source_x : {0, 10}) {
        iemir::PowerNet net;
        net.set_net_name("VDD");
        net.set_type(iemir::PowerNetType::kPower);
        net.set_has_explicit_parasitics(true);
        std::vector<int32_t> coordinates{0, 10, 11, 14, 20};
        for (std::size_t i = 1; i < coordinates.size(); ++i) {
          iemir::PowerWireSegment wire;
          wire.set_layer_idx(1);
          wire.set_layer_name("MET1");
          wire.set_first_x(coordinates[i - 1]);
          wire.set_first_y(0);
          wire.set_second_x(coordinates[i]);
          wire.set_second_y(0);
          wire.set_width(2);
          wire.set_resistance(coordinates[i] - coordinates[i - 1]);
          net.get_wire_segment_list().push_back(wire);
        }
        iemir::PowerPin load;
        load.set_layer_idx(1);
        load.set_instance_id(1);
        load.set_pin_name("U1:VDD");
        load.set_x(12);
        load.set_y(0);
        load.set_low_x(9);
        load.set_high_x(15);
        load.set_low_y(-1);
        load.set_high_y(1);
        iemir::PowerPin filler = load;
        filler.set_instance_id(2);
        filler.set_pin_name("U2:VDD");
        filler.set_low_x(12);
        filler.set_high_x(12);
        net.get_pin_list() = filler_first ? std::vector<iemir::PowerPin>{filler, load} : std::vector<iemir::PowerPin>{load, filler};
        iemir::PowerSource source;
        source.set_name("PLOC");
        source.set_net_name("VDD");
        source.set_net_type(iemir::PowerNetType::kPower);
        source.set_layer_name("MET1");
        source.set_x(source_x);
        source.set_y(0);
        EMIRDM.getDatabase().get_power_net_map()["VDD"] = net;
        EMIRDM.getDatabase().get_power_source_list().push_back(source);
        iemir::InstancePower power;
        power.set_instance_id(1);
        power.set_voltage(1.0);
        power.set_average_current(8e-6);
        if (pin_specific) {
          power.get_average_current_by_pin_name_map()["VDD"] = 8e-6;
        }
        EMIRDM.getDatabase().get_instance_power_map()[1] = power;
        iemir::GraphBuilder::initInst();
        EMIRGB.build();
        iemir::GraphBuilder::destroyInst();
        iemir::IRAnalyzer::initInst();
        EMIRIA.analyze();
        iemir::IRAnalyzer::destroyInst();
        auto& graph = EMIRDM.getDatabase().get_power_graph_map().at("VDD");
        for (auto [x, expected_current] : std::map<int32_t, double>{{10, -1e-6}, {11, -4e-6}, {14, -3e-6}, {12, 0.0}}) {
          auto id = graph.get_coordinate_node_id_map().at(std::make_tuple(1, x, 0));
          is_pass = is_pass && std::abs(graph.get_node_list()[id].get_current() - expected_current) < 1e-15;
        }
        auto endpoint = graph.get_coordinate_node_id_map().at(std::make_tuple(1, 14, 0));
        double expected_drop = source_x == 0 ? 96e-6 : 16e-6;
        is_pass = is_pass && std::abs(graph.get_node_list()[endpoint].get_voltage() - (1.0 - expected_drop)) < 1e-12;
        EMIRDM.getDatabase().get_instance_power_map().clear();
        EMIRDM.getDatabase().get_power_source_list().clear();
        EMIRDM.getDatabase().get_power_net_map().clear();
        EMIRDM.getDatabase().get_power_graph_map().clear();
      }
    }
  }
  return is_pass;
}

bool checkViaUsesConnectedResistorJunction()
{
  iemir::RedHawkWireSegmentRecord rail;
  rail.id = "rail";
  rail.layer_name = "MET4";
  rail.net_name = "VDD";
  rail.first_x_um = rail.second_x_um = 235.0;
  rail.first_y_um = 231.0;
  rail.second_y_um = 233.8;
  iemir::RedHawkWireSegmentRecord pad = rail;
  pad.id = "pad";
  pad.first_y_um = 233.8;
  pad.second_y_um = 233.84;
  std::unordered_map<std::string, const iemir::RedHawkWireSegmentRecord*> segments{{"rail", &rail}, {"pad", &pad}};
  iemir::RedHawkViaRecord via;
  via.net_name = "VDD";
  via.x_um = 234.75;
  via.y_um = 233.84;
  for (const auto& ids : {std::vector<std::string>{"rail", "pad"}, std::vector<std::string>{"pad", "rail"}}) {
    via.connected_wire_segment_ids = ids;
    auto coordinate = iemir::RedHawkResNetworkReader::connectionCoordinate(via, segments, "MET4");
    if (!coordinate || coordinate->first != 235.0 || coordinate->second != 233.8) {
      return false;
    }
  }
  // A single connected segment still uses projection; an unrelated layer
  // must not acquire a fabricated connection.
  via.connected_wire_segment_ids = {"pad"};
  auto coordinate = iemir::RedHawkResNetworkReader::connectionCoordinate(via, segments, "MET4");
  return coordinate && coordinate->first == 235.0 && coordinate->second == 233.84
         && !iemir::RedHawkResNetworkReader::connectionCoordinate(via, segments, "MET2");
}

}  // namespace

int main(int argc, char* argv[])
{
  if (argc > 1) {
    try {
      for (int argument_index = 1; argument_index < argc; argument_index++) {
        iemir::RedHawkResNetworkReader::read(argv[argument_index]);
      }
    } catch (const std::exception& error) {
      std::cerr << error.what() << "\n";
      return 1;
    }
    return 0;
  }

  iemir::Logger::initInst();
  iemir::DataManager::initInst();

  std::filesystem::path report_directory_path = std::filesystem::temp_directory_path() / "iemir_ir_em_test";
  std::filesystem::remove_all(report_directory_path);
  std::filesystem::create_directories(report_directory_path);
  EMIRDM.getConfig().ia_temp_directory_path = report_directory_path.string() + "/";

  bool is_pass = checkPTPXPowerReader(report_directory_path) && checkRedHawkResNetworkReader(report_directory_path) && checkShiftedViaGraph(report_directory_path)
                 && checkPointSourceDoesNotShortNearbySegment() && checkImportedPinAreaInjection() && checkViaUsesConnectedResistorJunction()
                 && checkSubMicroampLoad();
  EMIRDM.getDatabase().set_design_name("test_design");
  EMIRDM.getDatabase().set_micron_dbu(1000);
  iemir::EMMetalRule metal_rule;
  metal_rule.set_name("MET1");
  metal_rule.set_em_limit_ma_per_um(1.0);
  EMIRDM.getDatabase().get_em_tech().get_metal_rule_map()["MET1"] = metal_rule;
  iemir::InstancePower instance_power;
  instance_power.set_instance_id(1);
  instance_power.set_voltage(1.0);
  instance_power.set_internal_power(1.0);
  instance_power.set_average_current(1.0);
  instance_power.get_average_current_by_pin_name_map()["VDD"] = 1.0;
  EMIRDM.getDatabase().get_instance_power_map()[instance_power.get_instance_id()] = instance_power;
  EMIRDM.getDatabase().get_power_graph_map()["VDD"] = buildPowerGraph("VDD", iemir::PowerNetType::kPower);
  EMIRDM.getDatabase().get_power_graph_map()["VSS"] = buildPowerGraph("VSS", iemir::PowerNetType::kGround);

  iemir::IRAnalyzer::initInst();
  EMIRIA.analyze();
  iemir::IRAnalyzer::destroyInst();

  iemir::EMAnalyzer::initInst();
  EMIREA.analyze();
  iemir::EMAnalyzer::destroyInst();

  is_pass = is_pass && checkPowerGraph(EMIRDM.getDatabase().get_power_graph_map()["VDD"], 0.0)
            && checkPowerGraph(EMIRDM.getDatabase().get_power_graph_map()["VSS"], 1.0);
  is_pass = is_pass && checkReport((report_directory_path / "solver_diagnostics.csv").string(), "relative_l2_residual");
  EMIRDM.getConfig().er_temp_directory_path = report_directory_path.string() + "/";
  iemir::EMIRReporter::initInst();
  EMIRER.report();
  iemir::EMIRReporter::destroyInst();
  is_pass = is_pass && checkReport((report_directory_path / "ir.rpt").string(), "#voltage #ideal_volt")
            && checkReport((report_directory_path / "em.rpt").string(), "# For wires: #layer #end-to-end_coordinates #EM_Ratio #Current_value")
            && checkReport((report_directory_path / "test_design.ir.worst").string(), "#x_y_location")
            && checkReport((report_directory_path / "test_design.em.worst").string(), "#current")
            && checkReport((report_directory_path / "test_design.res_network").string(), "em_limit(A)")
            && checkReport((report_directory_path / "test_design.res_network").string(), "100000");
  std::filesystem::remove_all(report_directory_path);

  iemir::DataManager::destroyInst();
  iemir::Logger::destroyInst();
  return is_pass ? 0 : 1;
}
