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

#include "CharacterTimingTestCommon.hh"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <memory>
#include <set>
#include <string>
#include <system_error>

#include "api/TimingEngine.hh"
#include "gtest/gtest.h"
#include "liberty/Lib.hh"
#include "log/Log.hh"
#include "netlist/Instance.hh"
#include "netlist/Net.hh"
#include "netlist/Netlist.hh"
#include "netlist/Pin.hh"
#include "netlist/Port.hh"
#include "sta/StaArc.hh"
#include "sta/Sta.hh"
#include "sta/StaDelayPropagation.hh"
#include "sta/StaSlewPropagation.hh"
#include "sta/StaVertex.hh"

using namespace ista;

namespace {

namespace fs = std::filesystem;

std::string readText(const fs::path& file_path) {
  std::ifstream input(file_path);
  return std::string((std::istreambuf_iterator<char>(input)),
                     std::istreambuf_iterator<char>());
}

std::string normalizeGeneratedVerilogText(std::string text) {
  const auto newline_pos = text.find('\n');
  if (newline_pos != std::string::npos &&
      text.rfind("//Generate the verilog at ", 0) == 0) {
    text.erase(0, newline_pos + 1);
  }
  return text;
}

bool hasDelayValue(const std::vector<double>& values, double expected) {
  return std::any_of(values.begin(), values.end(), [expected](double value) {
    return std::abs(value - expected) < 1e-12;
  });
}

LibCell* addCell(
    LibLibrary& lib, const char* cell_name,
    std::initializer_list<std::pair<const char*, LibPort::LibertyPortType>>
        ports) {
  auto lib_cell = std::make_unique<LibCell>(cell_name, &lib);
  auto* lib_cell_ptr = lib_cell.get();

  for (const auto& [port_name, port_type] : ports) {
    auto lib_port = std::make_unique<LibPort>(port_name);
    lib_port->set_port_type(port_type);
    lib_port->set_ower_cell(lib_cell_ptr);
    lib_cell_ptr->addLibertyPort(std::move(lib_port));
  }

  lib.addLibertyCell(std::move(lib_cell));
  return lib.findCell(cell_name);
}

std::unique_ptr<LibTable> makeScalarTable(LibTable::TableType type,
                                          double value) {
  auto table = std::make_unique<LibTable>(type, nullptr);
  table->addTableValue(std::make_unique<LibFloatValue>(value));
  return table;
}

std::unique_ptr<LibAxis> makeAxis(const char* axis_name,
                                  std::initializer_list<double> values) {
  auto axis = std::make_unique<LibAxis>(axis_name);
  std::vector<std::unique_ptr<LibAttrValue>> axis_values;
  axis_values.reserve(values.size());
  for (double value : values) {
    axis_values.emplace_back(std::make_unique<LibFloatValue>(value));
  }
  axis->set_axis_values(std::move(axis_values));
  return axis;
}

std::unique_ptr<LibTable> makeLoadIndexedTable(
    LibTable::TableType type, LibLutTableTemplate* table_template,
    std::initializer_list<double> load_axis,
    std::initializer_list<double> table_values) {
  EXPECT_EQ(load_axis.size(), table_values.size());

  auto table = std::make_unique<LibTable>(type, table_template);
  table->addAxis(makeAxis("index_1", load_axis));
  for (double table_value : table_values) {
    table->addTableValue(std::make_unique<LibFloatValue>(table_value));
  }

  return table;
}

std::unique_ptr<LibArc> makeScalarDelayArc(
    LibCell* lib_cell, const char* src_port, const char* snk_port,
    const char* timing_sense, double rise_delay, double fall_delay,
    double rise_transition, double fall_transition) {
  auto lib_arc = std::make_unique<LibArc>();
  lib_arc->set_src_port(src_port);
  lib_arc->set_snk_port(snk_port);
  lib_arc->set_timing_type("combinational");
  lib_arc->set_timing_sense(timing_sense);
  lib_arc->set_owner_cell(lib_cell);

  auto delay_model = std::make_unique<LibDelayTableModel>();
  delay_model->addTable(
      makeScalarTable(LibTable::TableType::kCellRise, rise_delay));
  delay_model->addTable(
      makeScalarTable(LibTable::TableType::kCellFall, fall_delay));
  delay_model->addTable(
      makeScalarTable(LibTable::TableType::kRiseTransition, rise_transition));
  delay_model->addTable(
      makeScalarTable(LibTable::TableType::kFallTransition, fall_transition));
  lib_arc->set_table_model(std::move(delay_model));
  return lib_arc;
}

std::unique_ptr<LibArc> makeLoadIndexedDelayArc(
    LibCell* lib_cell, LibLutTableTemplate* table_template, const char* src_port,
    const char* snk_port, const char* timing_sense,
    std::initializer_list<double> rise_delay,
    std::initializer_list<double> fall_delay,
    std::initializer_list<double> rise_transition,
    std::initializer_list<double> fall_transition) {
  auto lib_arc = std::make_unique<LibArc>();
  lib_arc->set_src_port(src_port);
  lib_arc->set_snk_port(snk_port);
  lib_arc->set_timing_type("combinational");
  lib_arc->set_timing_sense(timing_sense);
  lib_arc->set_owner_cell(lib_cell);

  auto delay_model = std::make_unique<LibDelayTableModel>();
  delay_model->addTable(makeLoadIndexedTable(LibTable::TableType::kCellRise,
                                             table_template, {0.1, 0.4},
                                             rise_delay));
  delay_model->addTable(makeLoadIndexedTable(LibTable::TableType::kCellFall,
                                             table_template, {0.1, 0.4},
                                             fall_delay));
  delay_model->addTable(makeLoadIndexedTable(
      LibTable::TableType::kRiseTransition, table_template, {0.1, 0.4},
      rise_transition));
  delay_model->addTable(makeLoadIndexedTable(
      LibTable::TableType::kFallTransition, table_template, {0.1, 0.4},
      fall_transition));
  lib_arc->set_table_model(std::move(delay_model));
  return lib_arc;
}

class RestoredStaBehaviorTest : public testing::Test {
 protected:
  static void SetUpTestSuite() {
    char config[] = "test";
    char* argv[] = {config};
    Log::init(argv);
    unsetenv("IEDA_CHARACTER_TIMING_REUSE_OUTPUT");
    _timing_engine = ista::test::prepareGoldenCaseTimingRuntime(
        ista::test::goldenCaseRuntimeOutputDir());
  }

