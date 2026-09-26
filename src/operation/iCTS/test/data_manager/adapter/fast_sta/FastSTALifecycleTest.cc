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
 * @file FastSTALifecycleTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-09-13
 * @brief Real Liberty context transactions, topology splice and reset regressions.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "FastSTA.hh"
#include "FastSTATiming.hh"
#include "IdbLayout.h"
#include "clock_state/FastSTABuilder.hh"
#include "clock_state/FastSTAClockState.hh"
#include "design/Clock.hh"
#include "design/Design.hh"
#include "design/Inst.hh"
#include "design/Net.hh"
#include "design/Pin.hh"
#include "idm.h"
#include "io/Wrapper.hh"
#include "liberty/FastSTALiberty.hh"
#include "liberty/Lib.hh"

namespace icts_test {
namespace {

TEST(FastSTATest, ContextFailureRollbackAndReset)
{
  const auto path = (std::filesystem::path(__FILE__).parent_path() / "FastSTAContracts.lib").string();
  dmInst->get_config().set_lib_paths({path});
  ASSERT_TRUE(dmInst->readLib({path}));
  // extractBufferCell uses actual physical area, not Liberty's unitless area.
  // Both masters therefore have an explicit 1 um^2 physical fixture.
  idb::IdbLayout layout;
  layout.get_units()->set_microns_dbu(1000);
  for (const auto* master_name : {"GOODBUF", "SLOWBUF", "BADBUF"}) {
    auto* master = layout.get_cell_master_list()->set_cell_master(master_name);
    master->set_width(1000U);
    master->set_height(1000U);
  }
  icts::Wrapper wrapper;
  wrapper.set_idb_layout(&layout);
  auto* port = wrapper.findLibertyCell("GOODBUF")->get_cell_port_or_port_bus("A");
  ASSERT_TRUE(port->has_port_cap());
  EXPECT_DOUBLE_EQ(port->get_port_cap(), 0.0);
  const auto model = icts::FastStaLiberty::extractBufferCell(wrapper, "GOODBUF");
  if (!model.has_value()) {
    ADD_FAILURE() << "Expected the GOODBUF Liberty model.";
    return;
  }
  EXPECT_TRUE(model->input_cap_profile_available);
  EXPECT_DOUBLE_EQ(model->input_cap_pf, 0.0);
  EXPECT_DOUBLE_EQ(model->area_um2, 1.0);
  icts::WrapperTimingGraph graph;
  graph.status = icts::WrapperTimingGraphStatus::kComplete;
  graph.nodes = {
      {.pin_name = "clk", .input = true, .top_level = true},
      {.pin_name = "buf/A", .inst_name = "buf", .cell_master = "GOODBUF", .x_dbu = 1000, .input = true, .port_name = "A", .input_cap_profile_available = true},
      {.pin_name = "buf/Y", .inst_name = "buf", .cell_master = "GOODBUF", .x_dbu = 1000, .output = true, .port_name = "Y", .logic_function = "A"},
      {.pin_name = "sink",
       .x_dbu = 2000,
       .input_cap_pf = .01,
       .input_cap_pf_by_timing = {{{.01, .01}, {.01, .01}}},
       .max_slew_ns = .06,
       .output = true,
       .top_level = true,
       .input_cap_profile_available = true}};
  graph.nets = {{.net_name = "root",
                 .driver_pin = "clk",
                 .load_pins = {"buf/A"},
                 .rc_segments = {{.begin_x_dbu = 0, .end_x_dbu = 1000, .resistance_ohm = 10, .capacitance_pf = .01}}},
                {.net_name = "leaf",
                 .driver_pin = "buf/Y",
                 .load_pins = {"sink"},
                 .rc_segments = {{.begin_x_dbu = 1000, .end_x_dbu = 2000, .resistance_ohm = 20, .capacitance_pf = .02}}}};
  graph.arcs = {{.inst_name = "buf",
                 .cell_master = "GOODBUF",
                 .input_port = "A",
                 .output_port = "Y",
                 .input_pin = "buf/A",
                 .output_pin = "buf/Y",
                 .positive_unate = true}};
  icts::SdcClockData constraints;
  constraints.clocks.push_back({.clock_name = "ROOT",
                                .targets = {{.kind = icts::SdcObjectKind::kPort, .pattern = "clk"}},
                                .generated_sources = {},
                                .period_ns = 1,
                                .period_resolved = true,
                                .waveform_ns = {0, .5},
                                .generated_edges = {},
                                .generated_edge_shifts_ns = {},
                                .waveform_resolved = true});
  const icts::FastStaEnvironment environment{.wrapper = &wrapper, .dbu_per_um = 1000, .routing_layer = 1, .root_input_slew_ns = .03};
  icts::FastSTA authority;
  authority.bindEnvironment(environment);
  const icts::FastStaBuildInput input{.timing_graph = &graph, .constraints = &constraints, .require_power = false};
  const auto ideal = authority.buildContext(input);
  if (!ideal.context_id.has_value()) {
    ADD_FAILURE() << "Expected an ideal context identity.";
    return;
  }
  const auto ideal_arrival = authority.queryClockNodeArrival(*ideal.context_id, 3U);
  if (!ideal_arrival.has_value()) {
    ADD_FAILURE() << "Expected ideal clock arrival.";
    return;
  }
  EXPECT_DOUBLE_EQ(*ideal_arrival, 0.0);
  constraints.propagated_clocks.push_back({.kind = icts::SdcObjectKind::kClock, .pattern = "ROOT"});
  const auto physical = authority.replaceContext(*ideal.context_id, input);
  if (!physical.context_id.has_value()) {
    ADD_FAILURE() << "Expected a physical context identity.";
    return;
  }
  EXPECT_GT(*physical.context_id, *ideal.context_id);
  EXPECT_FALSE(authority.queryAnalysisStatus(*ideal.context_id).has_value());
  const auto physical_arrival = authority.queryClockNodeArrival(*physical.context_id, 3U);
  if (!physical_arrival.has_value()) {
    ADD_FAILURE() << "Expected physical clock arrival.";
    return;
  }
  EXPECT_GT(*physical_arrival, .1);
  const auto slew = authority.querySlewStatus(*physical.context_id, 3U);
  if (!slew.has_value()) {
    ADD_FAILURE() << "Expected physical slew status.";
    return;
  }
  EXPECT_TRUE(slew->violated);  // Fall slew .08 violates .06 even though rise .04 passes.
  const auto before = authority.collectTimingPointFacts(*physical.context_id);
  const auto before_rc = authority.collectPiElmoreFacts(*physical.context_id);
  ASSERT_TRUE(authority.beginBufferMastersClockTrial(*physical.context_id, {{.node_id = 2U, .cell_master = "SLOWBUF"}}));
  const auto trial_status = authority.queryAnalysisStatus(*physical.context_id);
  if (!trial_status.has_value()) {
    ADD_FAILURE() << "Expected analysis status during clock trial.";
    return;
  }
  EXPECT_TRUE(trial_status->clock_timing_valid);
  EXPECT_FALSE(trial_status->timing_valid);
  const auto trial_arrival = authority.queryClockNodeArrival(*physical.context_id, 3U);
  if (!trial_arrival.has_value()) {
    ADD_FAILURE() << "Expected clock arrival during clock trial.";
    return;
  }
  EXPECT_GT(*trial_arrival, *physical_arrival);
  EXPECT_FALSE(authority.collectClockPointFacts(*physical.context_id).empty());
  EXPECT_TRUE(authority.collectTimingRelations(*physical.context_id).empty());
  ASSERT_TRUE(authority.restoreBufferMastersClockTrial(*physical.context_id));
  const auto restored_trial_status = authority.queryAnalysisStatus(*physical.context_id);
  if (!restored_trial_status.has_value()) {
    ADD_FAILURE() << "Expected analysis status after restoring clock trial.";
    return;
  }
  EXPECT_TRUE(restored_trial_status->timing_valid);
  const auto restored_arrival = authority.queryClockNodeArrival(*physical.context_id, 3U);
  if (!restored_arrival.has_value()) {
    ADD_FAILURE() << "Expected clock arrival after restoring clock trial.";
    return;
  }
  EXPECT_DOUBLE_EQ(*restored_arrival, *physical_arrival);
  const auto restored_rc = authority.collectPiElmoreFacts(*physical.context_id);
  ASSERT_EQ(before_rc.size(), restored_rc.size());
  for (std::size_t index = 0U; index < before_rc.size(); ++index) {
    EXPECT_EQ(before_rc.at(index).analysis, restored_rc.at(index).analysis);
    EXPECT_EQ(before_rc.at(index).transition, restored_rc.at(index).transition);
    EXPECT_DOUBLE_EQ(before_rc.at(index).near_cap_pf, restored_rc.at(index).near_cap_pf);
    EXPECT_DOUBLE_EQ(before_rc.at(index).far_cap_pf, restored_rc.at(index).far_cap_pf);
    EXPECT_DOUBLE_EQ(before_rc.at(index).resistance_ohm, restored_rc.at(index).resistance_ohm);
    EXPECT_DOUBLE_EQ(before_rc.at(index).elmore_ns, restored_rc.at(index).elmore_ns);
  }

  ASSERT_TRUE(authority.beginBufferMastersClockTrial(*physical.context_id, {{.node_id = 2U, .cell_master = "SLOWBUF"}}));
  ASSERT_TRUE(authority.commitBufferMastersClockTrial(*physical.context_id));
  const auto committed_trial_status = authority.queryAnalysisStatus(*physical.context_id);
  if (!committed_trial_status.has_value()) {
    ADD_FAILURE() << "Expected analysis status after committing clock trial.";
    return;
  }
  EXPECT_FALSE(committed_trial_status->timing_valid);
  ASSERT_TRUE(authority.updateTiming(*physical.context_id));
  const auto committed_arrival = authority.queryClockNodeArrival(*physical.context_id, 3U);
  if (!committed_arrival.has_value()) {
    ADD_FAILURE() << "Expected clock arrival after committing clock trial.";
    return;
  }
  EXPECT_GT(*committed_arrival, *physical_arrival);
  ASSERT_TRUE(authority.changeBufferMastersTimingOnly(*physical.context_id, {{.node_id = 2U, .cell_master = "GOODBUF"}}));

  EXPECT_FALSE(authority.beginBufferMastersClockTrial(*physical.context_id, {{.node_id = 2U, .cell_master = "BADBUF"}}));
  EXPECT_FALSE(authority.restoreBufferMastersClockTrial(*physical.context_id));
  const auto restored_status = authority.queryAnalysisStatus(*physical.context_id);
  if (!restored_status.has_value()) {
    ADD_FAILURE() << "Expected analysis status after failed trial.";
    return;
  }
  EXPECT_TRUE(restored_status->timing_valid);
  EXPECT_EQ(authority.collectClockSizingBuffers(*physical.context_id).front().cell_master, "GOODBUF");
  EXPECT_FALSE(authority.changeBufferMastersTimingOnly(*physical.context_id, {{.node_id = 2U, .cell_master = "BADBUF"}}));
  const auto after = authority.collectTimingPointFacts(*physical.context_id);
  ASSERT_EQ(before.size(), after.size());
  for (std::size_t index = 0; index < before.size(); ++index) {
    EXPECT_EQ(before.at(index).pin_name, after.at(index).pin_name);
    EXPECT_DOUBLE_EQ(before.at(index).arrival_ns, after.at(index).arrival_ns);
    EXPECT_DOUBLE_EQ(before.at(index).slew_ns, after.at(index).slew_ns);
  }
  const auto invalid = authority.replaceContext(*physical.context_id, {});
  EXPECT_FALSE(invalid.ok());
  const auto retained_status = authority.queryAnalysisStatus(*physical.context_id);
  if (!retained_status.has_value()) {
    ADD_FAILURE() << "Expected analysis status after failed replacement.";
    return;
  }
  EXPECT_TRUE(retained_status->timing_valid);
  const auto complete_before = authority.queryTimingSummary(*physical.context_id);
  if (!complete_before.has_value()) {
    FAIL() << "Expected complete_before to have a value.";
  }
  const auto pending = authority.beginContextTransaction(*physical.context_id);
  if (!pending.context_id.has_value()) {
    FAIL() << pending.failure_reason;
  }
  ASSERT_TRUE(authority.changeInstanceMasters(*pending.context_id, {{.inst_name = "buf", .cell_master = "SLOWBUF"}}));
  const auto pending_arrival = authority.queryClockNodeArrival(*pending.context_id, 3U);
  if (!pending_arrival.has_value()) {
    FAIL() << "Expected pending_arrival to have a value.";
  }
  EXPECT_GT(*pending_arrival, *physical_arrival);
  EXPECT_EQ(authority.collectClockSizingBuffers(*physical.context_id).front().cell_master, "GOODBUF");
  EXPECT_DOUBLE_EQ(*authority.queryClockNodeArrival(*physical.context_id, 3U), *physical_arrival);
  EXPECT_EQ(authority.collectClockSizingBuffers(*pending.context_id).front().cell_master, "SLOWBUF");
  EXPECT_DOUBLE_EQ(*authority.queryClockNodeArrival(*pending.context_id, 3U), *pending_arrival);
  EXPECT_FALSE(authority.changeInstanceMasters(*pending.context_id, {{.inst_name = "buf", .cell_master = "BADBUF"}}));
  EXPECT_DOUBLE_EQ(*authority.queryClockNodeArrival(*pending.context_id, 3U), *pending_arrival);
  EXPECT_FALSE(authority.updateTiming(*pending.context_id));
  EXPECT_FALSE(authority.changeBufferMastersTimingOnly(*physical.context_id, {{.node_id = 2U, .cell_master = "SLOWBUF"}}));
  ASSERT_TRUE(authority.discardContextTransaction(*pending.context_id));
  EXPECT_FALSE(authority.queryAnalysisStatus(*pending.context_id).has_value());
  EXPECT_DOUBLE_EQ(*authority.queryClockNodeArrival(*physical.context_id, 3U), *physical_arrival);
  const auto discarded_summary = authority.queryTimingSummary(*physical.context_id);
  if (!discarded_summary.has_value()) {
    FAIL() << "Expected discarded_summary to have a value.";
  }
  // Exact summary time is restored by the journal. A rollback recomputation
  // would publish a different elapsed time even if arrival happened to match.
  EXPECT_DOUBLE_EQ(discarded_summary->runtime_s, complete_before->runtime_s);
  EXPECT_EQ(discarded_summary->used_full_rebuild, complete_before->used_full_rebuild);
  const auto accepted = authority.beginContextTransaction(*physical.context_id);
  if (!accepted.context_id.has_value()) {
    FAIL() << accepted.failure_reason;
  }
  ASSERT_TRUE(authority.changeInstanceMasters(*accepted.context_id, {{.inst_name = "buf", .cell_master = "SLOWBUF"}}));
  EXPECT_DOUBLE_EQ(*authority.queryClockNodeArrival(*physical.context_id, 3U), *physical_arrival);
  ASSERT_TRUE(authority.commitContextTransaction(*accepted.context_id));
  EXPECT_FALSE(authority.queryAnalysisStatus(*physical.context_id).has_value());
  EXPECT_DOUBLE_EQ(*authority.queryClockNodeArrival(*accepted.context_id, 3U), *pending_arrival);
  const auto accepted_status = authority.queryAnalysisStatus(*accepted.context_id);
  ASSERT_TRUE(accepted_status.has_value());
  EXPECT_TRUE(accepted_status->timing_valid);
  EXPECT_TRUE(accepted_status->power_valid);

  authority.reset();
  authority.bindEnvironment(environment);
  const auto rebuilt = authority.buildContext(input);
  if (!rebuilt.context_id.has_value()) {
    ADD_FAILURE() << "Expected a rebuilt context identity.";
    return;
  }
  EXPECT_GT(*rebuilt.context_id, *physical.context_id);
  EXPECT_FALSE(authority.queryAnalysisStatus(*physical.context_id).has_value());
}

TEST(FastSTATest, ClockTopologySpliceRetainsLogicIdentityAndRollsBackWithoutRebuild)
{
  const auto path = (std::filesystem::path(__FILE__).parent_path() / "FastSTAContracts.lib").string();
  dmInst->get_config().set_lib_paths({path});
  ASSERT_TRUE(dmInst->readLib({path}));
  idb::IdbLayout layout;
  layout.get_units()->set_microns_dbu(1000);
  for (const auto* name : {"GOODBUF", "SLOWBUF", "BADBUF", "SEQ"}) {
    auto* master = layout.get_cell_master_list()->set_cell_master(name);
    master->set_width(1000U);
    master->set_height(1000U);
  }
  icts::Wrapper wrapper;
  wrapper.set_idb_layout(&layout);
  icts::Pin source("clk", icts::PinType::kIn, {0, 0}, nullptr, nullptr, true);
  icts::Inst sink_inst("end", "SEQ", icts::InstType::kFlipFlop, {2000, 0});
  icts::Pin sink("CLK", icts::PinType::kIn, {2000, 0}, &sink_inst);
  sink_inst.add_pin(&sink);
  icts::Net root("root");
  root.set_driver(&source);
  root.set_loads({&sink});
  icts::Clock original_clock("ROOT", "root");
  original_clock.set_clock_period_ns(1.0);
  original_clock.set_clock_source(&source);
  original_clock.set_clock_source_net(&root);
  original_clock.add_load(&sink);
  original_clock.add_net(&root);
  const icts::FastStaClockRouteGeometry original_route{
      .design_dbu_per_um = 1000,
      .clock_nets = {{.net_name = "root",
                      .routed_segments = {{.begin = {0, 0}, .end = {2000, 0}, .resistance_ohm = 20, .capacitance_pf = .02, .electrical_values_valid = true}}}}};
  icts::WrapperTimingGraph graph;
  graph.status = icts::WrapperTimingGraphStatus::kComplete;
  graph.nodes = {{.pin_name = "clk", .input = true, .top_level = true},
                 {.pin_name = "end/CLK",
                  .inst_name = "end",
                  .cell_master = "SEQ",
                  .x_dbu = 2000,
                  .input_cap_pf = .01,
                  .input_cap_pf_by_timing = {{{.01, .01}, {.01, .01}}},
                  .input = true,
                  .port_name = "CLK",
                  .clock_pin = true,
                  .input_cap_profile_available = true},
                 {.pin_name = "din", .y_dbu = 1000, .input = true, .top_level = true},
                 {.pin_name = "logic/A",
                  .inst_name = "logic",
                  .cell_master = "GOODBUF",
                  .x_dbu = 1000,
                  .y_dbu = 1000,
                  .input = true,
                  .port_name = "A",
                  .input_cap_profile_available = true},
                 {.pin_name = "logic/Y",
                  .inst_name = "logic",
                  .cell_master = "GOODBUF",
                  .x_dbu = 1000,
                  .y_dbu = 1000,
                  .output = true,
                  .port_name = "Y",
                  .logic_function = "A"},
                 {.pin_name = "dout",
                  .x_dbu = 2000,
                  .y_dbu = 1000,
                  .input_cap_pf = .01,
                  .input_cap_pf_by_timing = {{{.01, .01}, {.01, .01}}},
                  .output = true,
                  .top_level = true,
                  .input_cap_profile_available = true},
                 {.pin_name = "end/Q", .inst_name = "end", .cell_master = "SEQ", .x_dbu = 2000, .output = true, .port_name = "Q", .logic_function = "IQ"},
                 {.pin_name = "end/D",
                  .inst_name = "end",
                  .cell_master = "SEQ",
                  .x_dbu = 2000,
                  .y_dbu = 1000,
                  .input_cap_pf = .01,
                  .input_cap_pf_by_timing = {{{.01, .01}, {.01, .01}}},
                  .input = true,
                  .port_name = "D",
                  .input_cap_profile_available = true}};
  graph.nets
      = {{.net_name = "root",
          .driver_pin = "clk",
          .load_pins = {"end/CLK"},
          .rc_segments = {{.begin_x_dbu = 0, .end_x_dbu = 2000, .resistance_ohm = 20, .capacitance_pf = .02}}},
         {.net_name = "data_in",
          .driver_pin = "end/Q",
          .load_pins = {"logic/A"},
          .rc_segments = {{.begin_x_dbu = 2000, .end_x_dbu = 1000, .resistance_ohm = 10, .capacitance_pf = .01},
                          {.begin_x_dbu = 1000, .end_x_dbu = 1000, .end_y_dbu = 1000, .resistance_ohm = 10, .capacitance_pf = .01}}},
         {.net_name = "data_out",
          .driver_pin = "logic/Y",
          .load_pins = {"dout", "end/D"},
          .rc_segments = {{.begin_x_dbu = 1000, .begin_y_dbu = 1000, .end_x_dbu = 2000, .end_y_dbu = 1000, .resistance_ohm = 10, .capacitance_pf = .01}}}};
  graph.arcs = {{.inst_name = "logic",
                 .cell_master = "GOODBUF",
                 .input_port = "A",
                 .output_port = "Y",
                 .input_pin = "logic/A",
                 .output_pin = "logic/Y",
                 .positive_unate = true}};
  graph.launches = {{.inst_name = "end", .cell_master = "SEQ", .clock_port = "CLK", .output_port = "Q", .clock_pin = "end/CLK", .output_pin = "end/Q"}};
  graph.checks = {{.inst_name = "end",
                   .cell_master = "SEQ",
                   .clock_port = "CLK",
                   .data_port = "D",
                   .clock_pin = "end/CLK",
                   .data_pin = "end/D",
                   .kind = icts::WrapperTimingCheckKind::kSetup},
                  {.inst_name = "end",
                   .cell_master = "SEQ",
                   .clock_port = "CLK",
                   .data_port = "D",
                   .clock_pin = "end/CLK",
                   .data_pin = "end/D",
                   .kind = icts::WrapperTimingCheckKind::kHold}};
  icts::SdcClockData constraints;
  constraints.clocks.push_back({.clock_name = "ROOT",
                                .targets = {{.kind = icts::SdcObjectKind::kPort, .pattern = "clk"}},
                                .generated_sources = {},
                                .period_ns = 1,
                                .period_resolved = true,
                                .waveform_ns = {0, .5},
                                .generated_edges = {},
                                .generated_edge_shifts_ns = {},
                                .waveform_resolved = true});
  constraints.propagated_clocks.push_back({.kind = icts::SdcObjectKind::kClock, .pattern = "ROOT"});
  constraints.clocks.push_back({.clock_name = "VIRTUAL",
                                .targets = {},
                                .generated_sources = {},
                                .period_ns = 2,
                                .period_resolved = true,
                                .is_virtual = true,
                                .waveform_ns = {0, 1},
                                .generated_edges = {},
                                .generated_edge_shifts_ns = {},
                                .waveform_resolved = true});
  const icts::FastStaEnvironment environment{.wrapper = &wrapper, .dbu_per_um = 1000, .routing_layer = 1, .root_input_slew_ns = .03, .max_cap_pf = 3.0};
  icts::FastSTA authority;
  authority.bindEnvironment(environment);
  const icts::FastStaBuildInput original_input{
      .clock = &original_clock, .route_geometry = &original_route, .timing_graph = &graph, .constraints = &constraints, .require_power = true};
  const auto original = authority.buildContext(original_input);
  if (!original.context_id.has_value()) {
    FAIL() << original.failure_reason;
  }
  const auto original_points = authority.collectTimingPointFacts(*original.context_id);
  const auto original_relations = authority.collectTimingRelations(*original.context_id);
  ASSERT_FALSE(original_relations.empty());
  const auto original_summary = authority.queryTimingSummary(*original.context_id);
  if (!original_summary.has_value()) {
    FAIL() << "Expected original_summary to have a value.";
  }
  const auto unchanged = authority.beginContextTransaction(*original.context_id);
  if (!unchanged.context_id.has_value()) {
    FAIL() << unchanged.failure_reason;
  }
  ASSERT_TRUE(authority.synchronizeClockContext(*unchanged.context_id, original_input).ok());
  const auto unchanged_summary = authority.queryTimingSummary(*unchanged.context_id);
  ASSERT_TRUE(unchanged_summary.has_value());
  EXPECT_DOUBLE_EQ(unchanged_summary->runtime_s, original_summary->runtime_s);
  auto changed_constraints = constraints;
  changed_constraints.clock_transitions.push_back({.clocks = {{.kind = icts::SdcObjectKind::kClock, .pattern = "ROOT"}}, .value_ns = .2});
  auto changed_input = original_input;
  changed_input.constraints = &changed_constraints;
  const auto stale_constraints = authority.synchronizeClockContext(*unchanged.context_id, changed_input);
  EXPECT_FALSE(stale_constraints.ok());
  EXPECT_EQ(stale_constraints.failure_reason, "clock_topology_constraint_input_changed");
  changed_input = original_input;
  changed_input.propagate_all_clocks = true;
  EXPECT_FALSE(authority.synchronizeClockContext(*unchanged.context_id, changed_input).ok());
  const auto restored_summary = authority.queryTimingSummary(*unchanged.context_id);
  ASSERT_TRUE(restored_summary.has_value());
  EXPECT_DOUBLE_EQ(restored_summary->runtime_s, original_summary->runtime_s);
  ASSERT_TRUE(authority.discardContextTransaction(*unchanged.context_id));
  auto timing_only_input = original_input;
  timing_only_input.require_power = false;
  const auto timing_only = authority.buildContext(timing_only_input);
  if (!timing_only.context_id.has_value()) {
    FAIL() << timing_only.failure_reason;
  }
  const auto incomplete_power = authority.beginContextTransaction(*timing_only.context_id);
  if (!incomplete_power.context_id.has_value()) {
    FAIL() << incomplete_power.failure_reason;
  }
  const auto missing_power = authority.synchronizeClockContext(*incomplete_power.context_id, original_input);
  EXPECT_FALSE(missing_power.ok());
  EXPECT_EQ(missing_power.failure_reason, "clock_topology_complete_power_unavailable");
  const auto incomplete_power_status = authority.queryAnalysisStatus(*incomplete_power.context_id);
  ASSERT_TRUE(incomplete_power_status.has_value());
  EXPECT_TRUE(incomplete_power_status->timing_valid);
  EXPECT_FALSE(incomplete_power_status->power_valid);
  ASSERT_TRUE(authority.discardContextTransaction(*incomplete_power.context_id));

  icts::Inst buffer("cts", "GOODBUF", icts::InstType::kBuffer, {1000, 0});
  icts::Pin buffer_in("A", icts::PinType::kIn, {1000, 0}, &buffer);
  icts::Pin buffer_out("Y", icts::PinType::kOut, {1000, 0}, &buffer);
  buffer.add_pin(&buffer_in);
  buffer.add_pin(&buffer_out);
  icts::Net next_root("root");
  next_root.set_driver(&source);
  next_root.set_loads({&buffer_in});
  icts::Net leaf("leaf");
  leaf.set_driver(&buffer_out);
  leaf.set_loads({&sink});
  icts::Clock next_clock("ROOT", "root");
  next_clock.set_clock_period_ns(1.0);
  next_clock.set_clock_source(&source);
  next_clock.set_clock_source_net(&next_root);
  next_clock.add_load(&sink);
  next_clock.add_net(&next_root);
  next_clock.add_net(&leaf);
  ASSERT_TRUE(next_clock.addPropagationArc({.inst = &buffer, .input_pin = &buffer_in, .output_pin = &buffer_out}).ok());
  const icts::FastStaClockRouteGeometry next_route{
      .design_dbu_per_um = 1000,
      .clock_nets
      = {{.net_name = "root",
          .routed_segments = {{.begin = {0, 0}, .end = {1000, 0}, .resistance_ohm = 10, .capacitance_pf = .01, .electrical_values_valid = true}}},
         {.net_name = "leaf",
          .routed_segments = {{.begin = {1000, 0}, .end = {2000, 0}, .resistance_ohm = 10, .capacitance_pf = .01, .electrical_values_valid = true}}}}};
  const icts::FastStaBuildInput next_input{
      .clock = &next_clock, .route_geometry = &next_route, .timing_graph = &graph, .constraints = &constraints, .require_power = true};
  const auto cold = authority.buildContext(next_input);
  if (!cold.context_id.has_value()) {
    FAIL() << cold.failure_reason;
  }
  const auto cold_points = authority.collectTimingPointFacts(*cold.context_id);
  const auto cold_pi = authority.collectPiElmoreFacts(*cold.context_id);
  const auto cold_relations = authority.collectTimingRelations(*cold.context_id);
  const auto compare_points = [&](const auto& expected, const auto& actual) -> void {
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
  const auto synchronized = authority.synchronizeClockContext(*pending.context_id, next_input);
  ASSERT_TRUE(synchronized.ok()) << synchronized.failure_reason;
  compare_points(cold_points, authority.collectTimingPointFacts(*pending.context_id));
  compare_points(original_points, authority.collectTimingPointFacts(*original.context_id));
  compare_points(cold_points, authority.collectTimingPointFacts(*pending.context_id));
  const auto changed_relations = authority.collectTimingRelations(*pending.context_id);
  ASSERT_EQ(cold_relations.size(), changed_relations.size());
  for (std::size_t index = 0U; index < cold_relations.size(); ++index) {
    const auto& actual = changed_relations.at(index);
    EXPECT_EQ(actual.relation_id, cold_relations.at(index).relation_id);
    EXPECT_DOUBLE_EQ(actual.arrival_ns, cold_relations.at(index).arrival_ns);
    EXPECT_DOUBLE_EQ(actual.required_ns, cold_relations.at(index).required_ns);
    EXPECT_DOUBLE_EQ(actual.slack_ns, cold_relations.at(index).slack_ns);
    EXPECT_EQ(actual.launch_node_id, original_relations.at(index).launch_node_id);
    EXPECT_EQ(actual.capture_node_id, original_relations.at(index).capture_node_id);
  }
  EXPECT_NE(changed_relations.front().arrival_ns, original_relations.front().arrival_ns);
  // A virtual clock declares no sinks and carried no constraint of its own once
  // external I/O timing left the input surface, so it owns no relation here.
  EXPECT_FALSE(std::ranges::any_of(
      changed_relations, [](const auto& relation) -> bool { return relation.launch_clock_name == "ROOT" && relation.capture_clock_name == "VIRTUAL"; }));
  const auto changed_pi = authority.collectPiElmoreFacts(*pending.context_id);
  ASSERT_EQ(cold_pi.size(), changed_pi.size());
  for (const auto& expected : cold_pi) {
    const auto found = std::ranges::find_if(changed_pi, [&](const auto& actual) -> bool {
      return expected.net_name == actual.net_name && expected.load_pin_name == actual.load_pin_name && expected.analysis == actual.analysis
             && expected.transition == actual.transition;
    });
    ASSERT_NE(found, changed_pi.end());
    EXPECT_DOUBLE_EQ(expected.near_cap_pf, found->near_cap_pf);
    EXPECT_DOUBLE_EQ(expected.far_cap_pf, found->far_cap_pf);
    EXPECT_DOUBLE_EQ(expected.resistance_ohm, found->resistance_ohm);
    EXPECT_DOUBLE_EQ(expected.elmore_ns, found->elmore_ns);
  }
  ASSERT_TRUE(authority.discardContextTransaction(*pending.context_id));
  compare_points(original_points, authority.collectTimingPointFacts(*original.context_id));
  const auto final_summary = authority.queryTimingSummary(*original.context_id);
  ASSERT_TRUE(final_summary.has_value());
  EXPECT_DOUBLE_EQ(final_summary->runtime_s, original_summary->runtime_s);
  const auto accepted = authority.beginContextTransaction(*original.context_id);
  if (!accepted.context_id.has_value()) {
    FAIL() << accepted.failure_reason;
  }
  ASSERT_TRUE(authority.synchronizeClockContext(*accepted.context_id, next_input).ok());
  ASSERT_TRUE(authority.commitContextTransaction(*accepted.context_id));
  EXPECT_FALSE(authority.queryAnalysisStatus(*original.context_id).has_value());
  compare_points(cold_points, authority.collectTimingPointFacts(*accepted.context_id));
  const auto initial_sizing = authority.buildClockSizingContext(*accepted.context_id, next_input);
  if (!initial_sizing.context_id.has_value()) {
    FAIL() << initial_sizing.failure_reason;
  }
  EXPECT_DOUBLE_EQ(authority.querySkew(*initial_sizing.context_id).skew_ns, authority.querySkew(*accepted.context_id).skew_ns);
  const auto sizing_power = authority.queryPower(*initial_sizing.context_id);
  const auto prepared_power = authority.queryPower(*accepted.context_id);
  if (!sizing_power.has_value()) {
    FAIL() << "Expected sizing_power to have a value.";
  }
  if (!prepared_power.has_value()) {
    FAIL() << "Expected prepared_power to have a value.";
  }
  EXPECT_DOUBLE_EQ(sizing_power->switching_power_w, prepared_power->switching_power_w);
  EXPECT_DOUBLE_EQ(sizing_power->internal_power_w, prepared_power->internal_power_w);
  EXPECT_DOUBLE_EQ(sizing_power->leakage_power_w, prepared_power->leakage_power_w);
  EXPECT_DOUBLE_EQ(sizing_power->area_um2, prepared_power->area_um2);

  // A master edit changes clock slew as well as latency. The sequential
  // Liberty tables must be reevaluated before publishing data/check facts.
  const auto accepted_summary = authority.queryTimingSummary(*accepted.context_id);
  if (!accepted_summary.has_value()) {
    FAIL() << "Expected accepted_summary to have a value.";
  }
  const auto previous_stages = authority.collectTimingStageFacts(*accepted.context_id);
  buffer.set_cell_master("SLOWBUF");
  const auto sized_cold = authority.buildContext(next_input);
  if (!sized_cold.context_id.has_value()) {
    FAIL() << sized_cold.failure_reason;
  }
  const auto sized = authority.beginContextTransaction(*accepted.context_id);
  if (!sized.context_id.has_value()) {
    FAIL() << sized.failure_reason;
  }
  ASSERT_TRUE(authority.changeInstanceMasters(*sized.context_id, {{.inst_name = "cts", .cell_master = "SLOWBUF"}}));
  const auto sized_points = authority.collectTimingPointFacts(*sized.context_id);
  compare_points(authority.collectTimingPointFacts(*sized_cold.context_id), sized_points);
  compare_points(cold_points, authority.collectTimingPointFacts(*accepted.context_id));
  const auto sized_summary = authority.queryTimingSummary(*sized.context_id);
  if (!sized_summary.has_value()) {
    FAIL() << "Expected sized_summary to have a value.";
  }
  EXPECT_FALSE(sized_summary->used_full_rebuild);
  EXPECT_GT(sized_summary->updated_logic_node_count, 0U);
  const auto sized_relations = authority.collectTimingRelations(*sized.context_id);
  const auto sized_cold_relations = authority.collectTimingRelations(*sized_cold.context_id);
  ASSERT_EQ(sized_relations.size(), sized_cold_relations.size());
  bool setup_checked = false;
  bool hold_checked = false;
  for (std::size_t index = 0U; index < sized_relations.size(); ++index) {
    const auto& actual = sized_relations.at(index);
    const auto& expected = sized_cold_relations.at(index);
    EXPECT_EQ(actual.relation_id, expected.relation_id);
    EXPECT_DOUBLE_EQ(actual.arrival_ns, expected.arrival_ns);
    EXPECT_DOUBLE_EQ(actual.required_ns, expected.required_ns);
    EXPECT_DOUBLE_EQ(actual.slack_ns, expected.slack_ns);
    EXPECT_DOUBLE_EQ(actual.requirement_ns, expected.requirement_ns);
    setup_checked |= !actual.output_delay && actual.check == icts::FastStaTimingCheckKind::kSetup;
    hold_checked |= !actual.output_delay && actual.check == icts::FastStaTimingCheckKind::kHold;
  }
  EXPECT_TRUE(setup_checked);
  EXPECT_TRUE(hold_checked);
  const auto sized_stages = authority.collectTimingStageFacts(*sized.context_id);
  bool launch_delay_changed = false;
  for (const auto& stage : sized_stages) {
    if (stage.pin_name != "end/Q") {
      continue;
    }
    const auto previous = std::ranges::find_if(previous_stages, [&](const auto& point) -> bool {
      return point.pin_name == stage.pin_name && point.analysis == stage.analysis && point.transition == stage.transition;
    });
    ASSERT_NE(previous, previous_stages.end());
    launch_delay_changed |= stage.stage_delay_ns != previous->stage_delay_ns && stage.input_slew_ns != previous->input_slew_ns;
  }
  EXPECT_TRUE(launch_delay_changed);
  EXPECT_FALSE(authority.changeInstanceMasters(*sized.context_id, {{.inst_name = "cts", .cell_master = "BADBUF"}}));
  compare_points(sized_points, authority.collectTimingPointFacts(*sized.context_id));
  ASSERT_TRUE(authority.discardContextTransaction(*sized.context_id));
  compare_points(cold_points, authority.collectTimingPointFacts(*accepted.context_id));
  const auto accepted_runtime = authority.queryTimingSummary(*accepted.context_id);
  ASSERT_TRUE(accepted_runtime.has_value());
  EXPECT_DOUBLE_EQ(accepted_runtime->runtime_s, accepted_summary->runtime_s);

  // Resynthesis replaces the old CTS suffix while preserving every input ID.
  buffer.set_name("cts_replaced");
  buffer.set_cell_master("SLOWBUF");
  const auto replaced_cold = authority.buildContext(next_input);
  if (!replaced_cold.context_id.has_value()) {
    FAIL() << replaced_cold.failure_reason;
  }
  const auto resynthesis = authority.beginContextTransaction(*accepted.context_id);
  if (!resynthesis.context_id.has_value()) {
    FAIL() << resynthesis.failure_reason;
  }
  const auto replaced = authority.synchronizeClockContext(*resynthesis.context_id, next_input);
  ASSERT_TRUE(replaced.ok()) << replaced.failure_reason;
  compare_points(authority.collectTimingPointFacts(*replaced_cold.context_id), authority.collectTimingPointFacts(*resynthesis.context_id));
  ASSERT_TRUE(authority.discardContextTransaction(*resynthesis.context_id));
  compare_points(cold_points, authority.collectTimingPointFacts(*accepted.context_id));
  const auto failed = authority.beginContextTransaction(*accepted.context_id);
  if (!failed.context_id.has_value()) {
    FAIL() << failed.failure_reason;
  }
  buffer.set_cell_master("BADBUF");
  const auto rejected = authority.synchronizeClockContext(*failed.context_id, next_input);
  EXPECT_FALSE(rejected.ok());
  EXPECT_TRUE(rejected.failure_reason.starts_with("clock_topology_analysis_failed:")) << rejected.failure_reason;
  compare_points(cold_points, authority.collectTimingPointFacts(*failed.context_id));
  compare_points(cold_points, authority.collectTimingPointFacts(*accepted.context_id));
  ASSERT_TRUE(authority.discardContextTransaction(*failed.context_id));
  buffer.set_cell_master("SLOWBUF");
  const auto replaced_commit = authority.beginContextTransaction(*accepted.context_id);
  if (!replaced_commit.context_id.has_value()) {
    FAIL() << replaced_commit.failure_reason;
  }
  ASSERT_TRUE(authority.synchronizeClockContext(*replaced_commit.context_id, next_input).ok());
  ASSERT_TRUE(authority.commitContextTransaction(*replaced_commit.context_id));
  EXPECT_FALSE(authority.queryAnalysisStatus(*accepted.context_id).has_value());
  compare_points(authority.collectTimingPointFacts(*replaced_cold.context_id), authority.collectTimingPointFacts(*replaced_commit.context_id));

  // DataManager's virtual-clock entry has no physical owner, but contains
  // the physical clock overlay and all cross/virtual-clock relations.
  icts::Design original_views;
  *original_views.makeClock("ROOT", "root") = original_clock;
  icts::Design next_views;
  *next_views.makeClock("ROOT", "root") = next_clock;
  auto original_virtual_input = original_input;
  original_virtual_input.clock = nullptr;
  original_virtual_input.committed_design = &original_views;
  auto next_virtual_input = next_input;
  next_virtual_input.clock = nullptr;
  next_virtual_input.committed_design = &next_views;
  const auto original_virtual = authority.buildContext(original_virtual_input);
  if (!original_virtual.context_id.has_value()) {
    FAIL() << original_virtual.failure_reason;
  }
  const auto cold_virtual = authority.buildContext(next_virtual_input);
  if (!cold_virtual.context_id.has_value()) {
    FAIL() << cold_virtual.failure_reason;
  }
  const auto pending_virtual = authority.beginContextTransaction(*original_virtual.context_id);
  if (!pending_virtual.context_id.has_value()) {
    FAIL() << pending_virtual.failure_reason;
  }
  const auto virtual_sync = authority.synchronizeClockContext(*pending_virtual.context_id, next_virtual_input);
  ASSERT_TRUE(virtual_sync.ok()) << virtual_sync.failure_reason;
  compare_points(authority.collectTimingPointFacts(*cold_virtual.context_id), authority.collectTimingPointFacts(*pending_virtual.context_id));
  ASSERT_TRUE(authority.commitContextTransaction(*pending_virtual.context_id));
  EXPECT_FALSE(authority.queryAnalysisStatus(*original_virtual.context_id).has_value());
  const auto virtual_relations = authority.collectTimingRelations(*pending_virtual.context_id);
  ASSERT_FALSE(virtual_relations.empty());
  // Only the physical clock owns relations; the virtual entry contributes its
  // overlay and no timing check of its own.
  EXPECT_FALSE(std::ranges::any_of(virtual_relations, [](const auto& relation) -> bool { return relation.capture_clock_name == "VIRTUAL"; }));

  auto resident = icts::FastStaBuilder::buildContext(environment, original_input);
  if (!resident.context.has_value()) {
    FAIL() << resident.failure_reason;
  }
  ASSERT_TRUE(icts::FastStaTiming::update(*resident.context));
  const auto logic_id = resident.context->node_id_by_name.at("logic/Y");
  const auto data_net_id = resident.context->net_id_by_name.at("data_out");
  const auto* rc_storage = resident.context->nets.at(data_net_id).parasitic.rc_nodes.data();
  auto overlay_input = next_input;
  overlay_input.timing_graph = nullptr;
  auto overlay = icts::FastStaBuilder::buildContext(environment, overlay_input);
  if (!overlay.context.has_value()) {
    FAIL() << overlay.failure_reason;
  }
  ASSERT_FALSE(icts::FastStaBuilder::spliceClockContext(*resident.context, std::move(*overlay.context), constraints).has_value());
  EXPECT_EQ(resident.context->node_id_by_name.at("logic/Y"), logic_id);
  EXPECT_EQ(resident.context->net_id_by_name.at("data_out"), data_net_id);
  EXPECT_EQ(resident.context->nets.at(data_net_id).parasitic.rc_nodes.data(), rc_storage);
}

}  // namespace
}  // namespace icts_test
