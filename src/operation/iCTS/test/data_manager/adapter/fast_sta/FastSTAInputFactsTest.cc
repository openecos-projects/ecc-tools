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
/**
 * @file FastSTAInputFactsTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-09-13
 * @brief Parsed Liberty and SDC facts must retain their meaning through timing evaluation.
 */

#include <gtest/gtest.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <utility>

#include "FastSTA.hh"
#include "FastSTAConstraints.hh"
#include "FastSTAEvents.hh"
#include "IdbLayout.h"
#include "SDCClockReader.hh"
#include "clock_state/FastSTAClockState.hh"
#include "idm.h"
#include "io/Wrapper.hh"
#include "liberty/FastSTALiberty.hh"
#include "liberty/Lib.hh"

namespace icts_test {
namespace {

class ScopedTimingInput
{
 public:
  explicit ScopedTimingInput(const std::string& text)
  {
    auto name = (std::filesystem::temp_directory_path() / "icts-input-facts-XXXXXX").string();
    const auto descriptor = mkstemp(name.data());
    if (descriptor >= 0) {
      close(descriptor);
      _path = std::move(name);
      std::ofstream stream(_path);
      stream << text;
      _ready = stream.good();
    }
  }
  ~ScopedTimingInput()
  {
    std::error_code error;
    std::filesystem::remove(_path, error);
  }
  auto path() const -> const std::string& { return _path; }
  auto ready() const -> bool { return _ready; }

 private:
  std::string _path;
  bool _ready = false;
};

auto ReplaceInput(std::string text, const std::string& from, const std::string& to) -> std::string
{
  const auto position = text.find(from);
  EXPECT_NE(position, std::string::npos);
  if (position != std::string::npos) {
    text.replace(position, from.size(), to);
  }
  return text;
}

class FastSTAInputFactsTestInterface : public ::testing::Test
{
 protected:
  void SetUp() override
  {
    std::ifstream stream(std::filesystem::path(__FILE__).parent_path() / "FastSTAContracts.lib");
    ASSERT_TRUE(stream.good());
    _library.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    _layout.get_units()->set_microns_dbu(1000);
    auto* master = _layout.get_cell_master_list()->set_cell_master("GOODBUF");
    master->set_width(1000U);
    master->set_height(1000U);
    _wrapper.set_idb_layout(&_layout);
  }

  auto loadModel(const std::string& text) -> std::optional<icts::FastStaLibertyCell>
  {
    const ScopedTimingInput input(text);
    if (!input.ready()) {
      ADD_FAILURE() << "Could not write Liberty input";
      return std::nullopt;
    }
    dmInst->get_config().set_lib_paths({input.path()});
    if (!dmInst->readLib({input.path()})) {
      ADD_FAILURE() << "Could not parse Liberty input";
      return std::nullopt;
    }
    return icts::FastStaLiberty::extractBufferCell(_wrapper, "GOODBUF");
  }

