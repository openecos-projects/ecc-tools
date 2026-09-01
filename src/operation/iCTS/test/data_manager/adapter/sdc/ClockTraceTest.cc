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
 * @file ClockTraceTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-08-14
 * @brief Native ownership tests for explicit SDC clock propagation evidence.
 */

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "IdbCellMaster.h"
#include "IdbDesign.h"
#include "IdbEnum.h"
#include "IdbInstance.h"
#include "IdbLayout.h"
#include "IdbNet.h"
#include "IdbPins.h"
#include "IdbTerm.h"
#include "LibParserCpp.hh"
#include "SDCClockReader.hh"
#include "clock_trace/SDCClockTraceAlgorithm.hh"
#include "liberty/Lib.hh"

namespace icts_test {
namespace {

struct PortSpec
{
  std::string name;
  idb::IdbConnectDirection direction = idb::IdbConnectDirection::kInput;
  idb::IdbConnectType type = idb::IdbConnectType::kSignal;
  bool liberty_clock = false;
};

enum class LogicKind
{
  kPlain,
  kBuffer,
  kInverter,
  kClockGate,
};

class ClockTraceFixtureInterface : public testing::Test
{
 protected:
  ClockTraceFixtureInterface() : _design(&_layout) {}

  auto addMaster(const std::string& name, const std::vector<PortSpec>& ports, LogicKind kind = LogicKind::kPlain, bool block = false) -> idb::IdbCellMaster*
  {
    auto* master = _layout.get_cell_master_list()->set_cell_master(name);
    EXPECT_NE(master, nullptr);
    if (master == nullptr) {
      return nullptr;
    }
    master->set_type(block ? idb::CellMasterType::kBlock : idb::CellMasterType::kCore);

    auto lib_cell = std::make_unique<idb::LibCell>(name.c_str(), nullptr);
    for (const auto& port : ports) {
      auto* term = master->add_term(port.name);
      EXPECT_NE(term, nullptr);
      if (term != nullptr) {
        term->set_direction(port.direction);
        term->set_type(port.type);
      }

      auto lib_port = std::make_unique<idb::LibPort>(port.name.c_str());
      lib_port->set_port_type(port.direction == idb::IdbConnectDirection::kOutput ? idb::LibPort::LibertyPortType::kOutput
                                                                                  : idb::LibPort::LibertyPortType::kInput);
      lib_port->set_is_clock(port.liberty_clock);
      lib_port->set_is_clock_pin(port.liberty_clock);
      if (kind == LogicKind::kClockGate && port.liberty_clock) {
        lib_port->set_clock_gate_clock_pin(true);
      }
      if (port.direction == idb::IdbConnectDirection::kOutput && (kind == LogicKind::kBuffer || kind == LogicKind::kPlain)) {
        lib_port->set_func_expr(liberty_convert_expr(liberty_parse_expr("A")));
      } else if (port.direction == idb::IdbConnectDirection::kOutput && kind == LogicKind::kInverter) {
        lib_port->set_func_expr(liberty_convert_expr(liberty_parse_expr("!A")));
      }
      lib_cell->addLibertyPort(std::move(lib_port));
    }
    if (kind == LogicKind::kClockGate) {
      lib_cell->set_is_clock_gating_integrated_cell(true);
    }
    _lib_cells[name] = lib_cell.get();
    _owned_lib_cells.push_back(std::move(lib_cell));
    return master;
  }

  auto addInst(const std::string& name, idb::IdbCellMaster* master, bool sequential = false) -> idb::IdbInstance*
  {
    auto* inst = _design.get_instance_list()->add_instance(name);
    EXPECT_NE(inst, nullptr);
    if (inst != nullptr) {
      inst->set_cell_master(master);
      inst->set_coodinate(static_cast<int32_t>(_design.get_instance_list()->get_instance_list().size() * 100), 0, false);
      if (sequential) {
        inst->set_as_flip_flop_flag();
      }
    }
    return inst;
  }

  static auto pin(idb::IdbInstance* inst, const std::string& name) -> idb::IdbPin* { return inst == nullptr ? nullptr : inst->get_pin_by_term(name); }

  auto connect(const std::string& name, const std::vector<idb::IdbPin*>& pins) -> idb::IdbNet*
  {
    auto* net = _design.createOrFindNet(name, idb::IdbConnectType::kClock);
    EXPECT_NE(net, nullptr);
    for (auto* connected_pin : pins) {
      EXPECT_TRUE(_design.connectPinToNet(connected_pin, net));
    }
    return net;
  }

