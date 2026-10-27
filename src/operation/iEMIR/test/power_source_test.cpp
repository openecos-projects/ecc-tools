// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
// iEDA is licensed under Mulan PSL v2.

#include <filesystem>
#include <functional>
#include <iostream>
#include <unistd.h>

#include "DataManager.hpp"
#include "GraphBuilder.hpp"
#include "Logger.hpp"
#include "PowerSourceReader.hpp"

namespace {
using namespace iemir;

void require(bool condition, const std::string& message)
{
  if (!condition) throw std::runtime_error(message);
}

void expectFailure(const std::function<void()>& action, const std::string& detail)
{
  bool failed = false;
  try {
    action();
  } catch (const std::exception& error) {
    failed = true;
    require(std::string(error.what()).find(detail) != std::string::npos, "missing error context: " + std::string(error.what()));
  }
  require(failed, "accepted invalid supply input: " + detail);
}

void addNet(const std::string& name, PowerNetType type = PowerNetType::kPower)
{
  auto& net = EMIRDM.getDatabase().get_power_net_map()[name];
  net.set_net_name(name);
  net.set_type(type);
  net.set_has_explicit_parasitics(true);
  PowerWireSegment wire;
  wire.set_layer_idx(1);
  wire.set_layer_name("M1");
  wire.set_first_x(0);
  wire.set_first_y(0);
  wire.set_second_x(100);
  wire.set_second_y(0);
  wire.set_width(10);
  wire.set_resistance(1.0);
  net.get_wire_segment_list().push_back(wire);
}

PowerSource source(const std::string& name, const std::string& net, PowerNetType type = PowerNetType::kPower)
{
  PowerSource value;
  value.set_name(name);
  value.set_net_name(net);
  value.set_net_type(type);
  value.set_layer_name("M1");
  value.set_x(50);
  value.set_y(2);
  return value;
}

void reset()
{
  EMIRDM.getDatabase() = Database();
  EMIRDM.getDatabase().set_micron_dbu(1000);
}

void checkSourceReader(const std::filesystem::path& directory)
{
  auto file = directory / "sources.ploc";
  auto read = [&](const std::string& text, int32_t dbu = 1000) {
    std::ofstream(file) << text;
    return PowerSourceReader::read(file.string(), dbu);
  };
  auto records = read("# source coordinates in microns\n"
                      "bump17 0.025 -0.005 M1 VDD_A # explicit network\n"
                      "bump18 0.05 0.002 M1 POWER net=VDD_B\n"
                      "VSS_GROUND_0 0 0 M1 GROUND\n");
  require(records.size() == 3, "incorrect source record count");
  require(records[0].get_name() == "bump17" && records[0].get_net_name() == "VDD_A"
              && records[0].get_x() == 25 && records[0].get_y() == -5,
          "explicit net/DBU coordinates not preserved");
  require(records[1].get_net_name() == "VDD_B" && records[1].get_net_type() == PowerNetType::kPower, "typed net lost");
  require(records[2].get_net_name() == "VSS", "legacy source-name compatibility lost");

  // Local coordinate definitions must never become absolute PLOC records.
  for (const auto& suffix : {".pad", ".pcell"}) {
    auto definition = directory / (std::string("local") + suffix);
    std::ofstream(definition) << "u0 2 3 M1 POWER\n";
    expectFailure([&] { PowerSourceReader::read(definition.string(), 1000); }, "pre-generated PLOC");
  }
  records = read("*PLOC\nbump17 0.025 -0.005 M1 VDD_A\n*PLOC\nbump18 0.05 0 M1 VDD_B\n");
  require(records.size() == 2, "absolute-only sections lost");
  for (const auto& marker : {"*PAD", "*PCELL", "*PAD_PSS", "*PLOC_PSS"}) {
    expectFailure([&] { read(std::string(marker) + "\nu0 2 3 M1 POWER\n"); }, "pre-generated PLOC");
  }
  auto other = directory / "other.ploc";
  std::ofstream(file) << "UNRELATED_POWER_0 1 2 M1 POWER\n";
  std::ofstream(other) << "ground 3 4 M1 GROUND net=VSS\n";
  const std::vector<std::string> files{file.string(), other.string()};
  records = PowerSourceReader::read(files, 1000);
  require(records.size() == 2 && records[0].get_name() == "UNRELATED_POWER_0" && records[0].get_net_name().empty()
              && records[1].get_net_name() == "VSS", "PLOC list inferred a net from source identity or lost a file");
  std::ofstream(other) << "UNRELATED_POWER_0 3 4 M1 VSS\n";
  expectFailure([&] { PowerSourceReader::read(files, 1000); }, "duplicate supply source name");
  std::ofstream(other) << "# empty second file\n";
  expectFailure([&] { PowerSourceReader::read(files, 1000); }, "other.ploc: no supply contacts");
  expectFailure([&] { PowerSourceReader::read(std::vector<std::string>{}, 1000); }, "No supply contact files");

  reset();
  addNet("VDD_A");
  addNet("VDD_B");
  for (const std::string& text : {"bump17 0.05 0.002 M1 VDD_A\n", "VDD_B_POWER_9 0.05 0.002 M1 POWER net=VDD_A\n",
                                 "VDD_A_POWER_0 0.05 0.002 M1 POWER\n"}) {
    EMIRDM.getDatabase().get_power_source_list() = read(text);
    EMIRGB.build();
    auto& graphs = EMIRDM.getDatabase().get_power_graph_map();
    require(graphs.size() == 1 && graphs.count("VDD_A"), "source name changed an explicit network binding");
    auto& graph = graphs.at("VDD_A");
    auto id = graph.get_source_node_id_list().at(0);
    require(graph.get_node_list()[id].get_x() == 50 && graph.get_node_list()[id].get_y() == 0, "equivalent input changed contact");
    require(graph.get_edge_list().size() == 2, "supply contact did not split resistor");
    require(std::abs(graph.get_edge_list()[0].get_resistance() + graph.get_edge_list()[1].get_resistance() - 1.0) < 1.e-12,
            "supply contact changed total resistance");
  }
  for (const std::string& invalid : {
           "s nan 0 M1 POWER\n", "s inf 0 M1 POWER\n", "s 1e100 0 M1 POWER\n", "s 2147483.648 0 M1 POWER\n",
           "s -2147483.649 0 M1 POWER\n", "s 1um 0 M1 POWER\n", "s 0 0 M1\n", "s 0 0 M1 POWER net=\n",
           "s 0 0 M1 POWER r=1\n", "s 0 0 M1 VDD_A net=VDD_B\n", "s 0 0 M1 POWER net=VDD_A unexpected\n"}) {
    expectFailure([&] { read(invalid); }, "line 1");
  }
  expectFailure([&] { read("s 0 0 M1 VDD_A\ns 1 0 M1 VDD_A\n"); }, "line 2");
  expectFailure([&] { read("# no sources\n"); }, "no supply");
  expectFailure([&] { read("s 0 0 M1 VDD_A\n", 0); }, "units");
  expectFailure([&] { PowerSourceReader::read((directory / "missing.ploc").string(), 1000); }, "missing.ploc");
}

void checkContactDiagnostics(const std::filesystem::path& directory)
{
  reset();
  addNet("VDD");
  EMIRDM.getDatabase().get_power_source_list() = {source("projected", "VDD")};
  EMIRGB.build();
  auto report = directory / "source_connections.csv";
  auto contents = [&] {
    std::ifstream input(report);
    require(input.good(), "supply connection diagnostics missing");
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
  };
  auto text = contents();
  require(text.find("requested_x_dbu,requested_y_dbu,node_id,node_x_dbu,node_y_dbu,displacement_dbu,contact_kind,element_index") != std::string::npos,
          "diagnostics lack contact coordinates");
  require(text.find("projected,VDD,M1,50,2,2,50,0,2,wire,0\n") != std::string::npos, "diagnostics do not describe accepted projection");

  // Two wire objects meeting at the contact must remain a single ideal node.
  auto& net = EMIRDM.getDatabase().get_power_net_map().at("VDD");
  net.get_wire_segment_list().front().set_second_x(50);
  auto wire = net.get_wire_segment_list().front();
  wire.set_first_x(50);
  wire.set_second_x(100);
  net.get_wire_segment_list().push_back(wire);
  EMIRDM.getDatabase().get_power_source_list() = {source("shared", "VDD")};
  EMIRGB.build();
  text = contents();
  require(text.find("projected") == std::string::npos, "diagnostics retained a previous build");
  require(std::count(text.begin(), text.end(), '\n') == 3, "shared endpoint should report both attached wires");
  require(EMIRDM.getDatabase().get_power_graph_map().at("VDD").get_source_node_id_list().size() == 1, "shared contact became extra ideal sources");

  EMIRDM.getDatabase().get_power_source_list() = {source("pad,\"17\"", "VDD")};
  EMIRGB.build();
  require(contents().find("\"pad,\"\"17\"\"\",VDD,") != std::string::npos, "source identity is not CSV escaped");

  auto bad = source("wrong_layer", "VDD");
  bad.set_layer_name("MISSING_LAYER");
  EMIRDM.getDatabase().get_power_source_list() = {bad};
  expectFailure([] { EMIRGB.build(); }, "MISSING_LAYER");
  require(contents().find("pad,") == std::string::npos, "failed build retained stale accepted contacts");

  bad = source("unconnected", "VDD");
  bad.set_y(20);
  EMIRDM.getDatabase().get_power_source_list() = {bad};
  expectFailure([] { EMIRGB.build(); }, "unconnected");

  // A source on either side of an offset via must refer to that side's node.
  reset();
  addNet("VDD");
  auto& via_net = EMIRDM.getDatabase().get_power_net_map().at("VDD");
  via_net.get_wire_segment_list().clear();
  PowerVia via;
  via.set_bottom_layer_name("M1");
  via.set_top_layer_name("M2");
  via.set_bottom_layer_idx(1);
  via.set_top_layer_idx(2);
  via.set_bottom_x(50);
  via.set_bottom_y(0);
  via.set_top_x(60);
  via.set_top_y(10);
  via.set_resistance(1.0);
  via_net.get_via_list().push_back(via);
  for (bool top : {false, true}) {
    auto contact = source("via_supply", "VDD");
    contact.set_layer_name(top ? "M2" : "M1");
    contact.set_x(top ? 60 : 50);
    contact.set_y(top ? 10 : 0);
    EMIRDM.getDatabase().get_power_source_list() = {contact};
    EMIRGB.build();
    require(contents().find(top ? "via_supply,VDD,M2,60,10,1,60,10,0,via_top,0\n"
                                : "via_supply,VDD,M1,50,0,0,50,0,0,via_bottom,0\n") != std::string::npos,
            "via source audit selected the wrong side");
  }
}
}  // namespace