  std::string _library;
  idb::IdbLayout _layout;
  icts::Wrapper _wrapper;
};

TEST_F(FastSTAInputFactsTestInterface, ParsedLeakageDistinguishesMissingZeroPositiveAndInvalid)
{
  const auto zero = loadModel(_library);
  if (!zero.has_value()) {
    FAIL() << "Expected zero to have a value.";
  }
  if (!zero->leakage_power_w.has_value()) {
    FAIL() << "Expected zero->leakage_power_w to have a value.";
  }
  EXPECT_DOUBLE_EQ(*zero->leakage_power_w, 0.0);
  EXPECT_TRUE(_wrapper.findLibertyCell("GOODBUF")->has_cell_leakage_power());

  const auto missing = loadModel(ReplaceInput(_library, "cell_leakage_power : 0;", ""));
  if (!missing.has_value()) {
    FAIL() << "Expected missing to have a value.";
  }
  EXPECT_FALSE(missing->leakage_power_w.has_value());
  EXPECT_FALSE(_wrapper.findLibertyCell("GOODBUF")->has_cell_leakage_power());

  const auto positive = loadModel(ReplaceInput(_library, "cell_leakage_power : 0;", "cell_leakage_power : 2000000;"));
  if (!positive.has_value()) {
    FAIL() << "Expected positive to have a value.";
  }
  if (!positive->leakage_power_w.has_value()) {
    FAIL() << "Expected positive->leakage_power_w to have a value.";
  }
  EXPECT_DOUBLE_EQ(*positive->leakage_power_w, 0.002);

  const auto invalid = loadModel(ReplaceInput(_library, "cell_leakage_power : 0;", "cell_leakage_power : -1; leakage_power() { value : 0; }"));
  if (!invalid.has_value()) {
    FAIL() << "Expected invalid to have a value.";
  }
  EXPECT_FALSE(invalid->leakage_power_w.has_value());

  const auto group = loadModel(ReplaceInput(_library, "cell_leakage_power : 0;", "leakage_power() { value : 0; }"));
  if (!group.has_value()) {
    FAIL() << "Expected group to have a value.";
  }
  if (!group->leakage_power_w.has_value()) {
    FAIL() << "Expected group->leakage_power_w to have a value.";
  }
  EXPECT_DOUBLE_EQ(*group->leakage_power_w, 0.0);
}

TEST(FastSTAParsedInputTest, LeakagePresenceSurvivesMove)
{
  idb::LibCell original("original", nullptr);
  EXPECT_FALSE(original.has_cell_leakage_power());
  original.set_cell_leakage_power(0.0);
  idb::LibCell moved(std::move(original));
  EXPECT_TRUE(moved.has_cell_leakage_power());
  EXPECT_DOUBLE_EQ(moved.get_cell_leakage_power(), 0.0);
  idb::LibCell assigned("assigned", nullptr);
  assigned = std::move(moved);
  EXPECT_TRUE(assigned.has_cell_leakage_power());
  EXPECT_DOUBLE_EQ(assigned.get_cell_leakage_power(), 0.0);
  idb::LibCell absent("absent", nullptr);
  assigned = std::move(absent);
  EXPECT_FALSE(assigned.has_cell_leakage_power());
}

TEST_F(FastSTAInputFactsTestInterface, ExplicitSdcUnitsCannotSupplyMissingLibertyUnits)
{
  const ScopedTimingInput sdc("set_units -time ns -capacitance pf\ncreate_clock -name clk -period 10\n");
  ASSERT_TRUE(sdc.ready());
  const auto constraints = icts::SdcClockReader(sdc.path(), {.time_unit_ns = 0.0, .capacitance_unit_pf = 0.0}).readClockData();
  ASSERT_EQ(constraints.clocks.size(), 1U);
  EXPECT_DOUBLE_EQ(constraints.clocks.front().period_ns, 10.0);
  EXPECT_FALSE(loadModel(ReplaceInput(_library, "time_unit : \"1ns\";", "")).has_value());
  EXPECT_FALSE(loadModel(ReplaceInput(_library, "capacitive_load_unit(1, pf);", "")).has_value());
  EXPECT_FALSE(loadModel(ReplaceInput(_library, "capacitive_load_unit(1, pf);", "capacitive_load_unit(1.5, pf);")).has_value());
  EXPECT_TRUE(loadModel(_library).has_value());
}

auto ParsedRelation(const std::string& commands, icts::FastStaTimingCheckKind kind, double capture_period = 10.0)
    -> std::optional<icts::FastStaTimingRelationFact>
{
  const ScopedTimingInput sdc("create_clock -name launch -period 10\ncreate_clock -name capture -period " + std::to_string(capture_period) + "\n" + commands);
  if (!sdc.ready()) {
    return std::nullopt;
  }
  icts::FastStaContext context;
  context.constraints = icts::SdcClockReader(sdc.path()).readClockData();
  EXPECT_EQ(context.constraints.clocks.size(), 2U);
  context.nodes.resize(4U);
  context.nodes.at(0U).name = "launch/CK";
  context.nodes.at(1U).name = "launch/Q";
  context.nodes.at(2U).name = "capture/CK";
  context.nodes.at(3U).name = "capture/D";
  icts::FastStaTimingPoint data{
      .arrival_ns = 1.0, .launch_node_id = 1U, .launch_clock_node_id = 0U, .valid = true, .clock_name = "launch", .exception_progress = {}};
  const icts::FastStaTimingPoint capture{.launch_clock_node_id = 2U, .valid = true, .clock_name = "capture", .exception_progress = {}};
  icts::FastStaEvents::startPath(context, data, icts::FastStaTransition::kRise);
  const icts::FastStaTimingCheck check{.data_node_id = 3U, .clock_node_id = 2U, .kind = kind};
  return icts::FastStaEvents::relation(context, check, icts::FastStaTransition::kRise, data, capture, 0.0, {});
}

TEST(FastSTAParsedInputTest, MulticycleDefaultAndDualSelectionRetainSignedHoldViolation)
{
  for (const auto* flags : {"", "-setup", "-setup -hold"}) {
    const auto command = std::string("set_multicycle_path 2 ") + flags + "\n";
    const auto setup = ParsedRelation(command, icts::FastStaTimingCheckKind::kSetup);
    const auto hold = ParsedRelation(command, icts::FastStaTimingCheckKind::kHold);
    if (!setup.has_value()) {
      FAIL() << "Expected setup to have a value.";
    }
    if (!hold.has_value()) {
      FAIL() << "Expected hold to have a value.";
    }
    EXPECT_DOUBLE_EQ(setup->required_ns, 20.0) << flags;
    EXPECT_DOUBLE_EQ(hold->required_ns, 10.0) << flags;
    EXPECT_DOUBLE_EQ(hold->slack_ns, -9.0) << flags;
  }
  const auto hold_only = ParsedRelation("set_multicycle_path 1 -hold\n", icts::FastStaTimingCheckKind::kHold);
  if (!hold_only.has_value()) {
    FAIL() << "Expected hold_only to have a value.";
  }
  EXPECT_DOUBLE_EQ(hold_only->required_ns, -10.0);
  const auto restored = ParsedRelation("set_multicycle_path 2 -setup\nset_multicycle_path 1 -hold\n", icts::FastStaTimingCheckKind::kHold);
  if (!restored.has_value()) {
    FAIL() << "Expected restored to have a value.";
  }
  EXPECT_DOUBLE_EQ(restored->required_ns, 0.0);
  EXPECT_DOUBLE_EQ(restored->slack_ns, 1.0);
}

TEST(FastSTAParsedInputTest, MulticycleStartAndEndKeepIndependentClockPeriods)
{
  const auto baseline = ParsedRelation("", icts::FastStaTimingCheckKind::kHold, 20.0);
  const auto changed = ParsedRelation("set_multicycle_path 2 -setup -end\nset_multicycle_path 1 -hold -start\n", icts::FastStaTimingCheckKind::kHold, 20.0);
  if (!baseline.has_value()) {
    FAIL() << "Expected baseline to have a value.";
  }
  if (!changed.has_value()) {
    FAIL() << "Expected changed to have a value.";
  }
  EXPECT_DOUBLE_EQ(changed->required_ns - baseline->required_ns, 10.0);
  const auto start = ParsedRelation("set_multicycle_path 2 -start\n", icts::FastStaTimingCheckKind::kHold, 20.0);
  const auto end = ParsedRelation("set_multicycle_path 2 -end\n", icts::FastStaTimingCheckKind::kHold, 20.0);
  if (!start.has_value()) {
    FAIL() << "Expected start to have a value.";
  }
  if (!end.has_value()) {
    FAIL() << "Expected end to have a value.";
  }
  EXPECT_DOUBLE_EQ(start->required_ns - baseline->required_ns, 10.0);
  EXPECT_DOUBLE_EQ(end->required_ns - baseline->required_ns, 20.0);
}

auto ParsedClockGraph(const std::string& commands) -> icts::FastStaContext
{
  const ScopedTimingInput sdc("create_clock -name A -period 10 [get_ports clkA]\ncreate_clock -name B -period 20 [get_ports clkB]\n" + commands);
  EXPECT_TRUE(sdc.ready());
  icts::FastStaContext context;
  context.constraints = icts::SdcClockReader(sdc.path()).readClockData();
  EXPECT_TRUE(context.constraints.ok());
  for (const auto* name : {"clkA", "clkB", "out", "branch", "nested", "inst/A"}) {
    icts::FastStaNode node;
    node.name = name;
    node.top_level = node.name != "inst/A";
    node.input = node.name == "clkA" || node.name == "clkB" || !node.top_level;
    node.output = !node.input;
    node.domain = icts::FastStaNodeDomain::kLogic;
    node.kind = node.input ? icts::FastStaNodeKind::kSource : icts::FastStaNodeKind::kSink;
    node.input_cap_profile_available = true;
    context.node_id_by_name[node.name] = context.nodes.size();
    context.nodes.push_back(std::move(node));
  }
  context.nodes.at(1U).output_net_ids = {0U};
  context.nodes.at(2U).incoming_net_id = 0U;
  context.nodes.at(3U).incoming_net_id = 0U;
  context.nodes.at(2U).output_net_ids = {1U};
  context.nodes.at(4U).incoming_net_id = 1U;
  context.nets = {{.name = "root", .driver_node_id = 1U, .load_node_ids = {2U, 3U}, .load_rc_node_ids = {}, .driver_timing_by_state = {}},
                  {.name = "child", .driver_node_id = 2U, .load_node_ids = {4U}, .load_rc_node_ids = {}, .driver_timing_by_state = {}}};
  return context;
}

TEST(FastSTAParsedInputTest, GeneratedClockRequiresItsActualMasterAndSourcePath)
{
  const auto correct = std::string("create_generated_clock -name G -source [get_ports clkB] -master_clock B -divide_by 2 [get_ports out]\n");
  auto context = ParsedClockGraph(correct);
  const auto good_error = icts::FastStaConstraints::prepare(context);
  ASSERT_FALSE(good_error.has_value()) << good_error.value_or("");
  const auto* generated = icts::FastStaConstraints::clock(context, "G");
  ASSERT_NE(generated, nullptr);
  EXPECT_DOUBLE_EQ(generated->period_ns, 40.0);
  EXPECT_EQ(context.nodes.at(2U).clock_name, "G");

  auto wrong_master = ParsedClockGraph(ReplaceInput(correct, "-master_clock B", "-master_clock A"));
  const auto master_error = icts::FastStaConstraints::prepare(wrong_master);
  if (!master_error.has_value()) {
    FAIL() << "Expected master_error to have a value.";
  }
  EXPECT_EQ(*master_error, "generated_clock_source_master_mismatch:G");

  auto wrong_branch = ParsedClockGraph(ReplaceInput(correct, "[get_ports clkB]", "[get_ports branch]"));
  const auto path_error = icts::FastStaConstraints::prepare(wrong_branch);
  if (!path_error.has_value()) {
    FAIL() << "Expected path_error to have a value.";
  }
  EXPECT_EQ(*path_error, "generated_clock_target_not_reached_from_source:G:out");

  auto no_clock = ParsedClockGraph(ReplaceInput(correct, "[get_ports clkB]", "[get_pins inst/A]"));
  const auto missing_error = icts::FastStaConstraints::prepare(no_clock);
  if (!missing_error.has_value()) {
    FAIL() << "Expected missing_error to have a value.";
  }
  EXPECT_EQ(*missing_error, "generated_clock_source_unavailable:G");

  auto implicit = ParsedClockGraph(ReplaceInput(correct, "-master_clock B ", ""));
  const auto implicit_error = icts::FastStaConstraints::prepare(implicit);
  ASSERT_FALSE(implicit_error.has_value()) << implicit_error.value_or("");
  EXPECT_EQ(icts::FastStaConstraints::clock(implicit, "G")->master_clock_name, "B");

  auto nested = ParsedClockGraph(correct + "create_generated_clock -name H -source [get_ports out] -master_clock G -divide_by 2 [get_ports nested]\n");
  const auto nested_error = icts::FastStaConstraints::prepare(nested);
  ASSERT_FALSE(nested_error.has_value()) << nested_error.value_or("");
  EXPECT_DOUBLE_EQ(icts::FastStaConstraints::clock(nested, "H")->period_ns, 80.0);
  EXPECT_EQ(nested.nodes.at(4U).clock_name, "H");
}

TEST_F(FastSTAInputFactsTestInterface, PrimaryClockSourcesKeepTheirSeedsAcrossUpstreamMasterEdits)
{
  const auto good = loadModel(_library);
  if (!good.has_value()) {
    FAIL() << "Expected good to have a value.";
  }
  auto* slow_master = _layout.get_cell_master_list()->set_cell_master("SLOWBUF");
  slow_master->set_width(1000U);
  slow_master->set_height(1000U);
  const auto slow = icts::FastStaLiberty::extractBufferCell(_wrapper, "SLOWBUF");
  if (!slow.has_value()) {
    FAIL() << "Expected slow to have a value.";
  }
  const auto make_graph = [&](const std::string& master) -> icts::WrapperTimingGraph {
    const auto& upstream = master == "GOODBUF" ? *good : *slow;
    icts::WrapperTimingGraph graph;
    graph.status = icts::WrapperTimingGraphStatus::kComplete;
    graph.nodes = {{.pin_name = "clk", .input = true, .top_level = true},
                   {.pin_name = "up/A",
                    .inst_name = "up",
                    .cell_master = master,
                    .x_dbu = 1000,
                    .input_cap_pf = upstream.input_cap_pf,
                    .input_cap_pf_by_timing = upstream.input_cap_pf_by_timing,
                    .input = true,
                    .port_name = "A",
                    .input_cap_profile_available = true},
                   {.pin_name = "up/Y", .inst_name = "up", .cell_master = master, .x_dbu = 1000, .output = true, .port_name = "Y", .logic_function = "A"},
                   {.pin_name = "buf/A",
                    .inst_name = "buf",
                    .cell_master = "GOODBUF",
                    .x_dbu = 2000,
                    .input_cap_pf = good->input_cap_pf,
                    .input_cap_pf_by_timing = good->input_cap_pf_by_timing,
                    .input = true,
                    .port_name = "A",
                    .input_cap_profile_available = true},
                   {.pin_name = "buf/Y", .inst_name = "buf", .cell_master = "GOODBUF", .x_dbu = 2000, .output = true, .port_name = "Y", .logic_function = "A"},
                   {.pin_name = "sink",
                    .x_dbu = 3000,
                    .input_cap_pf = .01,
                    .input_cap_pf_by_timing = {{{.01, .01}, {.01, .01}}},
                    .output = true,
                    .top_level = true,
                    .input_cap_profile_available = true}};
    graph.nets = {{.net_name = "root",
                   .driver_pin = "clk",
                   .load_pins = {"up/A"},
                   .rc_segments = {{.begin_x_dbu = 0, .end_x_dbu = 1000, .resistance_ohm = 10, .capacitance_pf = .01}}},
                  {.net_name = "middle",
                   .driver_pin = "up/Y",
                   .load_pins = {"buf/A"},
                   .rc_segments = {{.begin_x_dbu = 1000, .end_x_dbu = 2000, .resistance_ohm = 10, .capacitance_pf = .01}}},
                  {.net_name = "leaf",
                   .driver_pin = "buf/Y",
                   .load_pins = {"sink"},
                   .rc_segments = {{.begin_x_dbu = 2000, .end_x_dbu = 3000, .resistance_ohm = 10, .capacitance_pf = .01}}}};
    graph.arcs
        = {{.inst_name = "up", .cell_master = master, .input_port = "A", .output_port = "Y", .input_pin = "up/A", .output_pin = "up/Y", .positive_unate = true},
           {.inst_name = "buf",
            .cell_master = "GOODBUF",
            .input_port = "A",
            .output_port = "Y",
            .input_pin = "buf/A",
            .output_pin = "buf/Y",
            .positive_unate = true}};
    return graph;
  };
  // Output targets cut a cell arc; input targets cut a net edge while retaining
  // the target input's own buffer propagation. Both are declared primary clocks.
  for (const auto* source_pin : {"buf/Y", "buf/A"}) {
    SCOPED_TRACE(source_pin);
    const ScopedTimingInput sdc(std::string("create_clock -name ROOT -period 10 [get_ports clk]\n") + "create_clock -name SECOND -period 20 [get_pins "
                                + source_pin + "]\n" + "set_clock_latency -source 0.7 [get_clocks SECOND]\n" + "set_clock_transition 0.12 [get_clocks SECOND]\n"
                                + "set_propagated_clock [get_clocks {ROOT SECOND}]\n");
    ASSERT_TRUE(sdc.ready());
    const auto constraints = icts::SdcClockReader(sdc.path()).readClockData();
    ASSERT_TRUE(constraints.ok());
    ASSERT_EQ(constraints.clocks.size(), 2U);
    const auto primary_clock = std::ranges::find(constraints.clocks, std::string{"SECOND"}, &icts::SdcClockDecl::clock_name);
    ASSERT_NE(primary_clock, constraints.clocks.end());
    ASSERT_EQ(primary_clock->waveform_ns.size(), 2U);
    icts::FastSTA authority;
    authority.bindEnvironment(
        {.wrapper = &_wrapper, .dbu_per_um = 1000, .routing_layer = 1, .root_input_slew_ns = .03, .worker_count = icts::Wrapper::queryParallelWorkerCount()});
    const auto graph = make_graph("GOODBUF");
    const auto cold_graph = make_graph("SLOWBUF");
    const auto original = authority.buildContext({.timing_graph = &graph, .constraints = &constraints, .require_power = true});
    if (!original.context_id.has_value()) {
      FAIL() << original.failure_reason;
    }
    const auto cold = authority.buildContext({.timing_graph = &cold_graph, .constraints = &constraints, .require_power = true});
    if (!cold.context_id.has_value()) {
      FAIL() << cold.failure_reason;
    }
    const auto before = authority.collectTimingPointFacts(*original.context_id);
    std::size_t source_states = 0U;
    for (const auto& point : before) {
      if (point.pin_name == source_pin) {
        ++source_states;
        EXPECT_EQ(point.launch_pin_name, source_pin);
        const auto event_index = point.transition == icts::FastStaTransition::kRise ? 0U : 1U;
        EXPECT_DOUBLE_EQ(point.arrival_ns, .7 + primary_clock->waveform_ns.at(event_index));
        EXPECT_DOUBLE_EQ(point.slew_ns, .12);
      }
    }
    EXPECT_EQ(source_states, 4U);
    const auto compare_points = [](const auto& expected, const auto& actual) -> void {
      ASSERT_EQ(expected.size(), actual.size());
      for (const auto& point : expected) {
        const auto found = std::ranges::find_if(actual, [&](const auto& other) -> bool {
          return point.pin_name == other.pin_name && point.transition == other.transition && point.analysis == other.analysis;
        });
        ASSERT_NE(found, actual.end()) << point.pin_name;
        EXPECT_EQ(point.launch_pin_name, found->launch_pin_name);
        EXPECT_DOUBLE_EQ(point.arrival_ns, found->arrival_ns) << point.pin_name;
        EXPECT_DOUBLE_EQ(point.slew_ns, found->slew_ns) << point.pin_name;
      }
    };
    const auto pending = authority.beginContextTransaction(*original.context_id);
    if (!pending.context_id.has_value()) {
      FAIL() << pending.failure_reason;
    }
    ASSERT_TRUE(authority.changeInstanceMasters(*pending.context_id, {{.inst_name = "up", .cell_master = "SLOWBUF"}}));
    const auto updated = authority.collectTimingPointFacts(*pending.context_id);
    compare_points(authority.collectTimingPointFacts(*cold.context_id), updated);
    for (const auto& point : before) {
      if (point.pin_name != source_pin && point.pin_name != "sink") {
        continue;
      }
      const auto found = std::ranges::find_if(updated, [&](const auto& other) -> bool {
        return point.pin_name == other.pin_name && point.transition == other.transition && point.analysis == other.analysis;
      });
      ASSERT_NE(found, updated.end());
      EXPECT_DOUBLE_EQ(point.arrival_ns, found->arrival_ns);
      EXPECT_DOUBLE_EQ(point.slew_ns, found->slew_ns);
      EXPECT_EQ(found->launch_pin_name, source_pin);
    }
    EXPECT_GT(*authority.queryClockNodeArrival(*pending.context_id, 2U), *authority.queryClockNodeArrival(*original.context_id, 2U));
    compare_points(before, authority.collectTimingPointFacts(*original.context_id));
    ASSERT_TRUE(authority.discardContextTransaction(*pending.context_id));
    compare_points(before, authority.collectTimingPointFacts(*original.context_id));
  }
}

TEST(FastSTAParsedInputTest, LoadTargetsMustReceiveTheRequestedCapacitance)
{
  auto supported = ParsedClockGraph("set_load 0.25 [get_ports out]\n");
  const auto error = icts::FastStaConstraints::prepare(supported);
  ASSERT_FALSE(error.has_value()) << error.value_or("");
  for (const auto& analysis : supported.nodes.at(2U).input_cap_pf_by_timing) {
    for (const auto cap : analysis) {
      EXPECT_DOUBLE_EQ(cap, 0.25);
    }
  }
  for (const auto* selection : {"[get_clocks A]", "[get_pins inst/A]", "[get_ports clkA]", "[get_ports {out clkA}]", "*"}) {
    auto unsupported = ParsedClockGraph(std::string("set_load 0.25 ") + selection + "\n");
    const auto unsupported_error = icts::FastStaConstraints::prepare(unsupported);
    if (!unsupported_error.has_value()) {
      FAIL() << "Expected unsupported_error to have a value." << selection;
    }
    EXPECT_EQ(unsupported_error->find("unsupported_sdc_field:load."), 0U) << *unsupported_error;
    EXPECT_DOUBLE_EQ(unsupported.nodes.at(2U).input_cap_pf, 0.0);
  }
}

}  // namespace
}  // namespace icts_test