  static auto primaryClock(const std::string& clock_name, const std::string& net_name) -> icts::SdcClockDecl
  {
    return icts::SdcClockDecl{
        .kind = icts::SdcClockDecl::Kind::kPrimary,
        .clock_name = clock_name,
        .targets = {{.kind = icts::SdcObjectKind::kNet, .pattern = net_name}},
        .generated_sources = {},
        .master_clock_name = {},
        .period_ns = 10.0,
        .period_resolved = true,
        .divide_by = 1,
        .multiply_by = 1,
        .invert = false,
        .is_virtual = false,
    };
  }

  static auto generatedClock(const std::string& clock_name, const std::string& net_name, const std::string& master_name) -> icts::SdcClockDecl
  {
    return icts::SdcClockDecl{
        .kind = icts::SdcClockDecl::Kind::kGenerated,
        .clock_name = clock_name,
        .targets = {{.kind = icts::SdcObjectKind::kNet, .pattern = net_name}},
        .generated_sources = {},
        .master_clock_name = master_name,
        .period_ns = 10.0,
        .period_resolved = true,
        .divide_by = 1,
        .multiply_by = 1,
        .invert = false,
        .is_virtual = false,
    };
  }

  auto trace(const icts::SdcClockData& data, std::size_t max_fanout = 32U) -> icts::ClockTraceBuild
  {
    const icts::SdcLibertyCellLookup lookup = [this](const std::string& master_name) -> idb::LibCell* {
      const auto iter = _lib_cells.find(master_name);
      return iter == _lib_cells.end() ? nullptr : iter->second;
    };
    return icts::SdcClockReader::traceClockTargets(data, &_design, lookup, max_fanout);
  }

  static auto findRecord(const icts::ClockTraceBuild& build, const std::string& clock_name, const std::string& net_name, const std::string& status)
      -> const icts::ClockTraceRecord*
  {
    for (const auto& record : build.summary.records) {
      if (record.clock_name == clock_name && record.net_name == net_name && record.status == status) {
        return &record;
      }
    }
    return nullptr;
  }