  static void TearDownTestSuite() {
    TimingEngine::destroyTimingEngine();
    Sta::destroySta();
    Log::end();
  }

  static TimingEngine* _timing_engine;
};

TimingEngine* RestoredStaBehaviorTest::_timing_engine = nullptr;

TEST(RestoredBehaviorTest, sta_constructor_initializes_log_when_needed) {
  if (Log::isInit()) {
    Log::end();
  }
  Sta::destroySta();
  ASSERT_FALSE(Log::isInit());

  auto* ista = Sta::getOrCreateSta();
  ASSERT_NE(ista, nullptr);
  EXPECT_TRUE(Log::isInit());

  Sta::destroySta();
  Log::end();
}

TEST(RestoredBehaviorTest, netlist_writer_escapes_assign_net_names) {
  char config[] = "test";
  char* argv[] = {config};
  Log::init(argv);

  Netlist netlist;
  netlist.set_name("top");

  auto& input_port = netlist.addPort(Port("a", PortDir::kIn));
  auto& output_port = netlist.addPort(Port("y", PortDir::kOut));
  auto& escaped_net = netlist.addNet(Net("foo/bar"));
  escaped_net.addPinPort(&input_port);
  escaped_net.addPinPort(&output_port);

  const fs::path output_path =
      fs::temp_directory_path() / "ista_restored_behavior_writer.v";
  std::set<std::string> exclude_cell_names;
  netlist.writeVerilog(output_path.c_str(), exclude_cell_names, false);

  const auto verilog_text = readText(output_path);
  EXPECT_NE(verilog_text.find("assign \\foo/bar = a ;"), std::string::npos);
  EXPECT_NE(verilog_text.find("assign y = \\foo/bar ;"), std::string::npos);

  std::error_code ec;
  fs::remove(output_path, ec);
  Log::end();
}

TEST(RestoredBehaviorTest, netlist_writer_suppresses_self_assigns_for_escaped_names) {
  char config[] = "test";
  char* argv[] = {config};
  Log::init(argv);

  Netlist netlist;
  netlist.set_name("top");

  auto& escaped_input_port = netlist.addPort(Port("foo[0]", PortDir::kIn));
  auto& escaped_output_port = netlist.addPort(Port("bar/baz", PortDir::kOut));
  auto& escaped_input_net = netlist.addNet(Net("foo[0]"));
  auto& escaped_output_net = netlist.addNet(Net("bar/baz"));
  escaped_input_net.addPinPort(&escaped_input_port);
  escaped_output_net.addPinPort(&escaped_output_port);

  const fs::path output_path =
      fs::temp_directory_path() / "ista_restored_behavior_self_assign.v";
  std::set<std::string> exclude_cell_names;
  netlist.writeVerilog(output_path.c_str(), exclude_cell_names, false);

  const auto verilog_text = readText(output_path);
  EXPECT_EQ(verilog_text.find("assign \\foo[0] = foo[0] ;"), std::string::npos)
      << "did not expect bogus self-assigns for escaped input net names";
  EXPECT_EQ(verilog_text.find("assign bar/baz = \\bar/baz ;"), std::string::npos)
      << "did not expect bogus self-assigns for escaped output net names";

  std::error_code ec;
  fs::remove(output_path, ec);
  Log::end();
}

TEST(RestoredBehaviorTest, netlist_writer_writes_single_module_header) {
  char config[] = "test";
  char* argv[] = {config};
  Log::init(argv);

  Netlist netlist;
  netlist.set_name("top");
  netlist.addPort(Port("a", PortDir::kIn));
  netlist.addPort(Port("y", PortDir::kOut));

  const fs::path output_path =
      fs::temp_directory_path() / "ista_restored_behavior_single_header.v";
  std::set<std::string> exclude_cell_names;
  netlist.writeVerilog(output_path.c_str(), exclude_cell_names, false);

  const auto verilog_text = normalizeGeneratedVerilogText(readText(output_path));
  EXPECT_EQ(verilog_text.find("module top (module top ("), std::string::npos);
  EXPECT_EQ(verilog_text.find("module top ("), 0U);

  std::error_code ec;
  fs::remove(output_path, ec);
  Log::end();
}

TEST(RestoredBehaviorTest,
     mixed_sense_arc_sets_propagate_both_output_transitions_on_data_paths) {
  if (!Log::isInit()) {
    char config[] = "test";
    char* argv[] = {config};
    Log::init(argv);
  }
  Sta::destroySta();

  auto* ista = Sta::getOrCreateSta();
  ASSERT_NE(ista, nullptr);

  LibLibrary lib("mixed_sense_arc_propagation_test");
  auto* logic_cell = addCell(
      lib, "logic_cell",
      {{"A", LibPort::LibertyPortType::kInput},
       {"Y", LibPort::LibertyPortType::kOutput}});
  auto* load_cell =
      addCell(lib, "load_cell", {{"I", LibPort::LibertyPortType::kInput}});
  ASSERT_NE(logic_cell, nullptr);
  ASSERT_NE(load_cell, nullptr);

  auto* load_port =
      dynamic_cast<LibPort*>(load_cell->get_cell_port_or_port_bus("I"));
  ASSERT_NE(load_port, nullptr);
  load_port->set_port_cap(0.25);

  LibArcSet arc_set;
  auto positive_arc = makeScalarDelayArc(logic_cell, "A", "Y", "positive_unate",
                                         0.11, 0.12, 0.31, 0.32);
  auto* positive_arc_ptr = positive_arc.get();
  auto negative_arc = makeScalarDelayArc(logic_cell, "A", "Y", "negative_unate",
                                         0.21, 0.22, 0.41, 0.42);
  arc_set.addLibertyArc(std::move(positive_arc));
  arc_set.addLibertyArc(std::move(negative_arc));

  Instance logic_inst("u_logic", logic_cell);
  auto* src_pin =
      logic_inst.addPin("A", logic_cell->get_cell_port_or_port_bus("A"));
  auto* snk_pin =
      logic_inst.addPin("Y", logic_cell->get_cell_port_or_port_bus("Y"));
  ASSERT_NE(src_pin, nullptr);
  ASSERT_NE(snk_pin, nullptr);

  Instance load_inst("u_load", load_cell);
  auto* load_pin =
      load_inst.addPin("I", load_cell->get_cell_port_or_port_bus("I"));
  ASSERT_NE(load_pin, nullptr);

  Net output_net("logic_y");
  output_net.addPinPort(snk_pin);
  output_net.addPinPort(load_pin);

  StaVertex src_vertex(src_pin);
  StaVertex snk_vertex(snk_pin);
  src_vertex.initSlewData(NS_TO_FS(0.02), true);
  auto* src_rise_slew =
      src_vertex.getSlewData(AnalysisMode::kMax, TransType::kRise, nullptr);
  ASSERT_NE(src_rise_slew, nullptr);

  StaInstArc inst_arc(&src_vertex, &snk_vertex, positive_arc_ptr, &arc_set,
                      &logic_inst);

  StaSlewPropagation slew_propagation;
  EXPECT_EQ(slew_propagation(&inst_arc), 1U);

  auto* rise_slew =
      snk_vertex.getSlewData(AnalysisMode::kMax, TransType::kRise, src_rise_slew);
  auto* fall_slew =
      snk_vertex.getSlewData(AnalysisMode::kMax, TransType::kFall, src_rise_slew);
  ASSERT_NE(rise_slew, nullptr);
  ASSERT_NE(fall_slew, nullptr);
  EXPECT_DOUBLE_EQ(FS_TO_NS(rise_slew->get_slew()), 0.31);
  EXPECT_DOUBLE_EQ(FS_TO_NS(fall_slew->get_slew()), 0.42);

  StaDelayPropagation delay_propagation;
  EXPECT_EQ(delay_propagation(&inst_arc), 1U);

  std::vector<double> max_rise_delays_ns;
  std::vector<double> max_fall_delays_ns;
  auto has_delay = [](const std::vector<double>& values, double expected) {
    return std::any_of(values.begin(), values.end(), [expected](double value) {
      return std::abs(value - expected) < 1e-12;
    });
  };

  StaArc* inst_arc_ptr = &inst_arc;
  StaData* arc_delay_data = nullptr;
  FOREACH_ARC_DELAY_DATA(inst_arc_ptr, arc_delay_data) {
    if (arc_delay_data->get_delay_type() != AnalysisMode::kMax) {
      continue;
    }

    auto delay_ns = FS_TO_NS(
        dynamic_cast<StaArcDelayData*>(arc_delay_data)->get_arc_delay());
    if (arc_delay_data->get_trans_type() == TransType::kRise) {
      max_rise_delays_ns.push_back(delay_ns);
    } else {
      max_fall_delays_ns.push_back(delay_ns);
    }
  }

  EXPECT_TRUE(has_delay(max_rise_delays_ns, 0.11));
  EXPECT_TRUE(has_delay(max_rise_delays_ns, 0.21));
  EXPECT_TRUE(has_delay(max_fall_delays_ns, 0.12));
  EXPECT_TRUE(has_delay(max_fall_delays_ns, 0.22));

  Sta::destroySta();
  Log::end();
}

TEST(RestoredBehaviorTest,
     mixed_sense_arc_sets_use_transition_specific_loads_for_secondary_branch) {
  if (!Log::isInit()) {
    char config[] = "test";
    char* argv[] = {config};
    Log::init(argv);
  }
  Sta::destroySta();

  auto* ista = Sta::getOrCreateSta();
  ASSERT_NE(ista, nullptr);

  LibLibrary lib("mixed_sense_transition_specific_load_test");
  lib.set_cap_unit(CapacitiveUnit::kPF);

  auto load_template = std::make_unique<LibLutTableTemplate>("load_template");
  load_template->set_template_variable1("total_output_net_capacitance");
  auto* load_template_ptr = load_template.get();
  lib.addLutTemplate(std::move(load_template));

  auto* logic_cell = addCell(
      lib, "logic_cell",
      {{"A", LibPort::LibertyPortType::kInput},
       {"Y", LibPort::LibertyPortType::kOutput}});
  auto* load_cell =
      addCell(lib, "load_cell", {{"I", LibPort::LibertyPortType::kInput}});
  ASSERT_NE(logic_cell, nullptr);
  ASSERT_NE(load_cell, nullptr);

  auto* load_port =
      dynamic_cast<LibPort*>(load_cell->get_cell_port_or_port_bus("I"));
  ASSERT_NE(load_port, nullptr);
  load_port->set_port_cap(AnalysisMode::kMax, TransType::kRise, 0.1);
  load_port->set_port_cap(AnalysisMode::kMax, TransType::kFall, 0.4);

  LibArcSet arc_set;
  auto positive_arc = makeLoadIndexedDelayArc(logic_cell, load_template_ptr,
                                              "A", "Y", "positive_unate",
                                              {1.1, 1.4}, {2.1, 2.4},
                                              {10.1, 10.4}, {20.1, 20.4});
  auto* positive_arc_ptr = positive_arc.get();
  auto negative_arc = makeLoadIndexedDelayArc(logic_cell, load_template_ptr,
                                              "A", "Y", "negative_unate",
                                              {3.1, 3.4}, {4.1, 4.4},
                                              {30.1, 30.4}, {40.1, 40.4});
  arc_set.addLibertyArc(std::move(positive_arc));
  arc_set.addLibertyArc(std::move(negative_arc));

  Instance logic_inst("u_logic", logic_cell);
  auto* src_pin =
      logic_inst.addPin("A", logic_cell->get_cell_port_or_port_bus("A"));
  auto* snk_pin =
      logic_inst.addPin("Y", logic_cell->get_cell_port_or_port_bus("Y"));
  ASSERT_NE(src_pin, nullptr);
  ASSERT_NE(snk_pin, nullptr);

  Instance load_inst("u_load", load_cell);
  auto* load_pin =
      load_inst.addPin("I", load_cell->get_cell_port_or_port_bus("I"));
  ASSERT_NE(load_pin, nullptr);

  Net output_net("logic_y");
  output_net.addPinPort(snk_pin);
  output_net.addPinPort(load_pin);

  StaVertex src_vertex(src_pin);
  StaVertex snk_vertex(snk_pin);
  src_vertex.initSlewData(NS_TO_FS(0.02), true);
  auto* src_rise_slew =
      src_vertex.getSlewData(AnalysisMode::kMax, TransType::kRise, nullptr);
  ASSERT_NE(src_rise_slew, nullptr);

  StaInstArc inst_arc(&src_vertex, &snk_vertex, positive_arc_ptr, &arc_set,
                      &logic_inst);

  StaSlewPropagation slew_propagation;
  EXPECT_EQ(slew_propagation(&inst_arc), 1U);

  auto* rise_slew =
      snk_vertex.getSlewData(AnalysisMode::kMax, TransType::kRise, src_rise_slew);
  auto* fall_slew =
      snk_vertex.getSlewData(AnalysisMode::kMax, TransType::kFall, src_rise_slew);
  ASSERT_NE(rise_slew, nullptr);
  ASSERT_NE(fall_slew, nullptr);
  EXPECT_DOUBLE_EQ(FS_TO_NS(rise_slew->get_slew()), 10.1);
  EXPECT_DOUBLE_EQ(FS_TO_NS(fall_slew->get_slew()), 40.4);

  StaDelayPropagation delay_propagation;
  EXPECT_EQ(delay_propagation(&inst_arc), 1U);

  std::vector<double> max_rise_delays_ns;
  std::vector<double> max_fall_delays_ns;
  StaArc* inst_arc_ptr = &inst_arc;
  StaData* arc_delay_data = nullptr;
  FOREACH_ARC_DELAY_DATA(inst_arc_ptr, arc_delay_data) {
    if (arc_delay_data->get_delay_type() != AnalysisMode::kMax) {
      continue;
    }

    auto delay_ns = FS_TO_NS(
        dynamic_cast<StaArcDelayData*>(arc_delay_data)->get_arc_delay());
    if (arc_delay_data->get_trans_type() == TransType::kRise) {
      max_rise_delays_ns.push_back(delay_ns);
    } else {
      max_fall_delays_ns.push_back(delay_ns);
    }
  }

  EXPECT_TRUE(hasDelayValue(max_rise_delays_ns, 1.1));
  EXPECT_TRUE(hasDelayValue(max_fall_delays_ns, 4.4));

  Sta::destroySta();
  Log::end();
}

TEST(RestoredBehaviorTest,
     negative_unate_delay_uses_output_transition_specific_load) {
  if (!Log::isInit()) {
    char config[] = "test";
    char* argv[] = {config};
    Log::init(argv);
  }
  Sta::destroySta();

  auto* ista = Sta::getOrCreateSta();
  ASSERT_NE(ista, nullptr);

  LibLibrary lib("negative_unate_transition_specific_load_test");
  lib.set_cap_unit(CapacitiveUnit::kPF);

  auto load_template = std::make_unique<LibLutTableTemplate>("load_template");
  load_template->set_template_variable1("total_output_net_capacitance");
  auto* load_template_ptr = load_template.get();
  lib.addLutTemplate(std::move(load_template));

  auto* logic_cell = addCell(
      lib, "logic_cell",
      {{"A", LibPort::LibertyPortType::kInput},
       {"Y", LibPort::LibertyPortType::kOutput}});
  auto* load_cell =
      addCell(lib, "load_cell", {{"I", LibPort::LibertyPortType::kInput}});
  ASSERT_NE(logic_cell, nullptr);
  ASSERT_NE(load_cell, nullptr);

  auto* load_port =
      dynamic_cast<LibPort*>(load_cell->get_cell_port_or_port_bus("I"));
  ASSERT_NE(load_port, nullptr);
  load_port->set_port_cap(AnalysisMode::kMax, TransType::kRise, 0.1);
  load_port->set_port_cap(AnalysisMode::kMax, TransType::kFall, 0.4);

  LibArcSet arc_set;
  auto negative_arc = makeLoadIndexedDelayArc(logic_cell, load_template_ptr,
                                              "A", "Y", "negative_unate",
                                              {5.1, 5.4}, {6.1, 6.4},
                                              {50.1, 50.4}, {60.1, 60.4});
  auto* negative_arc_ptr = negative_arc.get();
  arc_set.addLibertyArc(std::move(negative_arc));

  Instance logic_inst("u_logic", logic_cell);
  auto* src_pin =
      logic_inst.addPin("A", logic_cell->get_cell_port_or_port_bus("A"));
  auto* snk_pin =
      logic_inst.addPin("Y", logic_cell->get_cell_port_or_port_bus("Y"));
  ASSERT_NE(src_pin, nullptr);
  ASSERT_NE(snk_pin, nullptr);

  Instance load_inst("u_load", load_cell);
  auto* load_pin =
      load_inst.addPin("I", load_cell->get_cell_port_or_port_bus("I"));
  ASSERT_NE(load_pin, nullptr);

  Net output_net("logic_y");
  output_net.addPinPort(snk_pin);
  output_net.addPinPort(load_pin);

  StaVertex src_vertex(src_pin);
  StaVertex snk_vertex(snk_pin);
  src_vertex.initSlewData(NS_TO_FS(0.02), true);
  auto* src_rise_slew =
      src_vertex.getSlewData(AnalysisMode::kMax, TransType::kRise, nullptr);
  ASSERT_NE(src_rise_slew, nullptr);

  StaInstArc inst_arc(&src_vertex, &snk_vertex, negative_arc_ptr, &arc_set,
                      &logic_inst);

  StaDelayPropagation delay_propagation;
  EXPECT_EQ(delay_propagation(&inst_arc), 1U);

  std::vector<double> max_fall_delays_ns;
  StaArc* inst_arc_ptr = &inst_arc;
  StaData* arc_delay_data = nullptr;
  FOREACH_ARC_DELAY_DATA(inst_arc_ptr, arc_delay_data) {
    if (arc_delay_data->get_delay_type() != AnalysisMode::kMax ||
        arc_delay_data->get_trans_type() != TransType::kFall) {
      continue;
    }

    max_fall_delays_ns.push_back(FS_TO_NS(
        dynamic_cast<StaArcDelayData*>(arc_delay_data)->get_arc_delay()));
  }

  EXPECT_TRUE(hasDelayValue(max_fall_delays_ns, 6.4));

  Sta::destroySta();
  Log::end();
}

TEST_F(RestoredStaBehaviorTest, report_timing_data_returns_runtime_payload) {
  auto* timing_engine = _timing_engine;
  ASSERT_NE(timing_engine, nullptr);
  auto* ista = timing_engine->get_ista();
  ASSERT_NE(ista, nullptr);

  const auto timing_data = ista->reportTimingData(1);
  EXPECT_FALSE(timing_data.empty());
}

TEST_F(RestoredStaBehaviorTest, report_wire_paths_cleans_stale_files_and_writes_report) {
  auto* timing_engine = _timing_engine;
  ASSERT_NE(timing_engine, nullptr);
  auto* ista = timing_engine->get_ista();
  ASSERT_NE(ista, nullptr);

  const fs::path design_workspace = ista->get_design_work_space();
  const fs::path wire_path_dir = design_workspace / "wire_paths";
  std::error_code ec;
  fs::create_directories(wire_path_dir, ec);
  ASSERT_FALSE(ec) << "failed to create wire path dir: " << wire_path_dir
                   << ", error=" << ec.message();

  const fs::path stale_file = wire_path_dir / "stale.txt";
  {
    std::ofstream output(stale_file);
    output << "stale";
  }
  ASSERT_TRUE(fs::exists(stale_file));

  EXPECT_EQ(ista->reportWirePaths(), 1U);
  EXPECT_FALSE(fs::exists(stale_file));

  const fs::path report_path =
      design_workspace / (ista->get_design_name() + ".rpt");
  ASSERT_TRUE(fs::exists(report_path));
  EXPECT_GT(fs::file_size(report_path), 0U);
}

TEST_F(RestoredStaBehaviorTest, json_report_mode_writes_sidecar_file) {
  auto* timing_engine = _timing_engine;
  ASSERT_NE(timing_engine, nullptr);
  auto* ista = timing_engine->get_ista();
  ASSERT_NE(ista, nullptr);

  const fs::path design_workspace = ista->get_design_work_space();
  const fs::path report_path = design_workspace / "json_report_restore.rpt";
  const fs::path json_report_path = design_workspace / "json_report_restore.rpt.json";

  std::error_code ec;
  fs::remove(report_path, ec);
  ec.clear();
  fs::remove(json_report_path, ec);
  ec.clear();

  ista->enableJsonReport();
  EXPECT_EQ(ista->reportPath(report_path.c_str(), false), 1U);

  ASSERT_TRUE(fs::exists(report_path));
  ASSERT_TRUE(fs::exists(json_report_path))
      << "expected reportPath() to keep writing the JSON sidecar when JSON "
         "mode is enabled";

  const auto json_text = readText(json_report_path);
  EXPECT_NE(json_text.find("\"summary\""), std::string::npos);
  EXPECT_NE(json_text.find("\"slack\""), std::string::npos);
  EXPECT_NE(json_text.find("\"detail\""), std::string::npos);
}

}  // namespace
