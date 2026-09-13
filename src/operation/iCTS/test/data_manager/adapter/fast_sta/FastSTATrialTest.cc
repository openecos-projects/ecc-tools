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
 * @file FastSTATrialTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-09-13
 * @brief Clock trial electrical/power equivalence and exact snapshot restoration.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include "FastSTA.hh"
#include "IdbLayout.h"
#include "clock_sizing/FastSTAIncremental.hh"
#include "clock_state/FastSTABuilder.hh"
#include "design/Clock.hh"
#include "design/Inst.hh"
#include "design/Net.hh"
#include "design/Pin.hh"
#include "idm.h"
#include "io/Wrapper.hh"
#include "liberty/FastSTALiberty.hh"
#include "liberty/FastSTALibertyModel.hh"
#include "power/FastSTAPower.hh"
#include "timing/FastSTATiming.hh"

namespace icts_test {
namespace {

struct PhysicalOwner
{
  explicit PhysicalOwner(const std::string& master)
      : buffer("buf", master, icts::InstType::kBuffer, {1000, 0}),
        downstream("gen", "CHARBUF", icts::InstType::kBoundaryLoad, {2000, 0}),
        source("clk", icts::PinType::kIn, {0, 0}, nullptr, nullptr, true),
        input("A", icts::PinType::kIn, {1000, 0}, &buffer),
        output("Y", icts::PinType::kOut, {1000, 0}, &buffer),
        boundary("A", icts::PinType::kIn, {2000, 0}, &downstream),
        root("root"),
        leaf("leaf"),
        clock("ROOT", "root")
  {
    buffer.set_pins({&input, &output});
    downstream.set_pins({&boundary});
    root.set_driver(&source);
    root.set_loads({&input});
    leaf.set_driver(&output);
    leaf.set_loads({&boundary});
    source.set_net(&root);
    input.set_net(&root);
    output.set_net(&leaf);
    boundary.set_net(&leaf);
    clock.set_clock_source(&source);
    clock.set_clock_source_net(&root);
    clock.set_clock_period_ns(1.0);
    clock.add_load(&boundary);
    clock.add_net(&root);
    clock.add_net(&leaf);
    EXPECT_TRUE(clock.addPropagationArc({.inst = &buffer, .input_pin = &input, .output_pin = &output}).ok());
  }