  idb::IdbLayout _layout;
  idb::IdbDesign _design;
  std::vector<std::unique_ptr<idb::LibCell>> _owned_lib_cells;
  std::map<std::string, idb::LibCell*> _lib_cells;
};

TEST_F(ClockTraceFixtureInterface, InputOnlyPhysicalBuffersRemainTerminalWithoutPropagationEvidence)
{
  auto* source_master = addMaster("CLK_SOURCE", {{"Y", idb::IdbConnectDirection::kOutput}});
  auto* buffer_master = addMaster("BUF_X1", {{"A"}, {"Y", idb::IdbConnectDirection::kOutput}}, LogicKind::kBuffer);
  auto* inverter_master = addMaster("INV_X1", {{"A"}, {"Y", idb::IdbConnectDirection::kOutput}}, LogicKind::kInverter);
  auto* sink_master = addMaster("DFF_X1", {{"CLK", idb::IdbConnectDirection::kInput, idb::IdbConnectType::kClock, true}});
  auto* source = addInst("source", source_master);
  auto* buffer = addInst("boundary_buf", buffer_master);
  auto* inverter = addInst("boundary_inv", inverter_master);
  auto* sink = addInst("sink", sink_master, true);
  connect("clk", {pin(source, "Y"), pin(buffer, "A"), pin(inverter, "A"), pin(sink, "CLK")});

  const auto build = trace(icts::SdcClockData{.clocks = {primaryClock("clk", "clk")}, .case_analyses = {}, .diagnostics = {}});

  ASSERT_EQ(build.output.clock_targets.size(), 1U);
  EXPECT_EQ(build.output.clock_targets.front().terminal_net_names, std::vector<std::string>{"clk"});
  EXPECT_TRUE(build.output.clock_targets.front().propagation_steps.empty());
  const auto* accepted = findRecord(build, "clk", "clk", "accepted");
  ASSERT_NE(accepted, nullptr);
  EXPECT_TRUE(accepted->propagation_steps.empty());
}

TEST_F(ClockTraceFixtureInterface, CompleteLibertyBufferAndInverterTransitionsProduceTypedSteps)
{
  auto* source_master = addMaster("CLK_SOURCE", {{"Y", idb::IdbConnectDirection::kOutput}});
  auto* buffer_master = addMaster("BUF_X1", {{"A"}, {"Y", idb::IdbConnectDirection::kOutput}}, LogicKind::kBuffer);
  auto* inverter_master = addMaster("INV_X1", {{"A"}, {"Y", idb::IdbConnectDirection::kOutput}}, LogicKind::kInverter);
  auto* sink_master = addMaster("DFF_X1", {{"CLK", idb::IdbConnectDirection::kInput, idb::IdbConnectType::kClock, true}});
  auto* source = addInst("source", source_master);
  auto* buffer = addInst("buf", buffer_master);
  auto* inverter = addInst("inv", inverter_master);
  auto* sink = addInst("sink", sink_master, true);
  connect("root", {pin(source, "Y"), pin(buffer, "A")});
  connect("middle", {pin(buffer, "Y"), pin(inverter, "A")});
  connect("leaf", {pin(inverter, "Y"), pin(sink, "CLK")});

  const auto build = trace(icts::SdcClockData{.clocks = {primaryClock("clk", "root")}, .case_analyses = {}, .diagnostics = {}});

  ASSERT_EQ(build.output.clock_targets.size(), 1U);
  const auto& target = build.output.clock_targets.front();
  ASSERT_EQ(target.propagation_steps.size(), 2U);
  EXPECT_EQ(target.terminal_net_names, (std::vector<std::string>{"leaf", "middle", "root"}));
  EXPECT_EQ(target.propagation_steps[0].clock_name, "clk");
  EXPECT_EQ(target.propagation_steps[0].inst_name, "buf");
  EXPECT_EQ(target.propagation_steps[0].input_pin_name, "A");
  EXPECT_EQ(target.propagation_steps[0].output_pin_name, "Y");
  EXPECT_EQ(target.propagation_steps[0].input_net_name, "root");
  EXPECT_EQ(target.propagation_steps[0].output_net_name, "middle");
  EXPECT_EQ(target.propagation_steps[0].kind, icts::ClockTracePropagationKind::kBuffer);
  EXPECT_EQ(target.propagation_steps[1].inst_name, "inv");
  EXPECT_EQ(target.propagation_steps[1].input_net_name, "middle");
  EXPECT_EQ(target.propagation_steps[1].output_net_name, "leaf");
  EXPECT_EQ(target.propagation_steps[1].kind, icts::ClockTracePropagationKind::kInverter);
  EXPECT_EQ(target.propagation_steps[0].ownership_reason, "sdc_reachable_owned_transition");
  EXPECT_EQ(target.propagation_steps[1].ownership_reason, "sdc_reachable_owned_transition");
}

TEST_F(ClockTraceFixtureInterface, TruePropagationPreservesInputOnlySameClockBoundaryNet)
{
  auto* source_master = addMaster("CLK_SOURCE", {{"Y", idb::IdbConnectDirection::kOutput}});
  auto* buffer_master = addMaster("BUF_X1", {{"A"}, {"Y", idb::IdbConnectDirection::kOutput}}, LogicKind::kBuffer);
  auto* inverter_master = addMaster("INV_X1", {{"A"}, {"Y", idb::IdbConnectDirection::kOutput}}, LogicKind::kInverter);
  auto* sink_master = addMaster("DFF_X1", {{"CLK", idb::IdbConnectDirection::kInput, idb::IdbConnectType::kClock, true}});
  auto* source = addInst("source", source_master);
  auto* buffer = addInst("propagating_buf", buffer_master);
  auto* boundary = addInst("input_only_inv", inverter_master);
  auto* sink = addInst("sink", sink_master, true);
  connect("root", {pin(source, "Y"), pin(buffer, "A"), pin(boundary, "A")});
  connect("leaf", {pin(buffer, "Y"), pin(sink, "CLK")});

  const auto build = trace(icts::SdcClockData{.clocks = {primaryClock("clk", "root")}, .case_analyses = {}, .diagnostics = {}});

  ASSERT_TRUE(build.ok());
  ASSERT_EQ(build.output.clock_targets.size(), 1U);
  const auto& target = build.output.clock_targets.front();
  ASSERT_EQ(target.propagation_steps.size(), 1U);
  EXPECT_EQ(target.propagation_steps.front().inst_name, "propagating_buf");
  EXPECT_EQ(target.propagation_steps.front().input_net_name, "root");
  EXPECT_EQ(target.propagation_steps.front().output_net_name, "leaf");
  EXPECT_EQ(target.terminal_net_names, (std::vector<std::string>{"leaf", "root"}));
}

TEST_F(ClockTraceFixtureInterface, DirectSourceSinksRemainOwnedIndependentOfSynthesisFanout)
{
  auto* source_master = addMaster("CLK_SOURCE", {{"Y", idb::IdbConnectDirection::kOutput}});
  auto* buffer_master = addMaster("BUF_X1", {{"A"}, {"Y", idb::IdbConnectDirection::kOutput}}, LogicKind::kBuffer);
  auto* sink_master = addMaster("DFF_X1", {{"CLK", idb::IdbConnectDirection::kInput, idb::IdbConnectType::kClock, true}});
  auto* source = addInst("source", source_master);
  auto* buffer = addInst("buf", buffer_master);
  auto* direct_sink = addInst("direct_sink", sink_master, true);
  connect("root", {pin(source, "Y"), pin(buffer, "A"), pin(direct_sink, "CLK")});

  std::vector<idb::IdbPin*> leaf_pins{pin(buffer, "Y")};
  for (std::size_t index = 0U; index < 5U; ++index) {
    auto* sink = addInst("leaf_sink_" + std::to_string(index), sink_master, true);
    leaf_pins.push_back(pin(sink, "CLK"));
  }
  connect("leaf", leaf_pins);
  const auto data = icts::SdcClockData{.clocks = {primaryClock("clk", "root")}, .case_analyses = {}, .diagnostics = {}};

  const auto low_fanout_build = trace(data, 1U);
  const auto high_fanout_build = trace(data, 128U);

  ASSERT_TRUE(low_fanout_build.ok());
  ASSERT_TRUE(high_fanout_build.ok());
  ASSERT_EQ(low_fanout_build.output.clock_targets.size(), 1U);
  ASSERT_EQ(high_fanout_build.output.clock_targets.size(), 1U);
  EXPECT_EQ(low_fanout_build.output.clock_targets.front().terminal_net_names, (std::vector<std::string>{"leaf", "root"}));
  EXPECT_EQ(high_fanout_build.output.clock_targets.front().terminal_net_names, low_fanout_build.output.clock_targets.front().terminal_net_names);
  EXPECT_NE(findRecord(low_fanout_build, "clk", "root", "accepted"), nullptr);
  EXPECT_NE(findRecord(high_fanout_build, "clk", "root", "accepted"), nullptr);
}

TEST_F(ClockTraceFixtureInterface, AmbiguousOwnershipRejectsTheWholeTraceInputDeterministically)
{
  auto* source_master = addMaster("CLK_SOURCE", {{"Y", idb::IdbConnectDirection::kOutput}});
  auto* sink_master = addMaster("DFF_X1", {{"CLK", idb::IdbConnectDirection::kInput, idb::IdbConnectType::kClock, true}});
  auto* source = addInst("source", source_master);
  auto* sink = addInst("sink", sink_master, true);
  connect("shared", {pin(source, "Y"), pin(sink, "CLK")});
  auto left = primaryClock("left", "shared");
  auto right = primaryClock("right", "shared");

  const auto forward = trace(icts::SdcClockData{.clocks = {left, right}, .case_analyses = {}, .diagnostics = {}});
  const auto reverse = trace(icts::SdcClockData{.clocks = {right, left}, .case_analyses = {}, .diagnostics = {}});

  EXPECT_EQ(forward.status, icts::ClockTraceBuildStatusCode::kAmbiguousOwnership);
  EXPECT_EQ(forward.message, "clock_trace_ambiguous_ownership");
  EXPECT_TRUE(forward.output.clock_targets.empty());
  ASSERT_EQ(forward.summary.records.size(), 2U);
  const auto* left_record = findRecord(forward, "left", "shared", "ambiguous");
  const auto* right_record = findRecord(forward, "right", "shared", "ambiguous");
  ASSERT_NE(left_record, nullptr);
  ASSERT_NE(right_record, nullptr);
  EXPECT_EQ(left_record->reason, "target_net_reachable_from_multiple_sdc_clocks");
  EXPECT_EQ(right_record->reason, "target_net_reachable_from_multiple_sdc_clocks");
  ASSERT_EQ(reverse.summary.records.size(), forward.summary.records.size());
  for (std::size_t index = 0U; index < forward.summary.records.size(); ++index) {
    EXPECT_EQ(reverse.summary.records[index].clock_name, forward.summary.records[index].clock_name);
    EXPECT_EQ(reverse.summary.records[index].net_name, forward.summary.records[index].net_name);
    EXPECT_EQ(reverse.summary.records[index].status, forward.summary.records[index].status);
    EXPECT_EQ(reverse.summary.records[index].reason, forward.summary.records[index].reason);
  }
}

TEST_F(ClockTraceFixtureInterface, ClockGateMuxLatchAndMacroAreTerminalBoundaries)
{
  auto* source_master = addMaster("CLK_SOURCE", {{"Y", idb::IdbConnectDirection::kOutput}});
  auto* icg_master
      = addMaster("ICG_X1", {{"CK", idb::IdbConnectDirection::kInput, idb::IdbConnectType::kSignal, true}, {"EN"}, {"Q", idb::IdbConnectDirection::kOutput}},
                  LogicKind::kClockGate);
  auto* mux_master = addMaster("MUX_X1", {{"A"}, {"B"}, {"Y", idb::IdbConnectDirection::kOutput}});
  auto* latch_master = addMaster("LATCH_X1", {{"G", idb::IdbConnectDirection::kInput, idb::IdbConnectType::kClock, true}});
  auto* macro_master = addMaster("MACRO", {{"CLK", idb::IdbConnectDirection::kInput, idb::IdbConnectType::kSignal, true}}, LogicKind::kPlain, true);
  auto* sink_master = addMaster("DFF_X1", {{"CLK", idb::IdbConnectDirection::kInput, idb::IdbConnectType::kClock, true}});
  auto* source = addInst("source", source_master);
  auto* icg = addInst("icg", icg_master);
  auto* mux = addInst("mux", mux_master);
  auto* latch = addInst("latch", latch_master, true);
  auto* macro = addInst("macro", macro_master);
  auto* icg_sink = addInst("icg_sink", sink_master, true);
  auto* mux_sink = addInst("mux_sink", sink_master, true);
  connect("root", {pin(source, "Y"), pin(icg, "CK"), pin(mux, "A"), pin(latch, "G"), pin(macro, "CLK")});
  connect("icg_leaf", {pin(icg, "Q"), pin(icg_sink, "CLK")});
  connect("mux_leaf", {pin(mux, "Y"), pin(mux_sink, "CLK")});

  const auto build = trace(icts::SdcClockData{.clocks = {primaryClock("clk", "root")}, .case_analyses = {}, .diagnostics = {}});

  const auto* accepted = findRecord(build, "clk", "root", "accepted");
  ASSERT_NE(accepted, nullptr);
  EXPECT_EQ(accepted->sequential_clock_sinks, 1U);
  EXPECT_EQ(accepted->macro_clock_sinks, 1U);
  EXPECT_TRUE(accepted->propagation_steps.empty());
  const auto* icg_stop = findRecord(build, "clk", "icg_leaf", "trace_stop");
  const auto* mux_stop = findRecord(build, "clk", "mux_leaf", "trace_stop");
  ASSERT_NE(icg_stop, nullptr);
  ASSERT_NE(mux_stop, nullptr);
  EXPECT_EQ(icg_stop->reason, "non_propagation_clock_boundary");
  EXPECT_EQ(mux_stop->reason, "non_propagation_clock_boundary");
  EXPECT_EQ(findRecord(build, "clk", "icg_leaf", "accepted"), nullptr);
  EXPECT_EQ(findRecord(build, "clk", "mux_leaf", "accepted"), nullptr);
  for (const auto& record : build.summary.records) {
    EXPECT_TRUE(record.propagation_steps.empty());
  }
}

TEST_F(ClockTraceFixtureInterface, GeneratedClockTargetStopsMasterClockOwnershipBeforeItsBoundary)
{
  auto* source_master = addMaster("CLK_SOURCE", {{"Y", idb::IdbConnectDirection::kOutput}});
  auto* buffer_master = addMaster("BUF_X1", {{"A"}, {"Y", idb::IdbConnectDirection::kOutput}}, LogicKind::kBuffer);
  auto* sink_master = addMaster("DFF_X1", {{"CLK", idb::IdbConnectDirection::kInput, idb::IdbConnectType::kClock, true}});
  auto* source = addInst("source", source_master);
  auto* buffer = addInst("gen_driver", buffer_master);
  auto* sink = addInst("sink", sink_master, true);
  connect("root", {pin(source, "Y"), pin(buffer, "A")});
  connect("generated", {pin(buffer, "Y"), pin(sink, "CLK")});
  auto primary = primaryClock("master", "root");
  auto generated = generatedClock("generated", "generated", "master");

  const auto build = trace(icts::SdcClockData{.clocks = {primary, generated}, .case_analyses = {}, .diagnostics = {}});

  const auto* master_stop = findRecord(build, "master", "generated", "trace_stop");
  ASSERT_NE(master_stop, nullptr);
  EXPECT_EQ(master_stop->target_kind, "generated_clock_boundary");
  EXPECT_EQ(master_stop->reason, "generated");
  EXPECT_EQ(findRecord(build, "master", "generated", "accepted"), nullptr);
  const auto* generated_accept = findRecord(build, "generated", "generated", "accepted");
  ASSERT_NE(generated_accept, nullptr);
  EXPECT_TRUE(generated_accept->propagation_steps.empty());
}

TEST_F(ClockTraceFixtureInterface, PreclusterReuseTreatsLeafDriversAsAnchorsNotPropagationArcs)
{
  auto* source_master = addMaster("CLK_SOURCE", {{"Y", idb::IdbConnectDirection::kOutput}});
  auto* buffer_master = addMaster("BUF_X1", {{"A"}, {"Y", idb::IdbConnectDirection::kOutput}}, LogicKind::kBuffer);
  auto* sink_master = addMaster("DFF_X1", {{"CLK", idb::IdbConnectDirection::kInput, idb::IdbConnectType::kClock, true}});
  auto* source = addInst("source", source_master);
  auto* left = addInst("left_anchor", buffer_master);
  auto* right = addInst("right_anchor", buffer_master);
  auto* left_sink = addInst("left_sink", sink_master, true);
  auto* right_sink = addInst("right_sink", sink_master, true);
  connect("root", {pin(source, "Y"), pin(left, "A"), pin(right, "A")});
  connect("left_leaf", {pin(left, "Y"), pin(left_sink, "CLK")});
  connect("right_leaf", {pin(right, "Y"), pin(right_sink, "CLK")});

  const auto build = trace(icts::SdcClockData{.clocks = {primaryClock("clk", "root")}, .case_analyses = {}, .diagnostics = {}});

  ASSERT_EQ(build.output.clock_targets.size(), 1U);
  const auto& target = build.output.clock_targets.front();
  EXPECT_TRUE(target.preclustered_sink_reuse);
  ASSERT_EQ(target.preclustered_sink_anchors.size(), 2U);
  EXPECT_TRUE(target.terminal_net_names.empty());
  EXPECT_TRUE(target.propagation_steps.empty());
  EXPECT_EQ(target.preclustered_sink_anchors[0].input_net_name, "root");
  EXPECT_EQ(target.preclustered_sink_anchors[1].input_net_name, "root");
}

TEST_F(ClockTraceFixtureInterface, MultipleResolvedSeedsDoNotCreateCompatibilityClockTargets)
{
  auto* source_master = addMaster("CLK_SOURCE", {{"Y", idb::IdbConnectDirection::kOutput}});
  auto* sink_master = addMaster("DFF_X1", {{"CLK", idb::IdbConnectDirection::kInput, idb::IdbConnectType::kClock, true}});
  auto* left_source = addInst("left_source", source_master);
  auto* right_source = addInst("right_source", source_master);
  auto* left_sink = addInst("left_sink", sink_master, true);
  auto* right_sink = addInst("right_sink", sink_master, true);
  connect("left", {pin(left_source, "Y"), pin(left_sink, "CLK")});
  connect("right", {pin(right_source, "Y"), pin(right_sink, "CLK")});
  auto clock = primaryClock("clk", "left");
  clock.targets.push_back({.kind = icts::SdcObjectKind::kNet, .pattern = "right"});

  const auto build = trace(icts::SdcClockData{.clocks = {clock}, .case_analyses = {}, .diagnostics = {}});

  EXPECT_NE(findRecord(build, "clk", "left", "accepted"), nullptr);
  EXPECT_NE(findRecord(build, "clk", "right", "accepted"), nullptr);
  EXPECT_TRUE(build.output.clock_targets.empty());
}

}  // namespace
}  // namespace icts_test
