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
 * @file ClockDAGTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-06
 * @brief Unit tests for Design-owned CTS clock DAG path-depth semantics
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "IdbDesign.h"
#include "data_manager/DataManager.hh"
#include "data_manager/design/Clock.hh"
#include "data_manager/design/ClockDAG.hh"
#include "data_manager/design/Design.hh"
#include "data_manager/design/Inst.hh"
#include "data_manager/design/Net.hh"
#include "data_manager/design/Pin.hh"
#include "data_manager/io/Wrapper.hh"
#include "data_manager/spatial/Point.hh"
#include "module/evaluation/qor/QOREvaluation.hh"
#include "module/synthesis/realization/ClockTreeRealization.hh"

namespace icts_test {
namespace {

class ScopedDesignReset
{
 public:
  ScopedDesignReset() : _idb_design(std::make_unique<idb::IdbDesign>())
  {
    CTSDM.getWrapper().reset();
    CTSDM.getDesign().reset();
    _idb_design->get_units()->set_microns_dbu(1000);
    CTSDM.getWrapper().set_idb_design(_idb_design.get());
  }
  ~ScopedDesignReset()
  {
    CTSDM.getWrapper().reset();
    CTSDM.getDesign().reset();
  }

 private:
  std::unique_ptr<idb::IdbDesign> _idb_design;
};

struct BufferPins
{
  icts::Inst* inst = nullptr;
  icts::Pin* input = nullptr;
  icts::Pin* output = nullptr;
};

struct ClockPins
{
  icts::Clock* clock = nullptr;
  icts::Pin* source = nullptr;
  icts::Net* source_net = nullptr;
};

auto makeInst(const std::string& name, icts::InstType type, const icts::Point<int>& location) -> icts::Inst*
{
  auto* inst = CTSDM.getDesign().makeInst(name);
  inst->set_name(name);
  inst->set_cell_master(type == icts::InstType::kBuffer ? "BUF_X1" : "REG_X1");
  inst->set_type(type);
  inst->set_location(location);
  return inst;
}

auto makePin(const std::string& name, icts::PinType type, icts::Inst* inst, const icts::Point<int>& location) -> icts::Pin*
{
  auto* pin = CTSDM.getDesign().makePin(name);
  pin->set_name(name);
  pin->set_type(type);
  pin->set_inst(inst);
  pin->set_location(location);
  pin->set_io(false);
  if (inst != nullptr) {
    inst->add_pin(pin);
  }
  (void) CTSDM.getDesign().indexPin(pin);
  return pin;
}

auto connectNet(const std::string& name, icts::Pin* driver, const std::vector<icts::Pin*>& loads) -> icts::Net*
{
  auto* net = CTSDM.getDesign().makeNet(name);
  net->set_name(name);
  net->set_driver(driver);
  net->set_loads({});
  if (driver != nullptr) {
    driver->set_net(net);
  }
  for (auto* load : loads) {
    net->add_load(load);
    if (load != nullptr) {
      load->set_net(net);
    }
  }
  return net;
}

auto makeClock(const std::string& clock_name, const std::string& net_name) -> ClockPins
{
  auto* clock = CTSDM.getDesign().makeClock(clock_name, net_name);
  auto* source = makePin(clock_name + "_src", icts::PinType::kOut, nullptr, icts::Point<int>(0, 0));
  auto* source_net = connectNet(net_name, source, {});
  clock->set_clock_source(source);
  clock->set_clock_source_net(source_net);
  return ClockPins{.clock = clock, .source = source, .source_net = source_net};
}

auto makeBuffer(const std::string& name, int x) -> BufferPins
{
  auto* inst = makeInst(name, icts::InstType::kBuffer, icts::Point<int>(x, 0));
  inst->set_cell_master("");
  auto* output = makePin("Y", icts::PinType::kOut, inst, inst->get_location());
  auto* input = makePin("A", icts::PinType::kIn, inst, inst->get_location());
  return BufferPins{.inst = inst, .input = input, .output = output};
}

auto addBufferArc(icts::Clock* clock, const BufferPins& buffer, icts::ClockPropagationOrigin origin = icts::ClockPropagationOrigin::kSynthesized) -> void
{
  ASSERT_NE(clock, nullptr);
  ASSERT_TRUE(clock
                  ->addPropagationArc({.inst = buffer.inst,
                                       .input_pin = buffer.input,
                                       .output_pin = buffer.output,
                                       .kind = icts::ClockPropagationKind::kBuffer,
                                       .origin = origin,
                                       .path_buffer_weight = 1})
                  .ok());
}

auto makeSink(const std::string& name, icts::InstType type, int x) -> icts::Pin*
{
  auto* inst = makeInst(name, type, icts::Point<int>(x, 0));
  return makePin("CLK", icts::PinType::kClock, inst, inst->get_location());
}

TEST(DesignIndexTest, RemoveClockMembershipObjectsErasesRecordedInstAndNetNames)
{
  const ScopedDesignReset scoped_design_reset;

  auto clock_pins = makeClock("clk", "clk_net");
  auto buffer = makeBuffer("buffer_to_remove", 10);
  auto* sink = makeSink("sink", icts::InstType::kFlipFlop, 20);
  auto* removed_net = connectNet("net_to_remove", buffer.output, {sink});
  addBufferArc(clock_pins.clock, buffer);
  clock_pins.clock->add_net(removed_net);
  clock_pins.clock->add_load(sink);

  buffer.inst->set_name("renamed_buffer_to_remove");
  removed_net->set_name("renamed_net_to_remove");

  auto& design = CTSDM.getDesign();
  design.removeClockMembershipObjects(*clock_pins.clock);

  EXPECT_EQ(design.findInst("buffer_to_remove"), nullptr);
  EXPECT_EQ(design.findInst("renamed_buffer_to_remove"), nullptr);
  EXPECT_EQ(design.findNet("net_to_remove"), nullptr);
  EXPECT_EQ(design.findNet("renamed_net_to_remove"), nullptr);
}

TEST(DesignIndexTest, RemovingOneClocksSynthesizedNetClearsEveryCrossClockBorrowedReference)
{
  const ScopedDesignReset scoped_design_reset;

  auto clock_a = makeClock("clock_a", "clock_a_source_net");
  auto clock_b = makeClock("clock_b", "clock_b_original_source_net");
  auto buffer = makeBuffer("clock_a_synthesized_buffer", 10);
  auto* sink_a = makeSink("clock_a_sink", icts::InstType::kFlipFlop, 20);
  auto* sink_b = makeSink("clock_b_sink", icts::InstType::kFlipFlop, 30);

  connectNet("clock_a_source_net", clock_a.source, {buffer.input});
  auto* shared_net = connectNet("clock_a_synthesized_shared_net", buffer.output, {sink_a, sink_b});
  clock_a.clock->set_clock_source_net(clock_a.source_net);
  addBufferArc(clock_a.clock, buffer);
  clock_a.clock->add_net(shared_net);
  clock_a.clock->add_load(sink_a);

  clock_b.clock->set_clock_source(buffer.output);
  clock_b.clock->set_clock_source_net(shared_net);
  clock_b.clock->add_net(shared_net);
  clock_b.clock->add_load(sink_b);
  ASSERT_TRUE(CTSDM.getDesign().rebuildClockDAG());

  CTSDM.getDesign().removeClockSynthesizedObjects(*clock_a.clock);

  EXPECT_EQ(CTSDM.getDesign().findInst("clock_a_synthesized_buffer"), nullptr);
  EXPECT_EQ(CTSDM.getDesign().findNet("clock_a_synthesized_shared_net"), nullptr);
  EXPECT_TRUE(clock_a.clock->get_propagation_arcs().empty());
  EXPECT_TRUE(clock_a.clock->get_insts().empty());
  EXPECT_TRUE(clock_a.clock->get_nets().empty());
  EXPECT_TRUE(clock_a.clock->get_loads().empty());
  EXPECT_EQ(clock_b.clock->get_clock_source(), nullptr);
  EXPECT_EQ(clock_b.clock->get_clock_source_net(), nullptr);
  EXPECT_TRUE(clock_b.clock->get_nets().empty());
  EXPECT_TRUE(clock_b.clock->get_loads().empty());
  EXPECT_EQ(sink_a->get_net(), nullptr);
  EXPECT_EQ(sink_b->get_net(), nullptr);
}

TEST(ClockDAGTest, NestedBranchedTracedTopologyUsesOnlyDeterministicTopLevelAndUncoveredFrontier)
{
  const ScopedDesignReset scoped_design_reset;

  auto clock_pins = makeClock("clk", "clk_net");
  auto top_buffer = makeBuffer("top_buffer", 10);
  auto nested_buffer_a = makeBuffer("nested_buffer_a", 20);
  auto nested_buffer_b = makeBuffer("nested_buffer_b", 30);
  auto* top_sink = makeSink("top_sink", icts::InstType::kFlipFlop, 40);
  auto* nested_sink_a = makeSink("nested_sink_a", icts::InstType::kFlipFlop, 50);
  auto* nested_sink_b = makeSink("nested_sink_b", icts::InstType::kFlipFlop, 60);
  auto* uncovered_sink = makeSink("aaa_uncovered_sink", icts::InstType::kFlipFlop, 70);

  connectNet("clk_net", clock_pins.source, {top_buffer.input, uncovered_sink});
  auto* branched_net = connectNet("traced_branched_net", top_buffer.output, {nested_buffer_b.input, top_sink, nested_buffer_a.input});
  auto* leaf_net_a = connectNet("traced_leaf_a", nested_buffer_a.output, {nested_sink_a});
  auto* leaf_net_b = connectNet("traced_leaf_b", nested_buffer_b.output, {nested_sink_b});
  clock_pins.clock->set_clock_source_net(clock_pins.source_net);
  addBufferArc(clock_pins.clock, nested_buffer_b, icts::ClockPropagationOrigin::kTracedInput);
  addBufferArc(clock_pins.clock, top_buffer, icts::ClockPropagationOrigin::kTracedInput);
  addBufferArc(clock_pins.clock, nested_buffer_a, icts::ClockPropagationOrigin::kTracedInput);
  clock_pins.clock->add_net(branched_net);
  clock_pins.clock->add_net(leaf_net_a);
  clock_pins.clock->add_net(leaf_net_b);
  clock_pins.clock->add_load(nested_sink_b);
  clock_pins.clock->add_load(top_sink);
  clock_pins.clock->add_load(uncovered_sink);
  clock_pins.clock->add_load(nested_sink_a);

  const auto frontier = icts::ClockTreeRealization::deriveSynthesisFrontier(*clock_pins.clock);
  ASSERT_TRUE(frontier.hasTracedTopology());
  ASSERT_EQ(frontier.top_level_traced_inputs, std::vector<icts::Pin*>({top_buffer.input}));
  ASSERT_EQ(frontier.uncovered_terminal_loads, std::vector<icts::Pin*>({uncovered_sink}));
  ASSERT_EQ(frontier.pins.size(), 2U);
  EXPECT_EQ(icts::Design::getPinFullName(frontier.pins.at(0)), "aaa_uncovered_sink/CLK");
  EXPECT_EQ(icts::Design::getPinFullName(frontier.pins.at(1)), "top_buffer/A");
  EXPECT_EQ(std::ranges::find(clock_pins.clock->get_loads(), top_buffer.input), clock_pins.clock->get_loads().end());
  EXPECT_EQ(std::ranges::find(frontier.pins, nested_buffer_a.input), frontier.pins.end());
  EXPECT_EQ(std::ranges::find(frontier.pins, nested_buffer_b.input), frontier.pins.end());

  const auto frontier_partition = icts::ClockTreeRealization::partitionClockSinks(frontier.pins);
  EXPECT_TRUE(frontier_partition.macro_sinks.empty());
  EXPECT_EQ(frontier_partition.regular_sinks, frontier.pins);
  const auto restored_frontier = icts::ClockTreeRealization::restoreClockSourceNetToSynthesisFrontier(*clock_pins.clock);
  EXPECT_EQ(restored_frontier.pins, frontier.pins);
  EXPECT_EQ(clock_pins.source_net->get_loads(), frontier.pins);
  EXPECT_EQ(branched_net->get_loads().size(), 3U);
  EXPECT_EQ(leaf_net_a->get_loads(), std::vector<icts::Pin*>({nested_sink_a}));
  EXPECT_EQ(leaf_net_b->get_loads(), std::vector<icts::Pin*>({nested_sink_b}));

  ASSERT_TRUE(CTSDM.getDesign().rebuildClockDAG());
  const auto stats = CTSDM.getDesign().get_clock_dag().pathBufferStats(clock_pins.clock);
  EXPECT_TRUE(stats.available);
  EXPECT_EQ(stats.min_buffer_count, 0);
  EXPECT_EQ(stats.max_buffer_count, 2);
  EXPECT_EQ(stats.ff_sink_terminal_count, 4U);
}

TEST(DesignIndexTest, ClockIdentityIndexTracksMakeClearResetAndClone)
{
  icts::Design design;
  auto* first = design.makeClock("clk", "clk_net_a");
  auto* second = design.makeClock("clk", "clk_net_b");
  auto* third = design.makeClock("generated_clk", "clk_net_a");

  ASSERT_NE(first, nullptr);
  ASSERT_NE(second, nullptr);
  ASSERT_NE(third, nullptr);
  EXPECT_NE(first, second);
  EXPECT_NE(first, third);
  EXPECT_EQ(design.makeClock("clk", "clk_net_a"), first);
  EXPECT_EQ(design.findClock("clk", "clk_net_a"), first);
  EXPECT_EQ(design.findClock("clk", "clk_net_b"), second);
  EXPECT_EQ(design.findClock("generated_clk", "clk_net_a"), third);

  auto cloned = design.clone();
  ASSERT_NE(cloned, nullptr);
  ASSERT_NE(cloned->findClock("clk", "clk_net_a"), nullptr);
  EXPECT_NE(cloned->findClock("clk", "clk_net_a"), first);
  EXPECT_EQ(cloned->makeClock("clk", "clk_net_a"), cloned->findClock("clk", "clk_net_a"));

  design.clearClocks();
  EXPECT_EQ(design.findClock("clk", "clk_net_a"), nullptr);
  EXPECT_EQ(design.findClock("clk", "clk_net_b"), nullptr);
  EXPECT_EQ(design.findClock("generated_clk", "clk_net_a"), nullptr);
  EXPECT_TRUE(design.get_clocks().empty());

  ASSERT_NE(design.makeClock("clk", "clk_net_a"), nullptr);
  design.reset();
  EXPECT_EQ(design.findClock("clk", "clk_net_a"), nullptr);
  EXPECT_TRUE(design.get_clocks().empty());
}

TEST(ClockDAGTest, BranchPathsReportSourceToFlipFlopBufferDepths)
{
  const ScopedDesignReset scoped_design_reset;

  auto clock_pins = makeClock("clk", "clk_net");
  auto buf_one = makeBuffer("buf_one", 10);
  auto buf_two_a = makeBuffer("buf_two_a", 20);
  auto buf_two_b = makeBuffer("buf_two_b", 30);
  auto* ff_one = makeSink("ff_one", icts::InstType::kFlipFlop, 40);
  auto* ff_two = makeSink("ff_two", icts::InstType::kFlipFlop, 50);

  connectNet("clk_net", clock_pins.source, {buf_one.input, buf_two_a.input});
  clock_pins.clock->set_clock_source_net(clock_pins.source_net);
  auto* one_deep_net = connectNet("one_deep_net", buf_one.output, {ff_one});
  auto* two_deep_mid_net = connectNet("two_deep_mid_net", buf_two_a.output, {buf_two_b.input});
  auto* two_deep_leaf_net = connectNet("two_deep_leaf_net", buf_two_b.output, {ff_two});

  addBufferArc(clock_pins.clock, buf_one);
  addBufferArc(clock_pins.clock, buf_two_a);
  addBufferArc(clock_pins.clock, buf_two_b);
  clock_pins.clock->add_net(one_deep_net);
  clock_pins.clock->add_net(two_deep_mid_net);
  clock_pins.clock->add_net(two_deep_leaf_net);
  clock_pins.clock->add_load(ff_one);
  clock_pins.clock->add_load(ff_two);

  ASSERT_TRUE(CTSDM.getDesign().rebuildClockDAG());
  const auto stats = CTSDM.getDesign().get_clock_dag().pathBufferStats(clock_pins.clock);
  EXPECT_TRUE(stats.available);
  EXPECT_EQ(stats.status, "available");
  EXPECT_EQ(stats.min_buffer_count, 1);
  EXPECT_EQ(stats.max_buffer_count, 2);
  EXPECT_EQ(stats.ff_sink_terminal_count, 2U);
}

TEST(ClockDAGTest, DirectSourceToFlipFlopPathReportsZeroBuffers)
{
  const ScopedDesignReset scoped_design_reset;

  auto clock_pins = makeClock("clk", "clk_net");
  auto* ff = makeSink("ff_zero", icts::InstType::kFlipFlop, 10);
  connectNet("clk_net", clock_pins.source, {ff});
  clock_pins.clock->set_clock_source_net(clock_pins.source_net);
  clock_pins.clock->add_load(ff);

  ASSERT_TRUE(CTSDM.getDesign().rebuildClockDAG());
  const auto stats = CTSDM.getDesign().get_clock_dag().pathBufferStats(clock_pins.clock);
  EXPECT_TRUE(stats.available);
  EXPECT_EQ(stats.min_buffer_count, 0);
  EXPECT_EQ(stats.max_buffer_count, 0);
}

TEST(ClockDAGTest, LargeStarUsesLinearWorkAccountingAndDeterministicReadyOrder)
{
  const ScopedDesignReset scoped_design_reset;
  constexpr std::size_t sink_count = 13850U;

  auto clock_pins = makeClock("large_star", "large_star_net");
  std::vector<icts::Pin*> sinks;
  sinks.reserve(sink_count);
  for (std::size_t index = 0U; index < sink_count; ++index) {
    sinks.push_back(makeSink("star_ff_" + std::to_string(index), icts::InstType::kFlipFlop, static_cast<int>(index + 1U)));
    clock_pins.clock->add_load(sinks.back());
  }
  connectNet("large_star_net", clock_pins.source, sinks);
  clock_pins.clock->set_clock_source_net(clock_pins.source_net);

  ASSERT_TRUE(CTSDM.getDesign().rebuildClockDAG());
  const auto* first_graph = CTSDM.getDesign().get_clock_dag().graphForClock(clock_pins.clock);
  ASSERT_NE(first_graph, nullptr);
  EXPECT_EQ(first_graph->build_work.ready_push_count, sink_count + 1U);
  EXPECT_EQ(first_graph->build_work.ready_pop_count, sink_count + 1U);
  EXPECT_EQ(first_graph->build_work.arc_relaxation_count, sink_count);
  const auto first_topological_pins = CTSDM.getDesign().get_clock_dag().topologicalPins(clock_pins.clock);
  ASSERT_EQ(first_topological_pins.size(), sink_count + 1U);

  auto reversed_sinks = sinks;
  std::ranges::reverse(reversed_sinks);
  clock_pins.source_net->set_loads(reversed_sinks);
  ASSERT_TRUE(CTSDM.getDesign().rebuildClockDAG());
  const auto* second_graph = CTSDM.getDesign().get_clock_dag().graphForClock(clock_pins.clock);
  ASSERT_NE(second_graph, nullptr);
  EXPECT_EQ(second_graph->build_work.ready_push_count, sink_count + 1U);
  EXPECT_EQ(second_graph->build_work.ready_pop_count, sink_count + 1U);
  EXPECT_EQ(second_graph->build_work.arc_relaxation_count, sink_count);
  const auto second_topological_pins = CTSDM.getDesign().get_clock_dag().topologicalPins(clock_pins.clock);
  ASSERT_EQ(second_topological_pins.size(), first_topological_pins.size());
  for (std::size_t index = 0U; index < first_topological_pins.size(); ++index) {
    EXPECT_EQ(icts::Design::getPinFullName(second_topological_pins.at(index)), icts::Design::getPinFullName(first_topological_pins.at(index)));
  }

  const auto stats = CTSDM.getDesign().get_clock_dag().pathBufferStats(clock_pins.clock);
  EXPECT_TRUE(stats.available);
  EXPECT_EQ(stats.ff_sink_terminal_count, sink_count);
  EXPECT_EQ(stats.terminal_probe_count, sink_count);
  EXPECT_EQ(stats.min_buffer_count, 0);
  EXPECT_EQ(stats.max_buffer_count, 0);
}

TEST(ClockDAGTest, BoundaryLoadDoesNotRequireBufferInputArc)
{
  const ScopedDesignReset scoped_design_reset;

  auto clock_pins = makeClock("clk", "clk_net");
  auto* boundary_load = makeSink("comb_boundary", icts::InstType::kBoundaryLoad, 10);
  auto* latch_sink = makeSink("latch_sink", icts::InstType::kLatch, 20);
  connectNet("clk_net", clock_pins.source, {boundary_load, latch_sink});
  clock_pins.clock->set_clock_source_net(clock_pins.source_net);
  clock_pins.clock->add_load(boundary_load);
  clock_pins.clock->add_load(latch_sink);

  ASSERT_TRUE(CTSDM.getDesign().rebuildClockDAG());
  const auto stats = CTSDM.getDesign().get_clock_dag().pathBufferStats(clock_pins.clock);
  EXPECT_TRUE(stats.available);
  EXPECT_EQ(stats.status, "available");
  EXPECT_EQ(stats.min_buffer_count, 0);
  EXPECT_EQ(stats.max_buffer_count, 0);
  EXPECT_EQ(stats.ff_sink_terminal_count, 1U);
}

TEST(ClockDAGTest, PhysicalBufferWithoutOwnedArcIsTerminalBoundary)
{
  const ScopedDesignReset scoped_design_reset;

  auto clock_pins = makeClock("clk", "clk_net");
  auto* buffer_inst = makeInst("malformed_buffer", icts::InstType::kBuffer, icts::Point<int>(10, 0));
  auto* buffer_input = makePin("A", icts::PinType::kIn, buffer_inst, buffer_inst->get_location());
  connectNet("clk_net", clock_pins.source, {buffer_input});
  clock_pins.clock->set_clock_source_net(clock_pins.source_net);
  clock_pins.clock->add_load(buffer_input);

  ASSERT_TRUE(CTSDM.getDesign().rebuildClockDAG());
  EXPECT_EQ(clock_pins.clock->get_propagation_arcs().size(), 0U);
  const auto reachable = CTSDM.getDesign().get_clock_dag().reachablePins(clock_pins.clock);
  EXPECT_NE(std::ranges::find(reachable, buffer_input), reachable.end());
}

TEST(ClockDAGTest, IncompletePropagationArcIsRejectedAtOwningBoundary)
{
  const ScopedDesignReset scoped_design_reset;
  auto clock_pins = makeClock("clk", "clk_net");
  auto buffer = makeBuffer("malformed_buffer", 10);
  const auto status = clock_pins.clock->addPropagationArc({.inst = buffer.inst, .input_pin = buffer.input, .output_pin = nullptr});
  EXPECT_EQ(status.code, icts::ClockPropagationMutationCode::kIncomplete);
  EXPECT_TRUE(clock_pins.clock->get_propagation_arcs().empty());
}

TEST(ClockDAGTest, AmbiguousInOutPropagationDirectionIsRejectedAtOwningBoundary)
{
  const ScopedDesignReset scoped_design_reset;
  auto clock_pins = makeClock("clk", "clk_net");
  auto buffer = makeBuffer("ambiguous_buffer", 10);
  buffer.input->set_type(icts::PinType::kInOut);

  const auto status = clock_pins.clock->addPropagationArc({.inst = buffer.inst,
                                                           .input_pin = buffer.input,
                                                           .output_pin = buffer.output,
                                                           .kind = icts::ClockPropagationKind::kBuffer,
                                                           .origin = icts::ClockPropagationOrigin::kSynthesized,
                                                           .path_buffer_weight = 1});

  EXPECT_EQ(status.code, icts::ClockPropagationMutationCode::kInvalidPinDirection);
  EXPECT_EQ(status.message, "invalid_clock_propagation_pin_direction");
  EXPECT_TRUE(clock_pins.clock->get_propagation_arcs().empty());
}

TEST(ClockDAGTest, CloneRemapsAndPinRemovalClearsOwnedPropagationArc)
{
  const ScopedDesignReset scoped_design_reset;
  auto clock_pins = makeClock("clk", "clk_net");
  auto buffer = makeBuffer("owned_buffer", 10);
  auto* sink = makeSink("sink", icts::InstType::kFlipFlop, 20);
  connectNet("clk_net", clock_pins.source, {buffer.input});
  auto* leaf_net = connectNet("leaf_net", buffer.output, {sink});
  addBufferArc(clock_pins.clock, buffer);
  clock_pins.clock->add_net(leaf_net);
  clock_pins.clock->add_load(sink);

  auto cloned = CTSDM.getDesign().clone();
  auto* cloned_clock = cloned->findClock("clk", "clk_net");
  ASSERT_NE(cloned_clock, nullptr);
  ASSERT_EQ(cloned_clock->get_propagation_arcs().size(), 1U);
  const auto& cloned_arc = cloned_clock->get_propagation_arcs().front();
  EXPECT_NE(cloned_arc.inst, buffer.inst);
  EXPECT_NE(cloned_arc.input_pin, buffer.input);
  EXPECT_NE(cloned_arc.output_pin, buffer.output);
  EXPECT_EQ(cloned_arc.inst->get_name(), buffer.inst->get_name());
  EXPECT_EQ(cloned_arc.origin, icts::ClockPropagationOrigin::kSynthesized);

  clock_pins.clock->removePropagationArcsFor(buffer.input);
  EXPECT_TRUE(clock_pins.clock->get_propagation_arcs().empty());
  EXPECT_TRUE(clock_pins.clock->get_insts().empty());
}

TEST(ClockDAGTest, DesignRemovalAndTopologyClearInvalidateEveryBorrowedClockReference)
{
  const ScopedDesignReset scoped_design_reset;
  auto& design = CTSDM.getDesign();
  auto clock_pins = makeClock("clk", "clk_net");
  auto buffer = makeBuffer("owned_buffer", 10);
  auto* sink = makeSink("sink", icts::InstType::kFlipFlop, 20);
  connectNet("clk_net", clock_pins.source, {buffer.input});
  auto* leaf_net = connectNet("leaf_net", buffer.output, {sink});
  addBufferArc(clock_pins.clock, buffer);
  clock_pins.clock->add_net(leaf_net);
  clock_pins.clock->add_load(sink);
  ASSERT_TRUE(design.rebuildClockDAG());

  design.removeClockMembershipObjects(*clock_pins.clock);
  EXPECT_EQ(design.findInst("owned_buffer"), nullptr);
  EXPECT_EQ(design.findNet("leaf_net"), nullptr);
  EXPECT_TRUE(clock_pins.clock->get_propagation_arcs().empty());
  EXPECT_TRUE(clock_pins.clock->get_insts().empty());
  EXPECT_TRUE(clock_pins.clock->get_nets().empty());
  EXPECT_TRUE(clock_pins.clock->get_loads().empty());
  EXPECT_TRUE(design.rebuildClockDAG());

  design.clearTopologyObjects();
  EXPECT_EQ(clock_pins.clock->get_clock_source(), nullptr);
  EXPECT_EQ(clock_pins.clock->get_clock_source_net(), nullptr);
  EXPECT_TRUE(clock_pins.clock->get_propagation_arcs().empty());
  EXPECT_TRUE(clock_pins.clock->get_insts().empty());
  EXPECT_TRUE(clock_pins.clock->get_nets().empty());
  EXPECT_TRUE(clock_pins.clock->get_loads().empty());
}

TEST(ClockDAGTest, TypedIssueCarriesDeterministicPhysicalKindEvidence)
{
  const ScopedDesignReset scoped_design_reset;
  auto clock_pins = makeClock("clk", "clk_net");
  auto buffer = makeBuffer("kind_mismatch", 10);
  auto* sink = makeSink("sink", icts::InstType::kFlipFlop, 20);
  connectNet("clk_net", clock_pins.source, {buffer.input});
  auto* leaf_net = connectNet("leaf_net", buffer.output, {sink});
  addBufferArc(clock_pins.clock, buffer);
  clock_pins.clock->add_net(leaf_net);
  clock_pins.clock->add_load(sink);
  buffer.inst->set_type(icts::InstType::kInverter);

  EXPECT_FALSE(CTSDM.getDesign().rebuildClockDAG());
  const auto& issues = CTSDM.getDesign().get_clock_dag().get_issues();
  ASSERT_FALSE(issues.empty());
  EXPECT_EQ(issues.front().code, icts::ClockGraphIssueCode::kPhysicalKindMismatch);
  EXPECT_EQ(issues.front().clock_name, "clk");
  EXPECT_EQ(issues.front().inst_name, "kind_mismatch");
  EXPECT_EQ(issues.front().input_pin_name, "kind_mismatch/A");
  EXPECT_EQ(issues.front().output_pin_name, "kind_mismatch/Y");
  EXPECT_EQ(issues.front().propagation_kind, "buffer");
  EXPECT_EQ(issues.front().propagation_origin, "synthesized");
  EXPECT_EQ(issues.front().expected, "buffer");
  EXPECT_EQ(issues.front().observed, "inverter");
  EXPECT_EQ(issues.front().invariant, "physical_kind_mismatch");

  const auto status = icts::DataManager::makeClockGraphFailureStatus(icts::DataManagerStatusCode::kCommitError, "candidate graph rejected",
                                                                     CTSDM.getDesign().get_clock_dag());
  EXPECT_EQ(status.code, icts::DataManagerStatusCode::kCommitError);
  ASSERT_FALSE(status.graph_issues.empty());
  EXPECT_EQ(status.graph_issues.front().code, icts::ClockGraphIssueCode::kPhysicalKindMismatch);
  EXPECT_EQ(status.graph_issues.front().inst_name, "kind_mismatch");
  EXPECT_EQ(status.graph_issues.front().propagation_origin, "synthesized");
  ASSERT_FALSE(status.diagnostics.empty());
  EXPECT_NE(status.message.find("physical_kind_mismatch"), std::string::npos);
  EXPECT_NE(status.message.find("propagation_origin=synthesized"), std::string::npos);
  EXPECT_NE(status.message.find("expected=buffer"), std::string::npos);
  EXPECT_NE(status.message.find("observed=inverter"), std::string::npos);
}

TEST(ClockDAGTest, NoFlipFlopTerminalIsUnavailableAndDoesNotReuseTotalBufferCount)
{
  const ScopedDesignReset scoped_design_reset;

  auto clock_pins = makeClock("clk", "clk_net");
  auto buffer = makeBuffer("buf_no_ff", 10);
  auto* macro_sink = makeSink("macro_sink", icts::InstType::kMacroBlock, 20);
  connectNet("clk_net", clock_pins.source, {buffer.input});
  clock_pins.clock->set_clock_source_net(clock_pins.source_net);
  auto* leaf_net = connectNet("macro_leaf_net", buffer.output, {macro_sink});

  addBufferArc(clock_pins.clock, buffer);
  clock_pins.clock->add_net(leaf_net);
  clock_pins.clock->add_load(macro_sink);

  ASSERT_TRUE(CTSDM.getDesign().rebuildClockDAG());
  const auto stats = CTSDM.getDesign().get_clock_dag().pathBufferStats(clock_pins.clock);
  EXPECT_FALSE(stats.available);
  EXPECT_EQ(stats.status, "no_ff_sink_terminal");
  EXPECT_EQ(stats.min_buffer_count, 0);
  EXPECT_EQ(stats.max_buffer_count, 0);
}

TEST(ClockDAGTest, CycleInvalidatesTopologyAndPathStats)
{
  const ScopedDesignReset scoped_design_reset;

  auto clock_pins = makeClock("clk", "clk_net");
  auto buffer = makeBuffer("buf_loop", 10);
  connectNet("clk_net", clock_pins.source, {buffer.input});
  clock_pins.clock->set_clock_source_net(clock_pins.source_net);
  auto* loop_net = connectNet("loop_net", buffer.output, {buffer.input});
  addBufferArc(clock_pins.clock, buffer);
  clock_pins.clock->add_net(loop_net);

  EXPECT_FALSE(CTSDM.getDesign().rebuildClockDAG());
  EXPECT_TRUE(CTSDM.getDesign().get_clock_dag().hasCycle(clock_pins.clock));
  const auto stats = CTSDM.getDesign().get_clock_dag().pathBufferStats(clock_pins.clock);
  EXPECT_FALSE(stats.available);
  EXPECT_EQ(stats.status, "invalid_topology");
}

TEST(ClockDAGTest, MultiClockQueriesRemainIsolated)
{
  const ScopedDesignReset scoped_design_reset;

  auto clock_zero = makeClock("clk_zero", "clk_zero_net");
  auto* ff_zero = makeSink("ff_zero", icts::InstType::kFlipFlop, 10);
  connectNet("clk_zero_net", clock_zero.source, {ff_zero});
  clock_zero.clock->set_clock_source_net(clock_zero.source_net);
  clock_zero.clock->add_load(ff_zero);

  auto clock_two = makeClock("clk_two", "clk_two_net");
  auto buf_a = makeBuffer("clk_two_buf_a", 20);
  auto buf_b = makeBuffer("clk_two_buf_b", 30);
  auto* ff_two = makeSink("ff_two", icts::InstType::kFlipFlop, 40);
  connectNet("clk_two_net", clock_two.source, {buf_a.input});
  clock_two.clock->set_clock_source_net(clock_two.source_net);
  auto* mid_net = connectNet("clk_two_mid_net", buf_a.output, {buf_b.input});
  auto* leaf_net = connectNet("clk_two_leaf_net", buf_b.output, {ff_two});
  addBufferArc(clock_two.clock, buf_a);
  addBufferArc(clock_two.clock, buf_b);
  clock_two.clock->add_net(mid_net);
  clock_two.clock->add_net(leaf_net);
  clock_two.clock->add_load(ff_two);

  ASSERT_TRUE(CTSDM.getDesign().rebuildClockDAG());
  const auto zero_stats = CTSDM.getDesign().get_clock_dag().pathBufferStats(clock_zero.clock);
  EXPECT_TRUE(zero_stats.available);
  EXPECT_EQ(zero_stats.min_buffer_count, 0);
  EXPECT_EQ(zero_stats.max_buffer_count, 0);

  const auto two_stats = CTSDM.getDesign().get_clock_dag().pathBufferStats(clock_two.clock);
  EXPECT_TRUE(two_stats.available);
  EXPECT_EQ(two_stats.min_buffer_count, 2);
  EXPECT_EQ(two_stats.max_buffer_count, 2);
}

TEST(ClockDAGTest, QorEvaluationPathDepthFieldsUseSourceToFlipFlopDAGStats)
{
  const ScopedDesignReset scoped_design_reset;

  auto clock_pins = makeClock("clk", "clk_net");
  auto buf_one = makeBuffer("qor_buf_one", 10);
  auto buf_two_a = makeBuffer("qor_buf_two_a", 20);
  auto buf_two_b = makeBuffer("qor_buf_two_b", 30);
  auto* ff_one = makeSink("qor_ff_one", icts::InstType::kFlipFlop, 40);
  auto* ff_two = makeSink("qor_ff_two", icts::InstType::kFlipFlop, 50);

  connectNet("clk_net", clock_pins.source, {buf_one.input, buf_two_a.input});
  clock_pins.clock->set_clock_source_net(clock_pins.source_net);
  auto* one_deep_net = connectNet("qor_one_deep_net", buf_one.output, {ff_one});
  auto* two_deep_mid_net = connectNet("qor_two_deep_mid_net", buf_two_a.output, {buf_two_b.input});
  auto* two_deep_leaf_net = connectNet("qor_two_deep_leaf_net", buf_two_b.output, {ff_two});

  addBufferArc(clock_pins.clock, buf_one);
  addBufferArc(clock_pins.clock, buf_two_a);
  addBufferArc(clock_pins.clock, buf_two_b);
  clock_pins.clock->add_net(one_deep_net);
  clock_pins.clock->add_net(two_deep_mid_net);
  clock_pins.clock->add_net(two_deep_leaf_net);
  clock_pins.clock->add_load(ff_one);
  clock_pins.clock->add_load(ff_two);

  icts::QorEvaluationModel model{
      .config = CTSDM.getConfig(),
      .design = CTSDM.getDesign(),
      .wrapper = CTSDM.getWrapper(),
      .state = {},
  };
  icts::QorEvaluation::evaluate(model);
  const auto summary = icts::QorEvaluation::outputSummary(model.state);

  EXPECT_EQ(summary.final_clock_buffer_count, 3);
  EXPECT_EQ(summary.clock_member_buffer_count, 3);
  EXPECT_EQ(summary.path_depth_metric_status, "available");
  EXPECT_EQ(summary.clock_path_min_buffer, 1);
  EXPECT_EQ(summary.clock_path_max_buffer, 2);
  EXPECT_EQ(summary.max_clock_network_level, 2);
  EXPECT_FALSE(summary.final_buffer_area_um2.has_value());
  EXPECT_EQ(summary.qor_metric_status, "partial");
}

TEST(ClockDAGTest, QorEvaluationNoFlipFlopPathDepthIsUnavailableZeroNotTotalBuffers)
{
  const ScopedDesignReset scoped_design_reset;

  auto clock_pins = makeClock("clk", "clk_net");
  auto buffer = makeBuffer("qor_buf_no_ff", 10);
  auto* macro_sink = makeSink("qor_macro_sink", icts::InstType::kMacroBlock, 20);
  connectNet("clk_net", clock_pins.source, {buffer.input});
  clock_pins.clock->set_clock_source_net(clock_pins.source_net);
  auto* leaf_net = connectNet("qor_macro_leaf_net", buffer.output, {macro_sink});

  addBufferArc(clock_pins.clock, buffer);
  clock_pins.clock->add_net(leaf_net);
  clock_pins.clock->add_load(macro_sink);

  icts::QorEvaluationModel model{
      .config = CTSDM.getConfig(),
      .design = CTSDM.getDesign(),
      .wrapper = CTSDM.getWrapper(),
      .state = {},
  };
  icts::QorEvaluation::evaluate(model);
  const auto summary = icts::QorEvaluation::outputSummary(model.state);

  EXPECT_EQ(summary.final_clock_buffer_count, 1);
  EXPECT_EQ(summary.clock_member_buffer_count, 1);
  EXPECT_EQ(summary.path_depth_metric_status, "no_ff_sink_terminal");
  EXPECT_EQ(summary.clock_path_min_buffer, 0);
  EXPECT_EQ(summary.clock_path_max_buffer, 0);
  EXPECT_EQ(summary.max_clock_network_level, 0);
}

}  // namespace
}  // namespace icts_test