  icts::Inst buffer;
  icts::Inst downstream;
  icts::Pin source;
  icts::Pin input;
  icts::Pin output;
  icts::Pin boundary;
  icts::Net root;
  icts::Net leaf;
  icts::Clock clock;
};

auto ExpectClockFactsEqual(const icts::FastSTA& authority, icts::FastStaContextId actual_id, icts::FastStaContextId expected_id, bool skew_available = true)
    -> void
{
  const auto actual_points = authority.collectClockPointFacts(actual_id);
  const auto expected_points = authority.collectClockPointFacts(expected_id);
  ASSERT_FALSE(actual_points.empty());
  ASSERT_EQ(actual_points.size(), expected_points.size());
  for (std::size_t index = 0U; index < actual_points.size(); ++index) {
    const auto& actual = actual_points.at(index);
    const auto& expected = expected_points.at(index);
    EXPECT_EQ(actual.pin_name, expected.pin_name);
    EXPECT_EQ(actual.kind, expected.kind);
    EXPECT_DOUBLE_EQ(actual.arrival_ns, expected.arrival_ns);
    EXPECT_DOUBLE_EQ(actual.slew_ns, expected.slew_ns);
  }
  const auto actual_scope = authority.queryClockElectricalScope(actual_id);
  const auto expected_scope = authority.queryClockElectricalScope(expected_id);
  if (!actual_scope.has_value()) {
    FAIL() << "Expected actual_scope to have a value.";
  }
  if (!expected_scope.has_value()) {
    FAIL() << "Expected expected_scope to have a value.";
  }
  ASSERT_EQ(actual_scope->node_ids, expected_scope->node_ids);
  ASSERT_EQ(actual_scope->net_ids, expected_scope->net_ids);
  for (const auto node_id : actual_scope->node_ids) {
    const auto actual = authority.querySlewStatus(actual_id, node_id);
    const auto expected = authority.querySlewStatus(expected_id, node_id);
    if (!actual.has_value()) {
      FAIL() << "Expected actual to have a value.";
    }
    if (!expected.has_value()) {
      FAIL() << "Expected expected to have a value.";
    }
    EXPECT_EQ(actual->node_name, expected->node_name);
    EXPECT_EQ(actual->role, expected->role);
    EXPECT_EQ(actual->violated, expected->violated);
    EXPECT_EQ(actual->constraint_available, expected->constraint_available);
    EXPECT_DOUBLE_EQ(actual->slew_ns, expected->slew_ns);
    EXPECT_DOUBLE_EQ(actual->max_slew_ns, expected->max_slew_ns);
  }
  for (const auto net_id : actual_scope->net_ids) {
    const auto actual = authority.queryCapStatus(actual_id, net_id);
    const auto expected = authority.queryCapStatus(expected_id, net_id);
    if (!actual.has_value()) {
      FAIL() << "Expected actual to have a value.";
    }
    if (!expected.has_value()) {
      FAIL() << "Expected expected to have a value.";
    }
    EXPECT_EQ(actual->net_name, expected->net_name);
    EXPECT_EQ(actual->violated, expected->violated);
    EXPECT_EQ(actual->constraint_available, expected->constraint_available);
    EXPECT_DOUBLE_EQ(actual->load_cap_pf, expected->load_cap_pf);
    EXPECT_DOUBLE_EQ(actual->max_cap_pf, expected->max_cap_pf);
  }
  const auto actual_caps = authority.collectPinCapacitanceFacts(actual_id);
  const auto expected_caps = authority.collectPinCapacitanceFacts(expected_id);
  ASSERT_EQ(actual_caps.size(), expected_caps.size());
  for (std::size_t index = 0U; index < actual_caps.size(); ++index) {
    EXPECT_EQ(actual_caps.at(index).pin_name, expected_caps.at(index).pin_name);
    EXPECT_EQ(actual_caps.at(index).capacitance_pf, expected_caps.at(index).capacitance_pf);
  }
  const auto actual_rc = authority.collectPiElmoreFacts(actual_id);
  const auto expected_rc = authority.collectPiElmoreFacts(expected_id);
  ASSERT_FALSE(actual_rc.empty());
  ASSERT_EQ(actual_rc.size(), expected_rc.size());
  for (std::size_t index = 0U; index < actual_rc.size(); ++index) {
    const auto& actual = actual_rc.at(index);
    const auto& expected = expected_rc.at(index);
    EXPECT_EQ(actual.net_name, expected.net_name);
    EXPECT_EQ(actual.driver_pin_name, expected.driver_pin_name);
    EXPECT_EQ(actual.load_pin_name, expected.load_pin_name);
    EXPECT_EQ(actual.analysis, expected.analysis);
    EXPECT_EQ(actual.transition, expected.transition);
    EXPECT_DOUBLE_EQ(actual.near_cap_pf, expected.near_cap_pf);
    EXPECT_DOUBLE_EQ(actual.far_cap_pf, expected.far_cap_pf);
    EXPECT_DOUBLE_EQ(actual.resistance_ohm, expected.resistance_ohm);
    EXPECT_DOUBLE_EQ(actual.elmore_ns, expected.elmore_ns);
  }
  const auto actual_skew = authority.querySkew(actual_id);
  const auto expected_skew = authority.querySkew(expected_id);
  ASSERT_EQ(actual_skew.valid, skew_available);
  ASSERT_EQ(expected_skew.valid, skew_available);
  EXPECT_EQ(actual_skew.min_sink_node_id, expected_skew.min_sink_node_id);
  EXPECT_EQ(actual_skew.max_sink_node_id, expected_skew.max_sink_node_id);
  EXPECT_EQ(actual_skew.min_sink_name, expected_skew.min_sink_name);
  EXPECT_EQ(actual_skew.max_sink_name, expected_skew.max_sink_name);
  EXPECT_DOUBLE_EQ(actual_skew.skew_ns, expected_skew.skew_ns);
  EXPECT_DOUBLE_EQ(actual_skew.min_arrival_ns, expected_skew.min_arrival_ns);
  EXPECT_DOUBLE_EQ(actual_skew.max_arrival_ns, expected_skew.max_arrival_ns);
  const auto actual_power = authority.queryPower(actual_id);
  const auto expected_power = authority.queryPower(expected_id);
  if (!actual_power.has_value()) {
    FAIL() << "Expected actual_power to have a value.";
  }
  if (!expected_power.has_value()) {
    FAIL() << "Expected expected_power to have a value.";
  }
  EXPECT_DOUBLE_EQ(actual_power->area_um2, expected_power->area_um2);
  EXPECT_DOUBLE_EQ(actual_power->internal_power_w, expected_power->internal_power_w);
  EXPECT_DOUBLE_EQ(actual_power->leakage_power_w, expected_power->leakage_power_w);
  EXPECT_DOUBLE_EQ(actual_power->switching_power_w, expected_power->switching_power_w);
  EXPECT_DOUBLE_EQ(actual_power->total_power_w, expected_power->total_power_w);
  EXPECT_EQ(actual_power->unknown_gate_activity_count, expected_power->unknown_gate_activity_count);
}

auto ExpectTimingPointEqual(const icts::FastStaTimingPoint& actual, const icts::FastStaTimingPoint& expected) -> void
{
  EXPECT_EQ(actual.valid, expected.valid);
  EXPECT_EQ(actual.clock_name, expected.clock_name);
  EXPECT_EQ(actual.launch_node_id, expected.launch_node_id);
  EXPECT_EQ(actual.launch_clock_node_id, expected.launch_clock_node_id);
  EXPECT_EQ(actual.launch_clock_transition, expected.launch_clock_transition);
  EXPECT_EQ(actual.launch_data_transition, expected.launch_data_transition);
  EXPECT_EQ(actual.driver_model_index, expected.driver_model_index);
  EXPECT_EQ(actual.driver_arc_variant_index, expected.driver_arc_variant_index);
  EXPECT_EQ(actual.slew_driver_model_index, expected.slew_driver_model_index);
  EXPECT_EQ(actual.slew_driver_arc_variant_index, expected.slew_driver_arc_variant_index);
  EXPECT_EQ(actual.driver_is_launch, expected.driver_is_launch);
  EXPECT_EQ(actual.slew_driver_is_launch, expected.slew_driver_is_launch);
  EXPECT_EQ(actual.stage_input_node_id, expected.stage_input_node_id);
  EXPECT_EQ(actual.stage_input_transition, expected.stage_input_transition);
  EXPECT_EQ(actual.exception_progress, expected.exception_progress);
  EXPECT_DOUBLE_EQ(actual.arrival_ns, expected.arrival_ns);
  EXPECT_DOUBLE_EQ(actual.slew_ns, expected.slew_ns);
  EXPECT_DOUBLE_EQ(actual.launch_clock_arrival_ns, expected.launch_clock_arrival_ns);
  EXPECT_DOUBLE_EQ(actual.driver_input_slew_ns, expected.driver_input_slew_ns);
  EXPECT_DOUBLE_EQ(actual.slew_driver_input_slew_ns, expected.slew_driver_input_slew_ns);
  EXPECT_DOUBLE_EQ(actual.stage_input_arrival_ns, expected.stage_input_arrival_ns);
  EXPECT_DOUBLE_EQ(actual.stage_input_slew_ns, expected.stage_input_slew_ns);
  EXPECT_DOUBLE_EQ(actual.stage_delay_ns, expected.stage_delay_ns);
}

auto ExpectDmpDriverEqual(const icts::FastStaDmpDriverResult& actual, const icts::FastStaDmpDriverResult& expected) -> void
{
  EXPECT_EQ(actual.valid, expected.valid);
  EXPECT_EQ(actual.driver_waveform_valid, expected.driver_waveform_valid);
  EXPECT_EQ(actual.algorithm, expected.algorithm);
  EXPECT_EQ(actual.transition, expected.transition);
  EXPECT_EQ(actual.driver_library_name, expected.driver_library_name);
  EXPECT_EQ(actual.driver_cell_master, expected.driver_cell_master);
  EXPECT_DOUBLE_EQ(actual.ceff_pf, expected.ceff_pf);
  EXPECT_DOUBLE_EQ(actual.gate_delay_ns, expected.gate_delay_ns);
  EXPECT_DOUBLE_EQ(actual.driver_slew_ns, expected.driver_slew_ns);
  EXPECT_DOUBLE_EQ(actual.driver_waveform_delay_ns, expected.driver_waveform_delay_ns);
  EXPECT_DOUBLE_EQ(actual.ramp_start_ns, expected.ramp_start_ns);
  EXPECT_DOUBLE_EQ(actual.ramp_duration_ns, expected.ramp_duration_ns);
  EXPECT_DOUBLE_EQ(actual.near_cap_pf, expected.near_cap_pf);
  EXPECT_DOUBLE_EQ(actual.far_cap_pf, expected.far_cap_pf);
  EXPECT_DOUBLE_EQ(actual.rpi_ns_per_pf, expected.rpi_ns_per_pf);
  EXPECT_DOUBLE_EQ(actual.rd_ns_per_pf, expected.rd_ns_per_pf);
  EXPECT_DOUBLE_EQ(actual.input_threshold, expected.input_threshold);
  EXPECT_DOUBLE_EQ(actual.output_threshold, expected.output_threshold);
  EXPECT_DOUBLE_EQ(actual.slew_lower_threshold, expected.slew_lower_threshold);
  EXPECT_DOUBLE_EQ(actual.slew_upper_threshold, expected.slew_upper_threshold);
  EXPECT_DOUBLE_EQ(actual.slew_derate, expected.slew_derate);
  EXPECT_DOUBLE_EQ(actual.pole1_per_ns, expected.pole1_per_ns);
  EXPECT_DOUBLE_EQ(actual.pole2_per_ns, expected.pole2_per_ns);
  EXPECT_DOUBLE_EQ(actual.zero1_per_ns, expected.zero1_per_ns);
  EXPECT_DOUBLE_EQ(actual.waveform_scale, expected.waveform_scale);
  EXPECT_DOUBLE_EQ(actual.waveform_offset, expected.waveform_offset);
  EXPECT_DOUBLE_EQ(actual.waveform_slope, expected.waveform_slope);
  EXPECT_DOUBLE_EQ(actual.first_pole_weight, expected.first_pole_weight);
  EXPECT_DOUBLE_EQ(actual.second_pole_weight, expected.second_pole_weight);
}

class FastSTATrialTestInterface : public ::testing::Test
{
 protected:
  void SetUp() override
  {
    const auto directory = std::filesystem::path(__FILE__).parent_path();
    const std::vector<std::string> libraries{(directory / "FastSTAChar.lib").string(), (directory / "FastSTAContracts.lib").string()};
    dmInst->get_config().set_lib_paths(libraries);
    ASSERT_TRUE(dmInst->readLib(libraries));
    _layout.get_units()->set_microns_dbu(1000);
    for (const auto* name : {"CHARBUF", "SLOWBUF", "BADBUF"}) {
      auto* master = _layout.get_cell_master_list()->set_cell_master(name);
      master->set_width(std::string(name) == "SLOWBUF" ? 2000U : 1000U);
      master->set_height(1000U);
    }
    _wrapper.set_idb_layout(&_layout);
    _environment = {.wrapper = &_wrapper, .dbu_per_um = 1000, .routing_layer = 1, .root_input_slew_ns = .03};
    _authority.bindEnvironment(_environment);
    _constraints.clocks.push_back({.clock_name = "ROOT",
                                   .targets = {{.kind = icts::SdcObjectKind::kPort, .pattern = "clk"}},
                                   .generated_sources = {},
                                   .period_ns = 1.0,
                                   .period_resolved = true,
                                   .waveform_ns = {0.0, .5},
                                   .generated_edges = {},
                                   .generated_edge_shifts_ns = {},
                                   .waveform_resolved = true});
    _constraints.propagated_clocks.push_back({.kind = icts::SdcObjectKind::kClock, .pattern = "ROOT"});
  }