int main()
{
  setenv("ECC_LOGGER_THROW_ON_ERROR", "1", 1);
  iemir::Logger::initInst();
  iemir::DataManager::initInst();
  iemir::GraphBuilder::initInst();
  auto directory = std::filesystem::temp_directory_path() / ("iemir_supply_" + std::to_string(getpid()));
  std::filesystem::create_directories(directory);
  EMIRDM.getConfig().gb_temp_directory_path = directory.string() + "/";
  int result = 0;
  try {
    reset();
    addNet("VDD_A");
    addNet("VDD_B");
    EMIRDM.getDatabase().get_power_source_list() = {source("bump17", "VDD_A")};
    EMIRGB.build();
    require(EMIRDM.getDatabase().get_power_graph_map().size() == 1, "explicit source drove another domain");
    require(EMIRDM.getDatabase().get_power_graph_map().at("VDD_A").get_source_node_id_list().size() == 1, "missing ideal contact");

    EMIRDM.getDatabase().get_power_source_list().push_back(source("missing", "ABSENT"));
    expectFailure([] { EMIRGB.build(); }, "ABSENT");

    EMIRDM.getDatabase().get_power_source_list() = {source("ambiguous", "")};
    expectFailure([] { EMIRGB.build(); }, "ambiguous");

    EMIRDM.getDatabase().get_power_source_list() = {source("wrong_type", "VDD_A", PowerNetType::kGround)};
    expectFailure([] { EMIRGB.build(); }, "wrong_type");

    EMIRDM.getDatabase().get_power_source_list() = {source("untyped", "VDD_B", PowerNetType::kNone)};
    EMIRGB.build();
    require(EMIRDM.getDatabase().get_power_graph_map().count("VDD_B") == 1, "explicit net did not resolve its PG type");

    reset();
    addNet("VDD");
    EMIRDM.getDatabase().get_power_source_list() = {source("unique", "")};
    EMIRGB.build();
    require(EMIRDM.getDatabase().get_power_source_list().front().get_net_name() == "VDD", "single-domain identity stayed unresolved");
    checkSourceReader(directory);
    checkContactDiagnostics(directory);
    std::cout << "Supply source tests passed\n";
  } catch (const std::exception& error) {
    std::cerr << "Supply source test failed: " << error.what() << '\n';
    result = 1;
  }
  iemir::GraphBuilder::destroyInst();
  iemir::DataManager::destroyInst();
  iemir::Logger::destroyInst();
  std::filesystem::remove_all(directory);
  return result;
}
