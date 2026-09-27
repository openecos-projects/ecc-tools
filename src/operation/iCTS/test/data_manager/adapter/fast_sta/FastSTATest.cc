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
 * @file FastSTATest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-09-13
 * @brief Numerical timing, parasitic and power regression tests.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "FastSTA.hh"
#include "FastSTADmpCeff.hh"
#include "FastSTAIncremental.hh"
#include "FastSTAParasitics.hh"
#include "FastSTAPower.hh"
#include "FastSTATiming.hh"
#include "clock_state/FastSTABuilder.hh"
#include "clock_tree/FastSTAClockTree.hh"
#include "data_manager/DataManager.hh"
#include "data_manager/config/Config.hh"
#include "design/Clock.hh"
#include "design/Inst.hh"
#include "design/Net.hh"
#include "design/Pin.hh"
#include "fixture/FastSTATestModel.hh"
#include "liberty/FastSTALibertyModel.hh"
#include "timing/FastSTAClockTiming.hh"

namespace icts_test {
namespace {

auto MakeReferenceBufferTable(icts::FastStaLibertyTableKind kind, double low_slew_low_cap, double low_slew_high_cap, double high_slew_low_cap,
                              double high_slew_high_cap, icts::FastStaTransition transition = icts::FastStaTransition::kRise) -> icts::FastStaLibertyTable
{
  return icts::FastStaLibertyTable{
      .kind = kind,
      .transition = transition,
      .axes = {MakeAxis(icts::FastStaLibertyAxisKind::kInputSlew, {0.1, 0.5}), MakeAxis(icts::FastStaLibertyAxisKind::kOutputLoad, {0.1, 1.0})},
      .values = {low_slew_low_cap, low_slew_high_cap, high_slew_low_cap, high_slew_high_cap},
  };
}

auto MakeReferenceBufferCell() -> icts::FastStaLibertyCell
{
  return icts::FastStaLibertyCell{
      .library_name = "test_lib",
      .cell_master = "BUF_X1",
      .input_port = "A",
      .output_port = "Y",
      .input_cap_pf = 0.2,
      .output_cap_limit_pf = 3.0,
      .input_threshold_rise = 0.5,
      .input_threshold_fall = 0.5,
      .output_threshold_rise = 0.5,
      .output_threshold_fall = 0.5,
      .slew_lower_threshold_rise = 0.3,
      .slew_lower_threshold_fall = 0.3,
      .slew_upper_threshold_rise = 0.7,
      .slew_upper_threshold_fall = 0.7,
      .slew_derate_from_library = 1.0,
      .area_um2 = 1.0,
      .voltage_v = 1.0,
      .leakage_power_w = 0.00001,
      .timing_arc = icts::FastStaLibertyArc{
          .library_name = {},
          .from_port = "A",
          .to_port = "Y",
          .delay_tables = {MakeReferenceBufferTable(icts::FastStaLibertyTableKind::kCellDelay, 0.110, 0.200, 0.150, 0.240),
                           MakeReferenceBufferTable(icts::FastStaLibertyTableKind::kCellDelay, 0.110, 0.200, 0.150, 0.240, icts::FastStaTransition::kFall)},
          .slew_tables = {MakeReferenceBufferTable(icts::FastStaLibertyTableKind::kOutputSlew, 0.210, 0.300, 0.250, 0.340),
                          MakeReferenceBufferTable(icts::FastStaLibertyTableKind::kOutputSlew, 0.210, 0.300, 0.250, 0.340, icts::FastStaTransition::kFall)},
          .internal_power_tables = {MakeReferenceBufferTable(icts::FastStaLibertyTableKind::kInternalPower, 0.510, 0.600, 0.550, 0.640)},
      },
      .timing_arcs = {},
  };
}

TEST(FastSTATest, InvalidEnvironmentFailsClosedBeforeContextBuild)
{
  const auto result = icts::FastStaBuilder::buildContext(icts::FastStaEnvironment{}, icts::FastStaBuildInput{});

  EXPECT_FALSE(result.context.has_value());
  EXPECT_EQ(result.failure_reason, "fast_sta_environment_wrapper_unavailable");
}

class ScopedRootInputSlew
{
 public:
  explicit ScopedRootInputSlew(double root_input_slew_ns) : _original_root_input_slew_ns(CTSDM.getConfig().get_root_input_slew())
  {
    CTSDM.getConfig().set_root_input_slew(root_input_slew_ns);
  }