  auto makeGraph(const std::string& master) -> std::optional<icts::WrapperTimingGraph>
  {
    const auto cell = icts::FastStaLiberty::extractBufferCell(_wrapper, master);
    if (!cell.has_value()) {
      return std::nullopt;
    }
    icts::WrapperTimingGraph graph;
    graph.status = icts::WrapperTimingGraphStatus::kComplete;
    graph.nodes = {{.pin_name = "clk", .input = true, .top_level = true},
                   {.pin_name = "buf/A",
                    .inst_name = "buf",
                    .cell_master = master,
                    .x_dbu = 1000,
                    .input_cap_pf = cell->input_cap_pf,
                    .input_cap_pf_by_timing = cell->input_cap_pf_by_timing,
                    .input = true,
                    .port_name = "A",
                    .input_cap_profile_available = true},
                   {.pin_name = "buf/Y", .inst_name = "buf", .cell_master = master, .x_dbu = 1000, .output = true, .port_name = "Y", .logic_function = "A"},
                   {.pin_name = "sink",
                    .x_dbu = 2000,
                    .input_cap_pf = .01,
                    .input_cap_pf_by_timing = {{{.01, .02}, {.03, .04}}},
                    .max_slew_ns = .06,
                    .output = true,
                    .top_level = true,
                    .input_cap_profile_available = true}};
    graph.nets = {{.net_name = "root",
                   .driver_pin = "clk",
                   .load_pins = {"buf/A"},
                   .rc_segments = {{.begin_x_dbu = 0, .end_x_dbu = 1000, .resistance_ohm = 10.0, .capacitance_pf = .01}}},
                  {.net_name = "leaf",
                   .driver_pin = "buf/Y",
                   .load_pins = {"sink"},
                   .rc_segments = {{.begin_x_dbu = 1000, .end_x_dbu = 2000, .resistance_ohm = 20.0, .capacitance_pf = .02}}}};
    graph.arcs = {{.inst_name = "buf",
                   .cell_master = master,
                   .input_port = "A",
                   .output_port = "Y",
                   .input_pin = "buf/A",
                   .output_pin = "buf/Y",
                   .positive_unate = true}};
    return graph;
  }

  auto build(const std::string& master) -> icts::FastStaBuildResult
  {
    const auto graph = makeGraph(master);
    return graph.has_value() ? _authority.buildContext({.timing_graph = &*graph, .constraints = &_constraints, .require_power = true})
                             : icts::FastStaBuildResult{.failure_reason = "fixture_cell_unavailable"};
  }

  auto buildRaw(const std::string& master) -> icts::FastStaBuilder::BuildResult
  {
    const auto graph = makeGraph(master);
    if (!graph.has_value()) {
      return {.failure_reason = "fixture_cell_unavailable"};
    }
    auto result = icts::FastStaBuilder::buildContext(_environment, {.timing_graph = &*graph, .constraints = &_constraints, .require_power = true});
    if (result.context.has_value() && (!icts::FastStaTiming::update(*result.context) || !icts::FastStaPower::update(*result.context))) {
      return {.failure_reason = "fixture_analysis_failed"};
    }
    return result;
  }

  auto makeGeneratedGraph(const std::string& master) -> std::optional<icts::WrapperTimingGraph>
  {
    auto base = makeGraph(master);
    const auto model = icts::FastStaLiberty::extractBufferCell(_wrapper, "CHARBUF");
    if (!base.has_value() || !model.has_value()) {
      return std::nullopt;
    }
    auto graph = std::move(*base);
    const auto& cell = *model;
    auto sink = graph.nodes.back();
    sink.x_dbu = 3000;
    graph.nodes.back() = {.pin_name = "gen/A",
                          .inst_name = "gen",
                          .cell_master = "CHARBUF",
                          .x_dbu = 2000,
                          .input_cap_pf = cell.input_cap_pf,
                          .input_cap_pf_by_timing = cell.input_cap_pf_by_timing,
                          .input = true,
                          .port_name = "A",
                          .input_cap_profile_available = true};
    graph.nodes.push_back(
        {.pin_name = "gen/Y", .inst_name = "gen", .cell_master = "CHARBUF", .x_dbu = 2000, .output = true, .port_name = "Y", .logic_function = "A"});
    graph.nodes.push_back(std::move(sink));
    graph.nets.back().load_pins = {"gen/A"};
    graph.nets.push_back({.net_name = "generated_leaf",
                          .driver_pin = "gen/Y",
                          .load_pins = {"sink"},
                          .rc_segments = {{.begin_x_dbu = 2000, .end_x_dbu = 3000, .resistance_ohm = 30.0, .capacitance_pf = .03}}});
    graph.arcs.push_back({.inst_name = "gen",
                          .cell_master = "CHARBUF",
                          .input_port = "A",
                          .output_port = "Y",
                          .input_pin = "gen/A",
                          .output_pin = "gen/Y",
                          .positive_unate = true});
    return graph;
  }

  auto generatedConstraints() -> icts::SdcClockData
  {
    auto constraints = _constraints;
    constraints.clocks.push_back({.kind = icts::SdcClockDecl::Kind::kGenerated,
                                  .clock_name = "GEN",
                                  .targets = {{.kind = icts::SdcObjectKind::kPin, .pattern = "gen/Y"}},
                                  .generated_sources = {{.kind = icts::SdcObjectKind::kPin, .pattern = "gen/A"}},
                                  .master_clock_name = "ROOT",
                                  .divide_by = 2,
                                  .waveform_ns = {},
                                  .generated_edges = {},
                                  .generated_edge_shifts_ns = {},
                                  .divide_by_explicit = true});
    constraints.propagated_clocks.push_back({.kind = icts::SdcObjectKind::kClock, .pattern = "GEN"});
    return constraints;
  }

  auto buildOwnedGenerated(const std::string& master) -> icts::FastStaBuildResult
  {
    PhysicalOwner owner(master);
    const auto graph = makeGeneratedGraph(master);
    if (!graph.has_value()) {
      return {.failure_reason = "fixture_generated_graph_unavailable"};
    }
    const auto constraints = generatedConstraints();
    return _authority.buildContext({.clock = &owner.clock, .timing_graph = &*graph, .constraints = &constraints, .require_power = true});
  }

  auto buildOwnedGeneratedRaw(const std::string& master) -> icts::FastStaBuilder::BuildResult
  {
    PhysicalOwner owner(master);
    const auto graph = makeGeneratedGraph(master);
    if (!graph.has_value()) {
      return {.failure_reason = "fixture_generated_graph_unavailable"};
    }
    const auto constraints = generatedConstraints();
    auto result = icts::FastStaBuilder::buildContext(_environment,
                                                     {.clock = &owner.clock, .timing_graph = &*graph, .constraints = &constraints, .require_power = true});
    if (result.context.has_value() && (!icts::FastStaTiming::update(*result.context) || !icts::FastStaPower::update(*result.context))) {
      return {.failure_reason = "fixture_analysis_failed"};
    }
    return result;
  }