  ~ScopedRootInputSlew() { CTSDM.getConfig().set_root_input_slew(_original_root_input_slew_ns); }

  ScopedRootInputSlew(const ScopedRootInputSlew& rhs) = delete;
  ScopedRootInputSlew(ScopedRootInputSlew&& rhs) = delete;
  auto operator=(const ScopedRootInputSlew& rhs) -> ScopedRootInputSlew& = delete;
  auto operator=(ScopedRootInputSlew&& rhs) -> ScopedRootInputSlew& = delete;

 private:
  double _original_root_input_slew_ns = 0.0;
};

auto MakeTwoBufferReferenceContext() -> icts::FastStaContext
{
  icts::FastStaContext context;
  context.clock_name = "clk";
  context.clock_net_name = "clk";
  context.clock_period_ns = 10.0;
  context.liberty_cell_by_master["BUF_X1"] = MakeReferenceBufferCell();

  context.source_node_id = 0U;
  context.nodes = {
      MakeNode(icts::FastStaNodeKind::kSource, "clk", "", "clk", "", {}, 0.0, icts::kInvalidFastStaNetId, {0U}),
      MakeNode(icts::FastStaNodeKind::kBufferInput, "u_buf/A", "u_buf", "A", "BUF_X1", {}, 0.20, 0U, {}),
      MakeNode(icts::FastStaNodeKind::kBufferOutput, "u_buf/Y", "u_buf", "Y", "BUF_X1", {}, 0.0, icts::kInvalidFastStaNetId, {1U}),
      MakeNode(icts::FastStaNodeKind::kBufferInput, "u_leaf/A", "u_leaf", "A", "BUF_X1", {}, 0.20, 1U, {}),
      MakeNode(icts::FastStaNodeKind::kBufferOutput, "u_leaf/Y", "u_leaf", "Y", "BUF_X1", {}, 0.0, icts::kInvalidFastStaNetId, {2U}),
      MakeNode(icts::FastStaNodeKind::kSink, "sink", "", "sink", "", {}, 0.0, 2U, {}),
  };
  context.node_id_by_name = {{"clk", 0U}, {"u_buf/A", 1U}, {"u_buf/Y", 2U}, {"u_leaf/A", 3U}, {"u_leaf/Y", 4U}, {"sink", 5U}};
  context.buffer_input_node_id_by_inst = {{"u_buf", 1U}, {"u_leaf", 3U}};
  context.buffer_output_node_id_by_inst = {{"u_buf", 2U}, {"u_leaf", 4U}};
  context.nets = {
      MakeNet("clk", 0U, {1U}, 3.0),
      MakeNet("leaf", 2U, {3U}, 3.0,
              MakeParasitic({MakeRcNode("leaf@root", 0.80, 0.0, 0.80, 0.0, 2U), MakeRcNode("leaf@u_leaf/A", 0.0, 0.20, 0.20, 0.15, 3U)}, {}, 0U,
                            icts::FastStaPiModel{.near_cap_pf = 0.20, .resistance_ohm = 1000.0, .far_cap_pf = 0.80}, 1.0, true)),
      MakeNet("sink", 4U, {5U}, 3.0,
              MakeParasitic({MakeRcNode("sink@root", 0.50, 0.0, 0.50, 0.0, 4U), MakeRcNode("sink@load", 0.0, 0.0, 0.0, 0.08, 5U)}, {}, 0U,
                            icts::FastStaPiModel{.near_cap_pf = 0.10, .resistance_ohm = 1000.0, .far_cap_pf = 0.40}, 0.50, true)),
  };
  context.net_id_by_name = {{"clk", 0U}, {"leaf", 1U}, {"sink", 2U}};
  return context;
}

TEST(FastSTATest, LibertyTableBilinearLookupInterpolates)
{
  const auto table = MakeTable(icts::FastStaLibertyTableKind::kCellDelay, 1.0);
  const auto value = table.lookup(0.5, 1.0);

  if (!value.has_value()) {
    ADD_FAILURE() << "Expected bilinear lookup result.";
    return;
  }
  EXPECT_NEAR(*value, 1.15, 1e-12);
}

TEST(FastSTATest, LibertyTableRejectsMalformedShape)
{
  auto table = MakeTable(icts::FastStaLibertyTableKind::kCellDelay, 1.0);
  table.values.pop_back();

  EXPECT_FALSE(table.valid());
  EXPECT_FALSE(table.lookup(0.5, 1.0).has_value());
}

TEST(FastSTATest, ClockContextBuildDoesNotRequireBufferModelForNonPropagationSink)
{
  icts::Clock clock("clk", "clk_net");
  clock.set_clock_period_ns(10.0);
  icts::Inst sink_inst("u_sink", "DFFQX1H7L", icts::InstType::kFlipFlop, icts::Point<int>(100, 200));
  icts::Pin sink_pin("CK", icts::PinType::kClock, icts::Point<int>(100, 200), &sink_inst);
  sink_inst.add_pin(&sink_pin);
  clock.add_load(&sink_pin);

  const auto context = icts::FastStaClockTree::buildFromClock(clock);
  ASSERT_EQ(context.nodes.size(), 1U);
  EXPECT_EQ(context.nodes.front().kind, icts::FastStaNodeKind::kSink);
  EXPECT_EQ(context.nodes.front().cell_master, "DFFQX1H7L");
  EXPECT_TRUE(context.liberty_cell_by_master.empty());
}

TEST(FastSTATest, PhysicalBufferBoundaryWithoutClockArcRemainsSinkNode)
{
  icts::Clock clock("clk", "clk_net");
  clock.set_clock_period_ns(10.0);
  icts::Inst boundary_inst("u_boundary", "BUF_X1", icts::InstType::kBuffer, icts::Point<int>(100, 200));
  icts::Pin boundary_input("A", icts::PinType::kIn, icts::Point<int>(100, 200), &boundary_inst);
  boundary_inst.add_pin(&boundary_input);
  clock.add_load(&boundary_input);

  const auto context = icts::FastStaClockTree::buildFromClock(clock);
  ASSERT_EQ(context.nodes.size(), 1U);
  EXPECT_EQ(context.nodes.front().kind, icts::FastStaNodeKind::kSink);
  EXPECT_TRUE(context.buffer_input_node_id_by_inst.empty());
  EXPECT_TRUE(context.buffer_output_node_id_by_inst.empty());
}

TEST(FastSTATest, DmpDriverTimingProducesCeffAndLoadSlew)
{
  const icts::FastStaPiModel pi{.near_cap_pf = 0.2, .resistance_ohm = 1000.0, .far_cap_pf = 0.8};
  const auto cell = MakeCell("BUF_X1", 0.20, 1.5, 0.01);

  const auto driver_timing = icts::FastStaDmpCeff::calcDriverTiming(cell, pi, icts::FastStaTransition::kRise, 0.2);

  ASSERT_TRUE(driver_timing.valid);
  EXPECT_GT(driver_timing.ceff_pf, 0.0);
  EXPECT_LE(driver_timing.ceff_pf, 1.0);
  EXPECT_GT(driver_timing.gate_delay_ns, 0.0);
  EXPECT_GT(driver_timing.driver_slew_ns, 0.0);

  const auto load_timing = icts::FastStaDmpCeff::calcLoadDelaySlew(driver_timing, 0.15, nullptr);
  EXPECT_TRUE(load_timing.valid);
  EXPECT_GT(load_timing.wire_delay_ns, 0.0);
  EXPECT_GE(load_timing.load_slew_ns, driver_timing.driver_slew_ns);
}

TEST(FastSTATest, DmpDriverTimingMatchesReferenceValues)
{
  const auto cell = MakeReferenceBufferCell();
  const icts::FastStaPiModel pi{.near_cap_pf = 0.2, .resistance_ohm = 1000.0, .far_cap_pf = 0.8};

  const auto driver_timing = icts::FastStaDmpCeff::calcDriverTiming(cell, pi, icts::FastStaTransition::kRise, 0.2);

  ASSERT_TRUE(driver_timing.valid);
  EXPECT_NEAR(driver_timing.ceff_pf, 0.433933, 1e-6) << "actual=" << driver_timing.ceff_pf;
  EXPECT_NEAR(driver_timing.gate_delay_ns, 0.153393, 1e-6) << "actual=" << driver_timing.gate_delay_ns;
  EXPECT_NEAR(driver_timing.driver_slew_ns, 0.265526, 1e-6) << "actual=" << driver_timing.driver_slew_ns;

  const auto load_timing = icts::FastStaDmpCeff::calcLoadDelaySlew(driver_timing, 0.15, nullptr);
  ASSERT_TRUE(load_timing.valid);
  EXPECT_NEAR(load_timing.wire_delay_ns, 0.143337, 1e-6) << "actual=" << load_timing.wire_delay_ns;
  EXPECT_NEAR(load_timing.load_slew_ns, 0.278977, 1e-6) << "actual=" << load_timing.load_slew_ns;
}

TEST(FastSTATest, TwoBufferPropagationMatchesReferenceValues)
{
  auto context = MakeTwoBufferReferenceContext();
  context.root_input_slew_ns = 0.2;
  ASSERT_TRUE(icts::FastStaTiming::update(context));

  ASSERT_TRUE(context.nodes.at(1U).timing.valid);
  ASSERT_TRUE(context.nodes.at(2U).timing.valid);
  ASSERT_TRUE(context.nodes.at(3U).timing.valid);
  ASSERT_TRUE(context.nodes.at(4U).timing.valid);
  ASSERT_TRUE(context.nodes.at(5U).timing.valid);

  EXPECT_NEAR(context.nodes.at(1U).timing.arrival_ns, 0.0, 1e-9);
  EXPECT_NEAR(context.nodes.at(1U).timing.slew_ns, 0.199999988, 1e-6);
  EXPECT_NEAR(context.nodes.at(2U).timing.arrival_ns, 0.153393298, 1e-6);
  EXPECT_NEAR(context.nodes.at(2U).timing.slew_ns, 0.265526026, 1e-6);
  EXPECT_NEAR(context.nodes.at(3U).timing.arrival_ns, 0.296730995, 1e-6);
  EXPECT_NEAR(context.nodes.at(3U).timing.slew_ns, 0.278976977, 1e-6);
  EXPECT_NEAR(context.nodes.at(4U).timing.arrival_ns, 0.443657875, 1e-6);
  EXPECT_NEAR(context.nodes.at(4U).timing.slew_ns, 0.254852772, 1e-6);
  EXPECT_NEAR(context.nodes.at(5U).timing.arrival_ns, 0.522887707, 1e-6);
  EXPECT_NEAR(context.nodes.at(5U).timing.slew_ns, 0.257477403, 1e-6);
}

TEST(FastSTATest, SignedSlackReducesTransitionsOncePerEndpoint)
{
  auto context = MakeClockAndLogicContext();
  context.clock_period_ns = 0.35;
  for (auto& arc : context.liberty_cell_by_master.at("BUF_X1").timing_arcs) {
    for (auto& table : arc.delay_tables) {
      const auto delay = table.transition == icts::FastStaTransition::kRise ? 0.10 : 0.15;
      std::ranges::fill(table.values, delay);
    }
    for (auto& table : arc.slew_tables) {
      std::ranges::fill(table.values, 0.05);
    }
  }
  context.timing_checks.back().requirement_override_ns = 0.40;

  ASSERT_TRUE(icts::FastStaTiming::update(context));
  ASSERT_EQ(context.timing_relations.size(), 4U);
  for (const auto& relation : context.timing_relations) {
    const auto rise = relation.data_transition == icts::FastStaTransition::kRise;
    const auto expected_setup = rise ? -0.05 : -0.15;
    const auto expected_hold = rise ? -0.20 : -0.10;
    const auto expected = relation.check == icts::FastStaTimingCheckKind::kSetup ? expected_setup : expected_hold;
    EXPECT_NEAR(relation.slack_ns, expected, 1e-12);
  }
  EXPECT_NEAR(context.timing_summary.setup_wns_ns, -0.15, 1e-12);
  EXPECT_NEAR(context.timing_summary.setup_tns_ns, -0.15, 1e-12);
  EXPECT_NEAR(context.timing_summary.hold_wns_ns, -0.20, 1e-12);
  EXPECT_NEAR(context.timing_summary.hold_tns_ns, -0.20, 1e-12);
  EXPECT_EQ(context.timing_summary.setup_violation_count, 1U);
  EXPECT_EQ(context.timing_summary.hold_violation_count, 1U);
}

TEST(FastSTATest, PositiveWnsRemainsSigned)
{
  auto context = MakeClockAndLogicContext();
  context.timing_checks.back().requirement_override_ns = 10.0;

  ASSERT_TRUE(icts::FastStaTiming::update(context));
  EXPECT_GT(context.timing_summary.setup_wns_ns, 0.0);
  // The same required time moves setup and hold in opposite directions:
  // setup is comfortably positive while hold remains a real negative check.
  EXPECT_LT(context.timing_summary.hold_wns_ns, 0.0);
  EXPECT_EQ(context.timing_summary.setup_violation_count, 0U);
  EXPECT_EQ(context.timing_summary.hold_violation_count, 1U);
}

TEST(FastSTATest, TimingUsesContextRootSlewWithoutConfigMutation)
{
  const ScopedRootInputSlew root_input_slew_guard(0.777);

  auto fast_context = MakeTwoBufferReferenceContext();
  fast_context.root_input_slew_ns = 0.12;
  ASSERT_TRUE(icts::FastStaTiming::update(fast_context));

  auto slow_context = MakeTwoBufferReferenceContext();
  slow_context.root_input_slew_ns = 0.48;
  ASSERT_TRUE(icts::FastStaTiming::update(slow_context));

  EXPECT_NEAR(fast_context.nodes.at(1U).timing.slew_ns, 0.12, 1e-12);
  EXPECT_NEAR(slow_context.nodes.at(1U).timing.slew_ns, 0.48, 1e-12);
  EXPECT_NEAR(CTSDM.getConfig().get_root_input_slew(), 0.777, 1e-12);
}

TEST(FastSTATest, TimingUpdateRejectsMissingDriverTimingTable)
{
  auto context = MakeTinyContext();
  context.liberty_cell_by_master.at("BUF_X1").timing_arc.delay_tables.clear();

  EXPECT_FALSE(icts::FastStaTiming::update(context));
  EXPECT_FALSE(context.timing_valid);
  EXPECT_FALSE(context.nodes.at(2U).timing.valid);
  EXPECT_FALSE(context.nodes.at(3U).timing.valid);
}

TEST(FastSTATest, ClockDriverResponsesPreserveEachElectricalStateAndArrival)
{
  for (const auto distinct_electrical_states : {false, true}) {
    auto context = MakeTinyContext();
    auto& sink = context.nodes.at(3U);
    sink.input_cap_profile_available = true;
    sink.input_cap_pf_by_timing = {{{0.1, 0.2}, {distinct_electrical_states ? 0.3 : 0.1, distinct_electrical_states ? 0.4 : 0.2}}};
    icts::FastStaBranchStates states{};
    for (std::size_t analysis = 0U; analysis < 2U; ++analysis) {
      for (std::size_t transition = 0U; transition < 2U; ++transition) {
        states.at(analysis).at(transition) = {
            .valid = true,
            .arrival_ns = analysis == 0U ? 0.1 : 0.5,
            .slew_ns = distinct_electrical_states && analysis == 1U ? 0.3 : 0.1,
            .source_transition = transition == 0U ? icts::FastStaTransition::kRise : icts::FastStaTransition::kFall,
        };
      }
    }
    ASSERT_TRUE(icts::FastStaTiming::updateBranch(context, context.source_node_id, states));
    const auto& cell = context.liberty_cell_by_master.at("BUF_X1");
    const auto& input = context.nodes.at(1U);
    const auto& output = context.nodes.at(2U);
    const auto& net = context.nets.at(1U);
    for (std::size_t analysis = 0U; analysis < 2U; ++analysis) {
      for (std::size_t transition = 0U; transition < 2U; ++transition) {
        const auto& input_timing = analysis == 0U ? input.early_timing.at(transition) : input.late_timing.at(transition);
        const auto& output_timing = analysis == 0U ? output.early_timing.at(transition) : output.late_timing.at(transition);
        const auto cold_driver
            = icts::FastStaDmpCeff::calcDriverTiming(cell, cell.timing_arc, net.parasitic.driver_pi_by_timing.at(analysis).at(transition),
                                                     transition == 0U ? icts::FastStaTransition::kRise : icts::FastStaTransition::kFall, input_timing.slew_ns);
        ASSERT_TRUE(cold_driver.valid);
        const auto& actual_driver = net.driver_timing_by_state.at(analysis).at(transition);
        EXPECT_DOUBLE_EQ(actual_driver.gate_delay_ns, cold_driver.gate_delay_ns);
        EXPECT_DOUBLE_EQ(actual_driver.driver_slew_ns, cold_driver.driver_slew_ns);
        EXPECT_DOUBLE_EQ(output_timing.arrival_ns, input_timing.arrival_ns + cold_driver.gate_delay_ns);
        EXPECT_DOUBLE_EQ(output_timing.slew_ns, cold_driver.driver_slew_ns);
      }
    }
    EXPECT_NE(output.early_timing.front().arrival_ns, output.late_timing.front().arrival_ns);
  }
}

TEST(FastSTATest, DmpTablePreparationRevalidatesEverySolve)
{
  auto cell = MakeReferenceBufferCell();
  const icts::FastStaPiModel pi{.near_cap_pf = 0.1, .resistance_ohm = 100.0, .far_cap_pf = 0.2};
  const auto reference = icts::FastStaDmpCeff::calcDriverTiming(cell, pi, icts::FastStaTransition::kRise, 0.2);
  ASSERT_TRUE(reference.valid);
  auto& delay = cell.timing_arc.delay_tables.front();
  const auto original_values = delay.values;
  delay.values.front() = std::numeric_limits<double>::quiet_NaN();
  EXPECT_FALSE(delay.lookup(0.2, 0.3).has_value());
  EXPECT_FALSE(icts::FastStaDmpCeff::calcDriverTiming(cell, pi, icts::FastStaTransition::kRise, 0.2).valid);
  delay.values = original_values;
  const auto restored = icts::FastStaDmpCeff::calcDriverTiming(cell, pi, icts::FastStaTransition::kRise, 0.2);
  ASSERT_TRUE(restored.valid);
  EXPECT_DOUBLE_EQ(restored.ceff_pf, reference.ceff_pf);
  EXPECT_DOUBLE_EQ(restored.gate_delay_ns, reference.gate_delay_ns);
  EXPECT_DOUBLE_EQ(restored.driver_slew_ns, reference.driver_slew_ns);
  EXPECT_DOUBLE_EQ(restored.driver_waveform_delay_ns, reference.driver_waveform_delay_ns);

  const auto original_axis = delay.axes.front().values;
  std::ranges::reverse(delay.axes.front().values);
  EXPECT_FALSE(delay.lookup(0.2, 0.3).has_value());
  EXPECT_FALSE(icts::FastStaDmpCeff::calcDriverTiming(cell, pi, icts::FastStaTransition::kRise, 0.2).valid);
  delay.axes.front().values = original_axis;
  cell.timing_arc.slew_tables.back().values.clear();
  EXPECT_FALSE(icts::FastStaDmpCeff::calcDriverTiming(cell, pi, icts::FastStaTransition::kFall, 0.2).valid);
  EXPECT_TRUE(icts::FastStaDmpCeff::calcDriverTiming(cell, pi, icts::FastStaTransition::kRise, 0.2).valid);
}

TEST(FastSTATest, SourceBoundaryNetUsesNormalNetLoadAndCapFields)
{
  auto context = MakeTinyContext();
  context.nets.at(0U).max_cap_pf = 0.15;

  icts::FastStaParasitics::updateNetLoads(context);

  const auto& source_boundary_net = context.nets.at(0U);
  EXPECT_EQ(source_boundary_net.driver_node_id, context.source_node_id);
  EXPECT_NEAR(source_boundary_net.load_cap_pf, 0.20, 1e-12);
  EXPECT_NEAR(source_boundary_net.max_cap_pf, 0.15, 1e-12);
  EXPECT_GT(source_boundary_net.load_cap_pf, source_boundary_net.max_cap_pf);
}

TEST(FastSTATest, PiElmoreReductionPropagatesDownstreamCapAndElmore)
{
  auto context = MakeTinyContext();
  icts::FastStaParasitics::updateNetLoads(context);

  ASSERT_TRUE(icts::FastStaParasitics::reduceToPiElmore(context, 0U));

  const auto& parasitic = context.nets.at(0U).parasitic;
  EXPECT_TRUE(parasitic.valid);
  EXPECT_NEAR(parasitic.total_cap_pf, 0.20, 1e-12);
  EXPECT_NEAR(parasitic.rc_nodes.at(1U).downstream_cap_pf, 0.20, 1e-12);
  EXPECT_NEAR(parasitic.rc_nodes.at(1U).elmore_delay_ns, 0.02, 1e-12);
}

TEST(FastSTATest, TimingPowerAndMasterChangeUpdateContext)
{
  auto context = MakeTinyContext();

  ASSERT_TRUE(icts::FastStaTiming::update(context));
  EXPECT_TRUE(context.skew.valid);
  EXPECT_EQ(context.skew.max_sink_name, "sink/CLK");
  EXPECT_GT(context.nodes.at(3U).timing.arrival_ns, 0.0);
  EXPECT_TRUE(icts::FastStaPower::update(context));
  EXPECT_NEAR(context.power.area_um2, 1.5, 1e-12);
  EXPECT_GT(context.power.switching_power_w, 0.0);
  EXPECT_GT(context.power.internal_power_w, 0.0);
  EXPECT_NEAR(context.power.leakage_power_w, 0.01, 1e-12);

  ASSERT_TRUE(icts::FastStaIncremental::changeBufferMaster(context, 1U, "BUF_X2"));
  EXPECT_EQ(context.nodes.at(1U).cell_master, "BUF_X2");
  EXPECT_EQ(context.nodes.at(2U).cell_master, "BUF_X2");
  EXPECT_NEAR(context.nodes.at(1U).input_cap_pf, 0.40, 1e-12);
  ASSERT_TRUE(icts::FastStaTiming::update(context));
  ASSERT_TRUE(icts::FastStaPower::update(context));
  EXPECT_NEAR(context.power.area_um2, 2.5, 1e-12);
  EXPECT_NEAR(context.power.leakage_power_w, 0.02, 1e-12);
}

TEST(FastSTATest, PowerUpdateRejectsMissingInternalPowerTable)
{
  auto context = MakeTinyContext();
  ASSERT_TRUE(icts::FastStaTiming::update(context));
  context.liberty_cell_by_master.at("BUF_X1").timing_arc.internal_power_tables.clear();

  EXPECT_FALSE(icts::FastStaPower::update(context));
  EXPECT_FALSE(context.power_valid);
  EXPECT_DOUBLE_EQ(context.power.total_power_w, 0.0);
}

TEST(FastSTATest, PowerUpdateRejectsMalformedInternalPowerTable)
{
  auto context = MakeTinyContext();
  ASSERT_TRUE(icts::FastStaTiming::update(context));
  auto& power_table = context.liberty_cell_by_master.at("BUF_X1").timing_arc.internal_power_tables.front();
  power_table.values.pop_back();

  EXPECT_FALSE(icts::FastStaPower::update(context));
  EXPECT_FALSE(context.power_valid);
  EXPECT_DOUBLE_EQ(context.power.total_power_w, 0.0);
}

TEST(FastSTATest, PowerUpdateRejectsNegativeInternalPowerEnergy)
{
  auto context = MakeTinyContext();
  ASSERT_TRUE(icts::FastStaTiming::update(context));
  auto& power_table = context.liberty_cell_by_master.at("BUF_X1").timing_arc.internal_power_tables.front();
  std::ranges::fill(power_table.values, -1.0);

  EXPECT_FALSE(icts::FastStaPower::update(context));
  EXPECT_FALSE(context.power_valid);
  EXPECT_DOUBLE_EQ(context.power.total_power_w, 0.0);
}

TEST(FastSTATest, PowerUpdateRejectsUnavailableLeakagePower)
{
  auto context = MakeTinyContext();
  ASSERT_TRUE(icts::FastStaTiming::update(context));
  context.liberty_cell_by_master.at("BUF_X1").leakage_power_w = std::nullopt;

  EXPECT_FALSE(icts::FastStaPower::update(context));
  EXPECT_FALSE(context.power_valid);
  EXPECT_DOUBLE_EQ(context.power.total_power_w, 0.0);
}

TEST(FastSTATest, PowerUpdateAcceptsExplicitZeroPowerData)
{
  auto context = MakeTinyContext();
  ASSERT_TRUE(icts::FastStaTiming::update(context));
  auto& cell = context.liberty_cell_by_master.at("BUF_X1");
  cell.leakage_power_w = 0.0;
  for (auto& power_table : cell.timing_arc.internal_power_tables) {
    std::ranges::fill(power_table.values, 0.0);
  }

  EXPECT_TRUE(icts::FastStaPower::update(context));
  EXPECT_TRUE(context.power_valid);
  EXPECT_DOUBLE_EQ(context.power.internal_power_w, 0.0);
  EXPECT_DOUBLE_EQ(context.power.leakage_power_w, 0.0);
}

TEST(FastSTATest, InvalidCharacterizationContextAccessIsSafe)
{
  icts::FastSTA fast_sta;

  EXPECT_FALSE(fast_sta.eraseCharContext(0U));
  EXPECT_FALSE(fast_sta.setCharLoad(0U, 0.25));
  EXPECT_FALSE(fast_sta.runCharSample(0U, 0.10).valid);
  fast_sta.reset();
  EXPECT_FALSE(fast_sta.eraseCharContext(0U));
}

}  // namespace
}  // namespace icts_test