  idb::IdbLayout _layout;
  icts::Wrapper _wrapper;
  icts::FastStaEnvironment _environment;
  icts::FastSTA _authority;
  icts::SdcClockData _constraints;
};

TEST_F(FastSTATrialTestInterface, TrialMatchesColdClockElectricalAndFullPowerFacts)
{
  const auto original = build("CHARBUF");
  const auto reference = build("CHARBUF");
  const auto changed = build("SLOWBUF");
  if (!original.context_id.has_value()) {
    FAIL() << original.failure_reason;
  }
  if (!reference.context_id.has_value()) {
    FAIL() << reference.failure_reason;
  }
  if (!changed.context_id.has_value()) {
    FAIL() << changed.failure_reason;
  }
  const auto original_power = _authority.queryPower(*original.context_id);
  const auto changed_power = _authority.queryPower(*changed.context_id);
  if (!original_power.has_value()) {
    FAIL() << "Expected original_power to have a value.";
  }
  if (!changed_power.has_value()) {
    FAIL() << "Expected changed_power to have a value.";
  }
  ASSERT_GT(original_power->internal_power_w, 0.0);
  ASSERT_NE(original_power->internal_power_w, changed_power->internal_power_w);
  ASSERT_NE(original_power->area_um2, changed_power->area_um2);
  const auto original_points = _authority.collectTimingPointFacts(*original.context_id);
  ASSERT_FALSE(original_points.empty());

  ASSERT_TRUE(_authority.beginBufferMastersClockTrial(*original.context_id, {{.node_id = 2U, .cell_master = "SLOWBUF"}}));
  const auto trial_status = _authority.queryAnalysisStatus(*original.context_id);
  if (!trial_status.has_value()) {
    FAIL() << "Expected trial_status to have a value.";
  }
  EXPECT_TRUE(trial_status->clock_timing_valid);
  EXPECT_TRUE(trial_status->power_valid);
  EXPECT_FALSE(trial_status->timing_valid);
  ExpectClockFactsEqual(_authority, *original.context_id, *changed.context_id);
  EXPECT_FALSE(_authority.beginBufferMastersClockTrial(*original.context_id, {{.node_id = 2U, .cell_master = "CHARBUF"}}));
  ExpectClockFactsEqual(_authority, *original.context_id, *changed.context_id);
  ASSERT_TRUE(_authority.restoreBufferMastersClockTrial(*original.context_id));
  ExpectClockFactsEqual(_authority, *original.context_id, *reference.context_id);
  const auto restored_points = _authority.collectTimingPointFacts(*original.context_id);
  ASSERT_EQ(restored_points.size(), original_points.size());
  for (std::size_t index = 0U; index < original_points.size(); ++index) {
    const auto& actual = restored_points.at(index);
    const auto& expected = original_points.at(index);
    EXPECT_EQ(actual.pin_name, expected.pin_name);
    EXPECT_EQ(actual.launch_pin_name, expected.launch_pin_name);
    EXPECT_EQ(actual.analysis, expected.analysis);
    EXPECT_EQ(actual.transition, expected.transition);
    EXPECT_DOUBLE_EQ(actual.arrival_ns, expected.arrival_ns);
    EXPECT_DOUBLE_EQ(actual.slew_ns, expected.slew_ns);
  }
}

TEST_F(FastSTATrialTestInterface, FailedTrialPreservesFactsAndAllowsRetry)
{
  const auto original = build("CHARBUF");
  const auto reference = build("CHARBUF");
  const auto changed = build("SLOWBUF");
  if (!original.context_id.has_value()) {
    FAIL() << original.failure_reason;
  }
  if (!reference.context_id.has_value()) {
    FAIL() << reference.failure_reason;
  }
  if (!changed.context_id.has_value()) {
    FAIL() << changed.failure_reason;
  }
  EXPECT_FALSE(_authority.beginBufferMastersClockTrial(*original.context_id, {{.node_id = 2U, .cell_master = "BADBUF"}}));
  const auto status = _authority.queryAnalysisStatus(*original.context_id);
  if (!status.has_value()) {
    FAIL() << "Expected status to have a value.";
  }
  EXPECT_TRUE(status->timing_valid);
  EXPECT_TRUE(status->clock_timing_valid);
  EXPECT_TRUE(status->power_valid);
  ExpectClockFactsEqual(_authority, *original.context_id, *reference.context_id);
  ASSERT_TRUE(_authority.beginBufferMastersClockTrial(*original.context_id, {{.node_id = 2U, .cell_master = "SLOWBUF"}}));
  ExpectClockFactsEqual(_authority, *original.context_id, *changed.context_id);
  ASSERT_TRUE(_authority.restoreBufferMastersClockTrial(*original.context_id));
  ExpectClockFactsEqual(_authority, *original.context_id, *reference.context_id);
}

TEST_F(FastSTATrialTestInterface, RegionalTrialPreservesColdEarlyLateRiseFallPropagation)
{
  auto original = buildRaw("CHARBUF");
  if (!original.context.has_value()) {
    FAIL() << original.failure_reason;
  }
  auto& actual = *original.context;
  for (const auto* master : {"SLOWBUF", "CHARBUF", "SLOWBUF"}) {
    const auto cold = buildRaw(master);
    if (!cold.context.has_value()) {
      FAIL() << cold.failure_reason;
    }
    const auto& expected = *cold.context;
    const auto dirty = icts::FastStaIncremental::changeBufferMastersClockIncremental(actual, {{.node_id = 2U, .cell_master = master}});
    if (!dirty.has_value()) {
      FAIL() << "Expected dirty to have a value.";
    }
    ASSERT_TRUE(icts::FastStaTiming::updateClockTrialRegion(actual, *dirty));
    ASSERT_TRUE(icts::FastStaPower::updateRegion(actual, *dirty));
    ASSERT_EQ(actual.nodes.size(), expected.nodes.size());
    for (std::size_t node_id = 0U; node_id < actual.nodes.size(); ++node_id) {
      const auto& actual_node = actual.nodes.at(node_id);
      const auto& expected_node = expected.nodes.at(node_id);
      SCOPED_TRACE(::testing::Message() << master << " node " << actual_node.name);
      for (std::size_t analysis = 0U; analysis < 2U; ++analysis) {
        for (std::size_t transition = 0U; transition < 2U; ++transition) {
          const auto& actual_timing = analysis == 0U ? actual_node.early_timing.at(transition) : actual_node.late_timing.at(transition);
          const auto& expected_timing = analysis == 0U ? expected_node.early_timing.at(transition) : expected_node.late_timing.at(transition);
          ASSERT_TRUE(expected_timing.valid);
          EXPECT_EQ(actual_timing.valid, expected_timing.valid);
          EXPECT_EQ(actual_timing.clock_name, expected_timing.clock_name);
          EXPECT_EQ(actual_timing.launch_node_id, expected_timing.launch_node_id);
          EXPECT_EQ(actual_timing.launch_clock_transition, expected_timing.launch_clock_transition);
          EXPECT_DOUBLE_EQ(actual_timing.arrival_ns, expected_timing.arrival_ns);
          EXPECT_DOUBLE_EQ(actual_timing.slew_ns, expected_timing.slew_ns);
        }
      }
    }
    const auto& actual_drivers = actual.nets.at(1U).driver_timing_by_state;
    const auto& expected_drivers = expected.nets.at(1U).driver_timing_by_state;
    ASSERT_EQ(actual_drivers.size(), 2U);
    ASSERT_EQ(actual_drivers.size(), expected_drivers.size());
    for (std::size_t analysis = 0U; analysis < 2U; ++analysis) {
      for (std::size_t transition = 0U; transition < 2U; ++transition) {
        const auto& actual_driver = actual_drivers.at(analysis).at(transition);
        const auto& expected_driver = expected_drivers.at(analysis).at(transition);
        EXPECT_EQ(actual_driver.valid, expected_driver.valid);
        EXPECT_EQ(actual_driver.algorithm, expected_driver.algorithm);
        EXPECT_EQ(actual_driver.driver_waveform_valid, expected_driver.driver_waveform_valid);
        EXPECT_EQ(actual_driver.driver_library_name, expected_driver.driver_library_name);
        EXPECT_EQ(actual_driver.driver_cell_master, expected_driver.driver_cell_master);
        EXPECT_DOUBLE_EQ(actual_driver.ceff_pf, expected_driver.ceff_pf);
        EXPECT_DOUBLE_EQ(actual_driver.gate_delay_ns, expected_driver.gate_delay_ns);
        EXPECT_DOUBLE_EQ(actual_driver.driver_slew_ns, expected_driver.driver_slew_ns);
        EXPECT_DOUBLE_EQ(actual_driver.driver_waveform_delay_ns, expected_driver.driver_waveform_delay_ns);
      }
    }
    EXPECT_DOUBLE_EQ(actual.power.area_um2, expected.power.area_um2);
    EXPECT_DOUBLE_EQ(actual.power.internal_power_w, expected.power.internal_power_w);
    EXPECT_DOUBLE_EQ(actual.power.leakage_power_w, expected.power.leakage_power_w);
    EXPECT_DOUBLE_EQ(actual.power.switching_power_w, expected.power.switching_power_w);
    EXPECT_DOUBLE_EQ(actual.power.total_power_w, expected.power.total_power_w);
    EXPECT_NE(actual.nodes.back().late_timing.front().arrival_ns, actual.nodes.back().late_timing.back().arrival_ns);
    EXPECT_NE(actual.nets.back().parasitic.driver_pi_by_timing.front().front().far_cap_pf,
              actual.nets.back().parasitic.driver_pi_by_timing.back().front().far_cap_pf);
  }
}

TEST_F(FastSTATrialTestInterface, GeneratedClockBeyondOwnerMatchesColdAndRestoresAllTimingFacts)
{
  const auto original = buildOwnedGenerated("CHARBUF");
  const auto reference = buildOwnedGenerated("CHARBUF");
  const auto changed = buildOwnedGenerated("SLOWBUF");
  if (!original.context_id.has_value()) {
    FAIL() << original.failure_reason;
  }
  const auto original_id = *original.context_id;
  if (!reference.context_id.has_value()) {
    FAIL() << reference.failure_reason;
  }
  const auto reference_id = *reference.context_id;
  if (!changed.context_id.has_value()) {
    FAIL() << changed.failure_reason;
  }
  const auto changed_id = *changed.context_id;
  const auto profile = _authority.queryGraphProfile(original_id);
  if (!profile.has_value()) {
    FAIL() << "Expected profile to have a value.";
  }
  ASSERT_EQ(profile->node_count, 6U);
  ASSERT_EQ(profile->owned_clock_node_count, 4U);
  const auto buffers = _authority.collectClockSizingBuffers(original_id);
  ASSERT_EQ(buffers.size(), 1U);
  ASSERT_EQ(buffers.front().inst_name, "buf");
  const auto before = _authority.collectTimingPointFacts(original_id);
  ASSERT_EQ(before.size(), 24U);
  ASSERT_EQ(std::ranges::count_if(before, [](const auto& fact) -> bool { return fact.pin_name == "gen/Y"; }), 4U);
  // The physical owner's endpoint is the input of GEN. The owner arrival
  // query includes that boundary, while skew requires a kSink node.
  const auto owner_arrivals = _authority.collectClockSinkArrivals(original_id);
  ASSERT_EQ(owner_arrivals.size(), 1U);
  EXPECT_EQ(owner_arrivals.front().sink_name, "gen/A");
  ASSERT_FALSE(_authority.querySkew(original_id).valid);
  const auto before_caps = _authority.collectPinCapacitanceFacts(original_id);
  const auto before_rc = _authority.collectPiElmoreFacts(original_id);
  const auto before_parasitics = _authority.collectParasiticNetFacts(original_id);
  const auto before_power = _authority.queryPower(original_id);
  const auto before_summary = _authority.queryTimingSummary(original_id);
  if (!before_power.has_value()) {
    FAIL() << "Expected before_power to have a value.";
  }
  const auto expected_power = *before_power;
  if (!before_summary.has_value()) {
    FAIL() << "Expected before_summary to have a value.";
  }
  const auto& expected_summary = *before_summary;
  const auto expect_restored = [&]() -> void {
    const auto status = _authority.queryAnalysisStatus(original_id);
    if (!status.has_value()) {
      FAIL() << "Expected status to have a value.";
    }
    EXPECT_TRUE(status->timing_valid);
    EXPECT_TRUE(status->clock_timing_valid);
    EXPECT_TRUE(status->power_valid);
    const auto restored_buffers = _authority.collectClockSizingBuffers(original_id);
    ASSERT_EQ(restored_buffers.size(), buffers.size());
    EXPECT_EQ(restored_buffers.front().cell_master, buffers.front().cell_master);
    ExpectClockFactsEqual(_authority, original_id, reference_id, false);
    const auto restored = _authority.collectTimingPointFacts(original_id);
    ASSERT_EQ(restored.size(), before.size());
    for (std::size_t index = 0U; index < before.size(); ++index) {
      const auto& actual = restored.at(index);
      const auto& expected = before.at(index);
      EXPECT_EQ(std::tie(actual.pin_name, actual.launch_pin_name, actual.analysis, actual.transition, actual.arrival_ns, actual.slew_ns),
                std::tie(expected.pin_name, expected.launch_pin_name, expected.analysis, expected.transition, expected.arrival_ns, expected.slew_ns));
    }
    const auto restored_caps = _authority.collectPinCapacitanceFacts(original_id);
    ASSERT_EQ(restored_caps.size(), before_caps.size());
    for (std::size_t index = 0U; index < before_caps.size(); ++index) {
      EXPECT_EQ(restored_caps.at(index).pin_name, before_caps.at(index).pin_name);
      EXPECT_EQ(restored_caps.at(index).capacitance_pf, before_caps.at(index).capacitance_pf);
    }
    const auto restored_rc = _authority.collectPiElmoreFacts(original_id);
    ASSERT_EQ(restored_rc.size(), before_rc.size());
    for (std::size_t index = 0U; index < before_rc.size(); ++index) {
      const auto& actual = restored_rc.at(index);
      const auto& expected = before_rc.at(index);
      EXPECT_EQ(std::tie(actual.net_name, actual.driver_pin_name, actual.load_pin_name, actual.analysis, actual.transition, actual.near_cap_pf,
                         actual.far_cap_pf, actual.resistance_ohm, actual.elmore_ns),
                std::tie(expected.net_name, expected.driver_pin_name, expected.load_pin_name, expected.analysis, expected.transition, expected.near_cap_pf,
                         expected.far_cap_pf, expected.resistance_ohm, expected.elmore_ns));
    }
    const auto restored_parasitics = _authority.collectParasiticNetFacts(original_id);
    ASSERT_EQ(restored_parasitics.size(), before_parasitics.size());
    for (std::size_t net_id = 0U; net_id < before_parasitics.size(); ++net_id) {
      const auto& actual = restored_parasitics.at(net_id);
      const auto& expected = before_parasitics.at(net_id);
      EXPECT_EQ(actual.net_name, expected.net_name);
      EXPECT_EQ(actual.driver_pin_name, expected.driver_pin_name);
      EXPECT_EQ(actual.load_pin_names, expected.load_pin_names);
      ASSERT_EQ(actual.nodes.size(), expected.nodes.size());
      ASSERT_EQ(actual.edges.size(), expected.edges.size());
      for (std::size_t node_id = 0U; node_id < actual.nodes.size(); ++node_id) {
        const auto& actual_node = actual.nodes.at(node_id);
        const auto& expected_node = expected.nodes.at(node_id);
        EXPECT_EQ(std::tie(actual_node.node_name, actual_node.terminal_pin_names, actual_node.capacitance_pf, actual_node.wire_capacitance_pf,
                           actual_node.pin_capacitance_pf),
                  std::tie(expected_node.node_name, expected_node.terminal_pin_names, expected_node.capacitance_pf, expected_node.wire_capacitance_pf,
                           expected_node.pin_capacitance_pf));
      }
      for (std::size_t edge_id = 0U; edge_id < actual.edges.size(); ++edge_id) {
        const auto& actual_edge = actual.edges.at(edge_id);
        const auto& expected_edge = expected.edges.at(edge_id);
        EXPECT_EQ(std::tie(actual_edge.source_node_index, actual_edge.target_node_index, actual_edge.resistance_ohm),
                  std::tie(expected_edge.source_node_index, expected_edge.target_node_index, expected_edge.resistance_ohm));
      }
    }
    const auto power = _authority.queryPower(original_id);
    if (!power.has_value()) {
      FAIL() << "Expected power to have a value.";
    }
    EXPECT_EQ(std::tie(power->area_um2, power->internal_power_w, power->leakage_power_w, power->switching_power_w, power->total_power_w,
                       power->unknown_gate_activity_count),
              std::tie(expected_power.area_um2, expected_power.internal_power_w, expected_power.leakage_power_w, expected_power.switching_power_w,
                       expected_power.total_power_w, expected_power.unknown_gate_activity_count));
    const auto summary = _authority.queryTimingSummary(original_id);
    if (!summary.has_value()) {
      FAIL() << "Expected summary to have a value.";
    }
    EXPECT_EQ(summary->status, expected_summary.status);
    EXPECT_EQ(summary->runtime_s, expected_summary.runtime_s);
    EXPECT_EQ(summary->clock_propagation_runtime_s, expected_summary.clock_propagation_runtime_s);
    EXPECT_EQ(summary->logic_propagation_runtime_s, expected_summary.logic_propagation_runtime_s);
    EXPECT_EQ(summary->relation_extraction_runtime_s, expected_summary.relation_extraction_runtime_s);
  };
  expect_restored();

  ASSERT_TRUE(_authority.beginBufferMastersClockTrial(original_id, {{.node_id = buffers.front().node_id, .cell_master = "SLOWBUF"}}));
  const auto trial_status = _authority.queryAnalysisStatus(original_id);
  if (!trial_status.has_value()) {
    FAIL() << "Expected trial_status to have a value.";
  }
  EXPECT_TRUE(trial_status->clock_timing_valid);
  EXPECT_TRUE(trial_status->power_valid);
  EXPECT_FALSE(trial_status->timing_valid);
  EXPECT_TRUE(_authority.collectTimingPointFacts(original_id).empty());
  EXPECT_FALSE(_authority.queryTimingSummary(original_id).has_value());
  ExpectClockFactsEqual(_authority, original_id, changed_id, false);
  const auto trial_points = _authority.collectClockPointFacts(original_id);
  const auto reference_points = _authority.collectClockPointFacts(reference_id);
  const auto trial_generated = std::ranges::find_if(trial_points, [](const auto& point) -> bool { return point.pin_name == "gen/Y"; });
  const auto reference_generated = std::ranges::find_if(reference_points, [](const auto& point) -> bool { return point.pin_name == "gen/Y"; });
  ASSERT_NE(trial_generated, trial_points.end());
  ASSERT_NE(reference_generated, reference_points.end());
  EXPECT_NE(trial_generated->arrival_ns, reference_generated->arrival_ns);
  EXPECT_NE(trial_generated->slew_ns, reference_generated->slew_ns);
  ASSERT_TRUE(_authority.restoreBufferMastersClockTrial(original_id));
  expect_restored();
  EXPECT_FALSE(_authority.beginBufferMastersClockTrial(original_id, {{.node_id = buffers.front().node_id, .cell_master = "BADBUF"}}));
  expect_restored();
  EXPECT_FALSE(_authority.restoreBufferMastersClockTrial(original_id));
  EXPECT_FALSE(_authority.changeBufferMasters(original_id, {{.node_id = buffers.front().node_id, .cell_master = "BADBUF"}}));
  expect_restored();
  EXPECT_FALSE(_authority.changeBufferMastersTimingOnly(original_id, {{.node_id = buffers.front().node_id, .cell_master = "BADBUF"}}));
  expect_restored();
  ASSERT_TRUE(_authority.beginBufferMastersClockTrial(original_id, {{.node_id = buffers.front().node_id, .cell_master = "SLOWBUF"}}));
  ExpectClockFactsEqual(_authority, original_id, changed_id, false);
  ASSERT_TRUE(_authority.restoreBufferMastersClockTrial(original_id));
  expect_restored();
}

TEST_F(FastSTATrialTestInterface, GeneratedDirtyClosureIncludesNonownerTimingRcDriversAndPower)
{
  auto original = buildOwnedGeneratedRaw("CHARBUF");
  const auto cold = buildOwnedGeneratedRaw("SLOWBUF");
  if (!original.context.has_value()) {
    FAIL() << original.failure_reason;
  }
  if (!cold.context.has_value()) {
    FAIL() << cold.failure_reason;
  }
  auto& actual = *original.context;
  const auto& expected = *cold.context;
  const auto edit_node = actual.node_id_by_name.at("buf/Y");
  const auto generated_node = actual.node_id_by_name.at("gen/Y");
  const auto sink_node = actual.node_id_by_name.at("sink");
  const auto generated_net = actual.net_id_by_name.at("generated_leaf");
  const auto before_generated = actual.nodes.at(generated_node);
  ASSERT_TRUE(actual.timing_valid);
  ASSERT_TRUE(actual.logic_tags_valid);
  ASSERT_TRUE(actual.power_valid);
  ASSERT_TRUE(expected.timing_valid);
  ASSERT_EQ(actual.nodes.at(generated_node).clock_name, "GEN");
  ASSERT_EQ(actual.nodes.at(sink_node).clock_name, "GEN");
  ASSERT_EQ(std::ranges::find(actual.owned_clock_node_ids, generated_node), actual.owned_clock_node_ids.end());
  EXPECT_FALSE(icts::FastStaIncremental::describeClockBufferMasterRegion(actual, {{.node_id = generated_node, .cell_master = "SLOWBUF"}}).has_value());
  const auto dirty = icts::FastStaIncremental::changeBufferMastersClockIncremental(actual, {{.node_id = edit_node, .cell_master = "SLOWBUF"}});
  if (!dirty.has_value()) {
    FAIL() << "Expected dirty to have a value.";
  }
  EXPECT_NE(std::ranges::find(dirty->node_ids, generated_node), dirty->node_ids.end());
  EXPECT_NE(std::ranges::find(dirty->node_ids, sink_node), dirty->node_ids.end());
  EXPECT_NE(std::ranges::find(dirty->net_ids, generated_net), dirty->net_ids.end());
  ASSERT_TRUE(icts::FastStaTiming::updateClockTrialRegion(actual, *dirty));
  ASSERT_TRUE(icts::FastStaPower::updateRegion(actual, *dirty));
  EXPECT_TRUE(actual.clock_timing_valid);
  EXPECT_TRUE(actual.power_valid);
  EXPECT_FALSE(actual.timing_valid);
  EXPECT_FALSE(actual.logic_tags_valid);
  ASSERT_EQ(actual.nodes.size(), expected.nodes.size());
  for (std::size_t node_id = 0U; node_id < actual.nodes.size(); ++node_id) {
    const auto& actual_node = actual.nodes.at(node_id);
    const auto& expected_node = expected.nodes.at(node_id);
    SCOPED_TRACE(actual_node.name);
    EXPECT_EQ(actual_node.clock_inactive, expected_node.clock_inactive);
    EXPECT_EQ(actual_node.cell_master, expected_node.cell_master);
    ExpectTimingPointEqual(actual_node.timing, expected_node.timing);
    for (std::size_t analysis = 0U; analysis < 2U; ++analysis) {
      for (std::size_t transition = 0U; transition < 2U; ++transition) {
        SCOPED_TRACE(::testing::Message() << "analysis " << analysis << " transition " << transition);
        const auto& actual_timing = analysis == 0U ? actual_node.early_timing.at(transition) : actual_node.late_timing.at(transition);
        const auto& expected_timing = analysis == 0U ? expected_node.early_timing.at(transition) : expected_node.late_timing.at(transition);
        ASSERT_TRUE(expected_timing.valid);
        ExpectTimingPointEqual(actual_timing, expected_timing);
        if (node_id == generated_node) {
          const auto& before_timing = analysis == 0U ? before_generated.early_timing.at(transition) : before_generated.late_timing.at(transition);
          EXPECT_NE(actual_timing.arrival_ns, before_timing.arrival_ns);
          EXPECT_NE(actual_timing.slew_ns, before_timing.slew_ns);
        }
      }
    }
    EXPECT_DOUBLE_EQ(actual_node.internal_power_w, expected_node.internal_power_w);
    EXPECT_DOUBLE_EQ(actual_node.leakage_power_w, expected_node.leakage_power_w);
    EXPECT_DOUBLE_EQ(actual_node.area_um2, expected_node.area_um2);
  }
  ASSERT_EQ(actual.nets.at(generated_net).driver_timing_by_state.size(), 2U);
  ASSERT_EQ(actual.nets.size(), expected.nets.size());
  for (std::size_t net_id = 0U; net_id < actual.nets.size(); ++net_id) {
    const auto& actual_net = actual.nets.at(net_id);
    const auto& expected_net = expected.nets.at(net_id);
    SCOPED_TRACE(actual_net.name);
    EXPECT_EQ(actual_net.parasitic.valid, expected_net.parasitic.valid);
    EXPECT_DOUBLE_EQ(actual_net.load_cap_pf, expected_net.load_cap_pf);
    EXPECT_DOUBLE_EQ(actual_net.switching_power_w, expected_net.switching_power_w);
    ASSERT_EQ(actual_net.driver_timing_by_state.size(), expected_net.driver_timing_by_state.size());
    ASSERT_EQ(actual_net.parasitic.rc_nodes.size(), expected_net.parasitic.rc_nodes.size());
    for (std::size_t analysis = 0U; analysis < 2U; ++analysis) {
      for (std::size_t transition = 0U; transition < 2U; ++transition) {
        SCOPED_TRACE(::testing::Message() << "analysis " << analysis << " transition " << transition);
        if (!actual_net.driver_timing_by_state.empty()) {
          const auto& actual_driver = actual_net.driver_timing_by_state.at(analysis).at(transition);
          const auto& expected_driver = expected_net.driver_timing_by_state.at(analysis).at(transition);
          ASSERT_TRUE(expected_driver.valid);
          ExpectDmpDriverEqual(actual_driver, expected_driver);
        }
        const auto& actual_pi = actual_net.parasitic.driver_pi_by_timing.at(analysis).at(transition);
        const auto& expected_pi = expected_net.parasitic.driver_pi_by_timing.at(analysis).at(transition);
        EXPECT_DOUBLE_EQ(actual_pi.near_cap_pf, expected_pi.near_cap_pf);
        EXPECT_DOUBLE_EQ(actual_pi.far_cap_pf, expected_pi.far_cap_pf);
        EXPECT_DOUBLE_EQ(actual_pi.resistance_ohm, expected_pi.resistance_ohm);
        const auto& actual_timing_pi = actual_net.parasitic.pi_by_timing.at(analysis).at(transition);
        const auto& expected_timing_pi = expected_net.parasitic.pi_by_timing.at(analysis).at(transition);
        EXPECT_DOUBLE_EQ(actual_timing_pi.near_cap_pf, expected_timing_pi.near_cap_pf);
        EXPECT_DOUBLE_EQ(actual_timing_pi.far_cap_pf, expected_timing_pi.far_cap_pf);
        EXPECT_DOUBLE_EQ(actual_timing_pi.resistance_ohm, expected_timing_pi.resistance_ohm);
        for (std::size_t rc_node_id = 0U; rc_node_id < actual_net.parasitic.rc_nodes.size(); ++rc_node_id) {
          EXPECT_DOUBLE_EQ(actual_net.parasitic.rc_nodes.at(rc_node_id).elmore_delay_ns_by_timing.at(analysis).at(transition),
                           expected_net.parasitic.rc_nodes.at(rc_node_id).elmore_delay_ns_by_timing.at(analysis).at(transition));
        }
      }
    }
  }
  EXPECT_DOUBLE_EQ(actual.power.area_um2, expected.power.area_um2);
  EXPECT_DOUBLE_EQ(actual.power.internal_power_w, expected.power.internal_power_w);
  EXPECT_DOUBLE_EQ(actual.power.leakage_power_w, expected.power.leakage_power_w);
  EXPECT_DOUBLE_EQ(actual.power.switching_power_w, expected.power.switching_power_w);
  EXPECT_DOUBLE_EQ(actual.power.total_power_w, expected.power.total_power_w);
  EXPECT_EQ(actual.power.unknown_gate_activity_count, expected.power.unknown_gate_activity_count);
}

TEST_F(FastSTATrialTestInterface, GeneratedFailedTrialReachesTimingAfterMasterAndRcMutation)
{
  auto original = buildOwnedGeneratedRaw("CHARBUF");
  if (!original.context.has_value()) {
    FAIL() << original.failure_reason;
  }
  auto& context = *original.context;
  const auto input_node = context.node_id_by_name.at("buf/A");
  const auto output_node = context.node_id_by_name.at("buf/Y");
  const auto root_net = context.net_id_by_name.at("root");
  const auto generated_node = context.node_id_by_name.at("gen/Y");
  const auto before_cap = context.nodes.at(input_node).input_cap_pf;
  const auto before_load = context.nets.at(root_net).load_cap_pf;
  const auto before_elmore = context.nets.at(root_net).parasitic.rc_nodes.back().elmore_delay_ns;
  const auto before_generated_arrival = context.nodes.at(generated_node).late_timing.front().arrival_ns;
  ASSERT_TRUE(context.nodes.at(generated_node).late_timing.front().valid);
  const auto dirty = icts::FastStaIncremental::changeBufferMastersClockIncremental(context, {{.node_id = output_node, .cell_master = "BADBUF"}});
  if (!dirty.has_value()) {
    FAIL() << "Expected dirty to have a value.";
  }
  EXPECT_EQ(context.nodes.at(input_node).cell_master, "BADBUF");
  EXPECT_EQ(context.nodes.at(output_node).cell_master, "BADBUF");
  EXPECT_NE(context.nodes.at(input_node).input_cap_pf, before_cap);
  EXPECT_FALSE(icts::FastStaTiming::updateClockTrialRegion(context, *dirty));
  EXPECT_NE(context.nets.at(root_net).load_cap_pf, before_load);
  EXPECT_NE(context.nets.at(root_net).parasitic.rc_nodes.back().elmore_delay_ns, before_elmore);
  EXPECT_FALSE(context.clock_timing_valid);
  EXPECT_FALSE(context.timing_valid);
  EXPECT_TRUE(context.nodes.at(generated_node).late_timing.front().valid);
  EXPECT_NE(context.nodes.at(generated_node).late_timing.front().arrival_ns, before_generated_arrival);
  EXPECT_FALSE(context.nodes.at(generated_node).late_timing.back().valid);
  EXPECT_FALSE(context.nodes.at(generated_node).early_timing.back().valid);
}

TEST_F(FastSTATrialTestInterface, CompleteTimingRejectsInvalidNonownerGeneratedDescendantWithoutPower)
{
  PhysicalOwner owner("CHARBUF");
  owner.downstream.set_cell_master("BADBUF");
  auto generated = makeGeneratedGraph("CHARBUF");
  if (!generated.has_value()) {
    FAIL() << "Expected the generated clock graph.";
  }
  auto& graph = *generated;
  const auto bad_cell = icts::FastStaLiberty::extractBufferCell(_wrapper, "BADBUF");
  if (!bad_cell.has_value()) {
    FAIL() << "Expected bad_cell to have a value.";
  }
  for (auto& node : graph.nodes) {
    if (node.inst_name == "gen") {
      node.cell_master = "BADBUF";
      if (node.input) {
        node.input_cap_pf = bad_cell->input_cap_pf;
        node.input_cap_pf_by_timing = bad_cell->input_cap_pf_by_timing;
      }
    }
  }
  graph.arcs.back().cell_master = "BADBUF";
  const auto constraints = generatedConstraints();
  const icts::FastStaBuildInput input{.clock = &owner.clock, .timing_graph = &graph, .constraints = &constraints, .require_power = false};
  auto raw = icts::FastStaBuilder::buildContext(_environment, input);
  if (!raw.context.has_value()) {
    FAIL() << raw.failure_reason;
  }
  const auto generated_node = raw.context->node_id_by_name.at("gen/Y");
  ASSERT_EQ(std::ranges::find(raw.context->owned_clock_node_ids, generated_node), raw.context->owned_clock_node_ids.end());
  EXPECT_FALSE(icts::FastStaTiming::update(*raw.context));
  EXPECT_FALSE(raw.context->timing_valid);
  EXPECT_FALSE(raw.context->clock_timing_valid);
  const auto complete = _authority.buildContext(input);
  EXPECT_FALSE(complete.ok());
  EXPECT_FALSE(complete.context_id.has_value());
}

}  // namespace
}  // namespace icts_test
