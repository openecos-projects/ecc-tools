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
 * @date 2026-05-18
 * @brief Unit tests for CTS fast STA data, timing, power, and incremental APIs.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "FastSTA.hh"
#include "FastSTADmpCeff.hh"
#include "FastSTAIncremental.hh"
#include "FastSTAParasitics.hh"
#include "FastSTAPower.hh"
#include "FastSTATiming.hh"
#include "clock_net_parasitic/FastSTAClockNetParasitic.hh"
#include "clock_sizing/FastSTAClockSizingEdit.hh"
#include "clock_state/FastSTABuilder.hh"
#include "clock_state/FastSTAClockState.hh"
#include "data_manager/DataManager.hh"
#include "data_manager/config/Config.hh"
#include "design/Clock.hh"
#include "design/Inst.hh"
#include "design/Pin.hh"
#include "io/Wrapper.hh"
#include "liberty/FastSTALibertyModel.hh"
#include "segment_char/FastSTAChar.hh"
#include "spatial/Point.hh"
#include "timing/FastSTAClockTiming.hh"

namespace icts_test {
namespace {

auto MakeAxis(icts::FastStaLibertyAxisKind kind, std::vector<double> values) -> icts::FastStaLibertyAxis
{
  return icts::FastStaLibertyAxis{.kind = kind, .values = std::move(values)};
}

auto MakeTable(icts::FastStaLibertyTableKind kind, double base) -> icts::FastStaLibertyTable
{
  return icts::FastStaLibertyTable{
      .kind = kind,
      .transition = icts::FastStaTransition::kRise,
      .axes = {MakeAxis(icts::FastStaLibertyAxisKind::kInputSlew, {0.0, 1.0}), MakeAxis(icts::FastStaLibertyAxisKind::kOutputLoad, {0.0, 2.0})},
      .values = {base, base + 0.2, base + 0.1, base + 0.3},
  };
}

auto MakeCell(const std::string& master, double input_cap_pf, double area_um2, double leakage_w) -> icts::FastStaLibertyCell
{
  return icts::FastStaLibertyCell{
      .cell_master = master,
      .input_port = "A",
      .output_port = "Y",
      .input_cap_pf = input_cap_pf,
      .output_cap_limit_pf = 3.0,
      .input_slew_limit_ns = 1.0,
      .area_um2 = area_um2,
      .voltage_v = 1.0,
      .leakage_power_w = leakage_w,
      .timing_arc = icts::FastStaLibertyArc{
          .from_port = "A",
          .to_port = "Y",
          .delay_tables = {MakeTable(icts::FastStaLibertyTableKind::kCellDelay, 0.10)},
          .slew_tables = {MakeTable(icts::FastStaLibertyTableKind::kOutputSlew, 0.20)},
          .internal_power_tables = {MakeTable(icts::FastStaLibertyTableKind::kInternalPower, 0.50)},
      },
  };
}

auto MakeOpenStaAlignmentTable(icts::FastStaLibertyTableKind kind, double low_slew_low_cap, double low_slew_high_cap, double high_slew_low_cap,
                               double high_slew_high_cap) -> icts::FastStaLibertyTable
{
  return icts::FastStaLibertyTable{
      .kind = kind,
      .transition = icts::FastStaTransition::kRise,
      .axes = {MakeAxis(icts::FastStaLibertyAxisKind::kInputSlew, {0.1, 0.5}), MakeAxis(icts::FastStaLibertyAxisKind::kOutputLoad, {0.1, 1.0})},
      .values = {low_slew_low_cap, low_slew_high_cap, high_slew_low_cap, high_slew_high_cap},
  };
}

auto MakeOpenStaAlignmentCell() -> icts::FastStaLibertyCell
{
  return icts::FastStaLibertyCell{
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
          .from_port = "A",
          .to_port = "Y",
          .delay_tables = {MakeOpenStaAlignmentTable(icts::FastStaLibertyTableKind::kCellDelay, 0.110, 0.200, 0.150, 0.240)},
          .slew_tables = {MakeOpenStaAlignmentTable(icts::FastStaLibertyTableKind::kOutputSlew, 0.210, 0.300, 0.250, 0.340)},
          .internal_power_tables = {MakeOpenStaAlignmentTable(icts::FastStaLibertyTableKind::kInternalPower, 0.510, 0.600, 0.550, 0.640)},
      },
  };
}

auto MakeNode(icts::FastStaNodeKind kind, std::string name, std::string inst_name, std::string pin_name, std::string cell_master, icts::FastStaPoint location,
              double input_cap_pf, icts::FastStaNetId incoming_net_id, std::vector<icts::FastStaNetId> output_net_ids) -> icts::FastStaNode
{
  icts::FastStaNode node;
  node.kind = kind;
  node.name = std::move(name);
  node.inst_name = std::move(inst_name);
  node.pin_name = std::move(pin_name);
  node.cell_master = std::move(cell_master);
  node.location = location;
  node.input_cap_pf = input_cap_pf;
  node.incoming_net_id = incoming_net_id;
  node.output_net_ids = std::move(output_net_ids);
  return node;
}

auto MakeRcNode(std::string name, double wire_cap_pf, double pin_cap_pf, double cap_pf, double elmore_delay_ns, icts::FastStaNodeId terminal_node_id)
    -> icts::FastStaRcNode
{
  icts::FastStaRcNode node;
  node.name = std::move(name);
  node.wire_cap_pf = wire_cap_pf;
  node.pin_cap_pf = pin_cap_pf;
  node.cap_pf = cap_pf;
  node.elmore_delay_ns = elmore_delay_ns;
  node.terminal_node_id = terminal_node_id;
  return node;
}

auto MakeParasitic(std::vector<icts::FastStaRcNode> rc_nodes, std::vector<icts::FastStaRcEdge> rc_edges, icts::FastStaRcNodeId root_rc_node_id,
                   icts::FastStaPiModel pi = {}, double total_cap_pf = 0.0, bool pre_reduced_pi_elmore = false) -> icts::FastStaNetParasitic
{
  icts::FastStaNetParasitic parasitic;
  parasitic.rc_nodes = std::move(rc_nodes);
  parasitic.rc_edges = std::move(rc_edges);
  parasitic.root_rc_node_id = root_rc_node_id;
  parasitic.pi = pi;
  parasitic.total_cap_pf = total_cap_pf;
  parasitic.pre_reduced_pi_elmore = pre_reduced_pi_elmore;
  return parasitic;
}

auto MakeNet(std::string name, icts::FastStaNodeId driver_node_id, std::vector<icts::FastStaNodeId> load_node_ids, double max_cap_pf,
             icts::FastStaNetParasitic parasitic = {}) -> icts::FastStaNet
{
  icts::FastStaNet net;
  net.name = std::move(name);
  net.driver_node_id = driver_node_id;
  net.load_node_ids = std::move(load_node_ids);
  net.max_cap_pf = max_cap_pf;
  net.parasitic = std::move(parasitic);
  return net;
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

auto MakeTinyContext() -> icts::FastStaClockContext
{
  icts::FastStaClockContext context;
  context.clock_name = "clk";
  context.clock_net_name = "clk_net";
  context.clock_period_ns = 10.0;
  context.dbu_per_um = 1000;
  context.routing_layer = 1;
  context.liberty_cell_by_master["BUF_X1"] = MakeCell("BUF_X1", 0.20, 1.5, 0.01);
  context.liberty_cell_by_master["BUF_X2"] = MakeCell("BUF_X2", 0.40, 2.5, 0.02);

  context.source_node_id = 0U;
  context.nodes = {
      MakeNode(icts::FastStaNodeKind::kSource, "clk_src", "", "clk_src", "", icts::FastStaPoint{.x_dbu = 0, .y_dbu = 0}, 0.0, icts::kInvalidFastStaNetId, {0U}),
      MakeNode(icts::FastStaNodeKind::kBufferInput, "buf/A", "buf", "A", "BUF_X1", icts::FastStaPoint{.x_dbu = 1000, .y_dbu = 0}, 0.20, 0U, {}),
      MakeNode(icts::FastStaNodeKind::kBufferOutput, "buf/Y", "buf", "Y", "BUF_X1", icts::FastStaPoint{.x_dbu = 1000, .y_dbu = 0}, 0.0,
               icts::kInvalidFastStaNetId, {1U}),
      MakeNode(icts::FastStaNodeKind::kSink, "sink/CLK", "sink", "CLK", "", icts::FastStaPoint{.x_dbu = 2000, .y_dbu = 0}, 0.10, 1U, {}),
  };
  context.node_id_by_name = {{"clk_src", 0U}, {"buf/A", 1U}, {"buf/Y", 2U}, {"sink/CLK", 3U}};
  context.buffer_input_node_id_by_inst = {{"buf", 1U}};
  context.buffer_output_node_id_by_inst = {{"buf", 2U}};
  context.node_id_by_location = {{{0, 0}, 0U}, {{1000, 0}, 1U}, {{2000, 0}, 3U}};
  context.nets = {
      MakeNet("clk_net", 0U, {1U}, 3.0,
              MakeParasitic({MakeRcNode("clk_net@0", 0.0, 0.0, 0.0, 0.0, 0U), MakeRcNode("clk_net@1", 0.0, 0.20, 0.20, 0.0, 1U)},
                            {icts::FastStaRcEdge{.from = 0U, .to = 1U, .resistance_ohm = 100.0}}, 0U)),
      MakeNet("leaf_net", 2U, {3U}, 3.0,
              MakeParasitic({MakeRcNode("leaf_net@0", 0.0, 0.0, 0.0, 0.0, 2U), MakeRcNode("leaf_net@1", 0.0, 0.10, 0.10, 0.0, 3U)},
                            {icts::FastStaRcEdge{.from = 0U, .to = 1U, .resistance_ohm = 100.0}}, 0U)),
  };
  context.net_id_by_name = {{"clk_net", 0U}, {"leaf_net", 1U}};
  return context;
}

auto MakeTwoLevelContext() -> icts::FastStaClockContext
{
  icts::FastStaClockContext context;
  context.clock_name = "clk";
  context.clock_net_name = "clk_net";
  context.clock_period_ns = 10.0;
  context.liberty_cell_by_master["BUF_X1"] = MakeCell("BUF_X1", 0.20, 1.5, 0.01);
  context.liberty_cell_by_master["BUF_X2"] = MakeCell("BUF_X2", 0.40, 2.5, 0.02);

  context.source_node_id = 0U;
  context.nodes = {
      MakeNode(icts::FastStaNodeKind::kSource, "clk_src", "", "clk_src", "", {}, 0.0, icts::kInvalidFastStaNetId, {0U}),
      MakeNode(icts::FastStaNodeKind::kBufferInput, "buf1/A", "buf1", "A", "BUF_X1", {}, 0.20, 0U, {}),
      MakeNode(icts::FastStaNodeKind::kBufferOutput, "buf1/Y", "buf1", "Y", "BUF_X1", {}, 0.0, icts::kInvalidFastStaNetId, {1U}),
      MakeNode(icts::FastStaNodeKind::kBufferInput, "buf2/A", "buf2", "A", "BUF_X1", {}, 0.20, 1U, {}),
      MakeNode(icts::FastStaNodeKind::kBufferOutput, "buf2/Y", "buf2", "Y", "BUF_X1", {}, 0.0, icts::kInvalidFastStaNetId, {2U}),
      MakeNode(icts::FastStaNodeKind::kSink, "sink/CLK", "sink", "CLK", "", {}, 0.10, 2U, {}),
  };
  context.node_id_by_name = {{"clk_src", 0U}, {"buf1/A", 1U}, {"buf1/Y", 2U}, {"buf2/A", 3U}, {"buf2/Y", 4U}, {"sink/CLK", 5U}};
  context.buffer_input_node_id_by_inst = {{"buf1", 1U}, {"buf2", 3U}};
  context.buffer_output_node_id_by_inst = {{"buf1", 2U}, {"buf2", 4U}};
  context.nets = {
      MakeNet("clk_net", 0U, {1U}, 3.0,
              MakeParasitic({MakeRcNode("clk_net@0", 0.0, 0.0, 0.0, 0.0, 0U), MakeRcNode("clk_net@1", 0.0, 0.20, 0.20, 0.0, 1U)},
                            {icts::FastStaRcEdge{.from = 0U, .to = 1U, .resistance_ohm = 100.0}}, 0U)),
      MakeNet("mid_net", 2U, {3U}, 3.0,
              MakeParasitic({MakeRcNode("mid_net@0", 0.0, 0.0, 0.0, 0.0, 2U), MakeRcNode("mid_net@1", 0.0, 0.20, 0.20, 0.0, 3U)},
                            {icts::FastStaRcEdge{.from = 0U, .to = 1U, .resistance_ohm = 100.0}}, 0U)),
      MakeNet("leaf_net", 4U, {5U}, 3.0,
              MakeParasitic({MakeRcNode("leaf_net@0", 0.0, 0.0, 0.0, 0.0, 4U), MakeRcNode("leaf_net@1", 0.0, 0.10, 0.10, 0.0, 5U)},
                            {icts::FastStaRcEdge{.from = 0U, .to = 1U, .resistance_ohm = 100.0}}, 0U)),
  };
  context.net_id_by_name = {{"clk_net", 0U}, {"mid_net", 1U}, {"leaf_net", 2U}};
  return context;
}

auto MakeScaleContext(std::size_t node_count) -> icts::FastStaClockContext
{
  icts::FastStaClockContext context;
  context.clock_name = "scale_clk";
  context.clock_net_name = "scale_source_net";
  context.clock_period_ns = 10.0;
  context.root_input_slew_ns = 0.1;
  context.liberty_cell_by_master["BUF_X1"] = MakeCell("BUF_X1", 0.20, 1.5, 0.01);
  context.liberty_cell_by_master["BUF_X2"] = MakeCell("BUF_X2", 0.40, 2.5, 0.02);

  const auto buffer_count = (node_count - 2U) / 2U;
  const auto buffer_input_id = [](std::size_t buffer_id) -> icts::FastStaNodeId { return 1U + 2U * buffer_id; };
  const auto buffer_output_id = [](std::size_t buffer_id) -> icts::FastStaNodeId { return 2U + 2U * buffer_id; };
  context.nodes.reserve(node_count);
  context.nets.reserve(buffer_count + 1U);
  context.node_id_by_name.reserve(node_count);
  context.buffer_input_node_id_by_inst.reserve(buffer_count);
  context.buffer_output_node_id_by_inst.reserve(buffer_count);
  context.net_id_by_name.reserve(buffer_count + 1U);

  context.source_node_id = 0U;
  context.nodes.push_back(MakeNode(icts::FastStaNodeKind::kSource, "scale_source", "", "scale_source", "", {}, 0.0, icts::kInvalidFastStaNetId, {0U}));
  context.node_id_by_name.emplace("scale_source", 0U);
  for (std::size_t buffer_id = 0U; buffer_id < buffer_count; ++buffer_id) {
    const auto inst_name = "scale_buf_" + std::to_string(buffer_id);
    const auto input_name = inst_name + "/A";
    const auto output_name = inst_name + "/Y";
    const auto input_id = buffer_input_id(buffer_id);
    const auto output_id = buffer_output_id(buffer_id);
    const auto incoming_net_id = buffer_id == 0U ? 0U : (buffer_id - 1U) / 2U + 1U;
    context.nodes.push_back(MakeNode(icts::FastStaNodeKind::kBufferInput, input_name, inst_name, "A", "BUF_X1", {}, 0.20, incoming_net_id, {}));
    context.nodes.push_back(
        MakeNode(icts::FastStaNodeKind::kBufferOutput, output_name, inst_name, "Y", "BUF_X1", {}, 0.0, icts::kInvalidFastStaNetId, {buffer_id + 1U}));
    context.node_id_by_name.emplace(input_name, input_id);
    context.node_id_by_name.emplace(output_name, output_id);
    context.buffer_input_node_id_by_inst.emplace(inst_name, input_id);
    context.buffer_output_node_id_by_inst.emplace(inst_name, output_id);
  }

  const auto sink_node_id = context.nodes.size();
  context.nodes.push_back(MakeNode(icts::FastStaNodeKind::kSink, "scale_sink/CLK", "scale_sink", "CLK", "", {}, 0.10, buffer_count, {}));
  context.node_id_by_name.emplace("scale_sink/CLK", sink_node_id);

  context.nets.push_back(MakeNet("scale_source_net", 0U, {buffer_input_id(0U)}, 3.0));
  context.net_id_by_name.emplace("scale_source_net", 0U);
  for (std::size_t buffer_id = 0U; buffer_id < buffer_count; ++buffer_id) {
    std::vector<icts::FastStaNodeId> load_node_ids;
    const auto left_child = 2U * buffer_id + 1U;
    const auto right_child = left_child + 1U;
    if (left_child < buffer_count) {
      load_node_ids.push_back(buffer_input_id(left_child));
    }
    if (right_child < buffer_count) {
      load_node_ids.push_back(buffer_input_id(right_child));
    }
    if (buffer_id + 1U == buffer_count) {
      load_node_ids.push_back(sink_node_id);
    }
    const auto net_name = "scale_net_" + std::to_string(buffer_id);
    context.nets.push_back(MakeNet(net_name, buffer_output_id(buffer_id), std::move(load_node_ids), 3.0));
    context.net_id_by_name.emplace(net_name, buffer_id + 1U);
  }
  return context;
}

auto MakeScaleChanges(std::size_t node_count) -> std::vector<icts::FastStaBufferMasterChange>
{
  const auto buffer_count = (node_count - 2U) / 2U;
  const auto parent_buffer_id = buffer_count / 2U - 1U;
  const auto left_child = 2U * parent_buffer_id + 1U;
  const auto right_child = left_child + 1U;
  return {
      {.node_id = 1U + 2U * left_child, .cell_master = "BUF_X2"},
      {.node_id = 1U + 2U * right_child, .cell_master = "BUF_X2"},
  };
}

auto TimingStatesMatch(const icts::FastStaClockContext& lhs, const icts::FastStaClockContext& rhs) -> bool
{
  if (lhs.nodes.size() != rhs.nodes.size() || lhs.timing_valid != rhs.timing_valid || lhs.power_valid != rhs.power_valid || lhs.skew.valid != rhs.skew.valid
      || lhs.skew.min_sink_node_id != rhs.skew.min_sink_node_id || lhs.skew.max_sink_node_id != rhs.skew.max_sink_node_id
      || std::abs(lhs.skew.min_arrival_ns - rhs.skew.min_arrival_ns) > 1e-12 || std::abs(lhs.skew.max_arrival_ns - rhs.skew.max_arrival_ns) > 1e-12
      || std::abs(lhs.skew.skew_ns - rhs.skew.skew_ns) > 1e-12) {
    return false;
  }
  for (std::size_t node_id = 0U; node_id < lhs.nodes.size(); ++node_id) {
    const auto& lhs_node = lhs.nodes.at(node_id);
    const auto& rhs_node = rhs.nodes.at(node_id);
    if (lhs_node.cell_master != rhs_node.cell_master || lhs_node.timing.valid != rhs_node.timing.valid
        || std::abs(lhs_node.timing.arrival_ns - rhs_node.timing.arrival_ns) > 1e-12 || std::abs(lhs_node.timing.slew_ns - rhs_node.timing.slew_ns) > 1e-12) {
      return false;
    }
  }
  return true;
}

auto MeasureScaleRoutes(const icts::FastStaClockContext& baseline_context, const std::vector<icts::FastStaBufferMasterChange>& changes, double& full_replay_us,
                        double& incremental_replay_us) -> bool
{
  auto full_context = baseline_context;
  const auto full_start = std::chrono::steady_clock::now();
  const auto full_ok = icts::FastStaIncremental::changeBufferMasters(full_context, changes) && icts::FastStaTiming::update(full_context);
  const auto full_finish = std::chrono::steady_clock::now();

  auto incremental_context = baseline_context;
  const auto incremental_start = std::chrono::steady_clock::now();
  const auto dirty_region = icts::FastStaIncremental::changeBufferMastersIncremental(incremental_context, changes);
  const auto incremental_ok = dirty_region.has_value() && icts::FastStaTiming::updateRegion(incremental_context, dirty_region.value());
  const auto incremental_finish = std::chrono::steady_clock::now();

  full_replay_us = std::chrono::duration<double, std::micro>(full_finish - full_start).count();
  incremental_replay_us = std::chrono::duration<double, std::micro>(incremental_finish - incremental_start).count();
  return full_ok && incremental_ok && TimingStatesMatch(full_context, incremental_context);
}

auto Median(std::vector<double> samples) -> double
{
  std::ranges::sort(samples);
  return samples.at(samples.size() / 2U);
}

auto MakeOpenStaAlignmentPathContext() -> icts::FastStaClockContext
{
  icts::FastStaClockContext context;
  context.clock_name = "clk";
  context.clock_net_name = "clk";
  context.clock_period_ns = 10.0;
  context.liberty_cell_by_master["BUF_X1"] = MakeOpenStaAlignmentCell();

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
  icts::Wrapper wrapper;
  icts::Clock clock("clk", "clk_net");
  clock.set_clock_period_ns(10.0);
  icts::Inst sink_inst("u_sink", "DFFQX1H7L", icts::InstType::kFlipFlop, icts::Point<int>(100, 200));
  icts::Pin sink_pin("CK", icts::PinType::kClock, icts::Point<int>(100, 200), &sink_inst);
  sink_inst.add_pin(&sink_pin);
  clock.add_inst(&sink_inst);

  const auto build = icts::FastStaBuilder::buildClockContext(
      icts::FastStaEnvironment{
          .wrapper = &wrapper,
          .dbu_per_um = 1000,
          .routing_layer = 1,
          .root_input_slew_ns = 0.1,
          .max_cap_pf = 1.0,
          .max_sink_tran_ns = 1.0,
      },
      icts::FastStaClockBuildInput{.clock = &clock});

  if (!build.context.has_value()) {
    ADD_FAILURE() << build.failure_reason;
    return;
  }
  const auto& context = build.context.value();
  ASSERT_EQ(context.nodes.size(), 1U);
  EXPECT_EQ(context.nodes.front().kind, icts::FastStaNodeKind::kSink);
  EXPECT_EQ(context.nodes.front().cell_master, "DFFQX1H7L");
  EXPECT_TRUE(context.liberty_cell_by_master.empty());
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

TEST(FastSTATest, DmpDriverTimingMatchesOpenStaMicroCase)
{
  const auto cell = MakeOpenStaAlignmentCell();
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

TEST(FastSTATest, TimingPropagationMatchesOpenStaTwoLevelPath)
{
  auto context = MakeOpenStaAlignmentPathContext();
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

TEST(FastSTATest, TimingUsesContextRootSlewWithoutConfigMutation)
{
  const ScopedRootInputSlew root_input_slew_guard(0.777);

  auto fast_context = MakeOpenStaAlignmentPathContext();
  fast_context.root_input_slew_ns = 0.12;
  ASSERT_TRUE(icts::FastStaTiming::update(fast_context));

  auto slow_context = MakeOpenStaAlignmentPathContext();
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

TEST(FastSTATest, CharacterizationSampleRejectsMissingSourceBoundaryNet)
{
  auto context = MakeTinyContext();
  context.node_id_by_name["cts_char_source/Y"] = 3U;
  context.node_id_by_name["cts_char_sink/A"] = 3U;

  const auto sample = icts::FastStaChar::runSample(context, 0.2);

  EXPECT_FALSE(sample.valid);
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

TEST(FastSTATest, IncrementalMasterChangeMatchesFullRecompute)
{
  auto incremental_context = MakeTwoLevelContext();
  ASSERT_TRUE(icts::FastStaTiming::update(incremental_context));
  ASSERT_TRUE(icts::FastStaPower::update(incremental_context));

  auto full_context = incremental_context;

  const auto dirty_region_opt = icts::FastStaIncremental::changeBufferMasterIncremental(incremental_context, 3U, "BUF_X2");
  if (!dirty_region_opt.has_value()) {
    ADD_FAILURE() << "Expected incremental dirty region.";
    return;
  }
  const auto& dirty_region = *dirty_region_opt;
  ASSERT_TRUE(dirty_region.valid);
  EXPECT_EQ(dirty_region.start_node_id, 1U);
  EXPECT_FALSE(dirty_region.net_ids.empty());

  ASSERT_TRUE(icts::FastStaTiming::updateRegion(incremental_context, dirty_region));
  ASSERT_TRUE(icts::FastStaPower::updateRegion(incremental_context, dirty_region));

  ASSERT_TRUE(icts::FastStaIncremental::changeBufferMaster(full_context, 3U, "BUF_X2"));
  ASSERT_TRUE(icts::FastStaTiming::update(full_context));
  ASSERT_TRUE(icts::FastStaPower::update(full_context));

  ASSERT_TRUE(incremental_context.skew.valid);
  ASSERT_TRUE(full_context.skew.valid);
  EXPECT_NEAR(incremental_context.nodes.at(3U).input_cap_pf, full_context.nodes.at(3U).input_cap_pf, 1e-12);
  EXPECT_NEAR(incremental_context.nets.at(1U).load_cap_pf, full_context.nets.at(1U).load_cap_pf, 1e-12);
  EXPECT_NEAR(incremental_context.nodes.at(5U).timing.arrival_ns, full_context.nodes.at(5U).timing.arrival_ns, 1e-12);
  EXPECT_NEAR(incremental_context.nodes.at(5U).timing.slew_ns, full_context.nodes.at(5U).timing.slew_ns, 1e-12);
  EXPECT_NEAR(incremental_context.power.switching_power_w, full_context.power.switching_power_w, 1e-18);
  EXPECT_NEAR(incremental_context.power.internal_power_w, full_context.power.internal_power_w, 1e-18);
  EXPECT_NEAR(incremental_context.power.leakage_power_w, full_context.power.leakage_power_w, 1e-18);
  EXPECT_NEAR(incremental_context.power.area_um2, full_context.power.area_um2, 1e-12);
}

TEST(FastSTATest, BatchIncrementalMasterChangeAndRestoreMatchFullRecompute)
{
  auto original_context = MakeTwoLevelContext();
  ASSERT_TRUE(icts::FastStaTiming::update(original_context));
  auto incremental_context = original_context;
  auto full_context = original_context;
  const std::vector<icts::FastStaBufferMasterChange> changes{
      {.node_id = 1U, .cell_master = "BUF_X2"},
      {.node_id = 3U, .cell_master = "BUF_X2"},
  };

  ASSERT_TRUE(icts::FastStaIncremental::validateBufferMasterChanges(incremental_context, changes));
  const auto changed_region = icts::FastStaIncremental::changeBufferMastersIncremental(incremental_context, changes);
  if (!changed_region.has_value()) {
    ADD_FAILURE() << "Expected a dirty region for the validated buffer-master batch.";
    return;
  }
  ASSERT_TRUE(icts::FastStaTiming::updateRegion(incremental_context, *changed_region));
  ASSERT_TRUE(icts::FastStaIncremental::changeBufferMasters(full_context, changes));
  ASSERT_TRUE(icts::FastStaTiming::update(full_context));
  EXPECT_TRUE(TimingStatesMatch(incremental_context, full_context));

  const std::vector<icts::FastStaBufferMasterChange> restore{
      {.node_id = 1U, .cell_master = "BUF_X1"},
      {.node_id = 3U, .cell_master = "BUF_X1"},
  };
  ASSERT_TRUE(icts::FastStaIncremental::validateBufferMasterChanges(incremental_context, restore));
  const auto restored_region = icts::FastStaIncremental::changeBufferMastersIncremental(incremental_context, restore);
  if (!restored_region.has_value()) {
    ADD_FAILURE() << "Expected a dirty region when restoring the original buffer masters.";
    return;
  }
  ASSERT_TRUE(icts::FastStaTiming::updateRegion(incremental_context, *restored_region));
  EXPECT_TRUE(TimingStatesMatch(incremental_context, original_context));
}

TEST(FastSTATest, MissingBufferPairIndexesUseValidatedFallback)
{
  auto incremental_context = MakeTwoLevelContext();
  incremental_context.buffer_input_node_id_by_inst.clear();
  incremental_context.buffer_output_node_id_by_inst.clear();
  ASSERT_TRUE(icts::FastStaTiming::update(incremental_context));

  auto full_context = incremental_context;
  const std::vector<icts::FastStaBufferMasterChange> changes{
      {.node_id = 2U, .cell_master = "BUF_X2"},
      {.node_id = 3U, .cell_master = "BUF_X2"},
  };

  const auto dirty_region = icts::FastStaIncremental::changeBufferMastersIncremental(incremental_context, changes);
  if (!dirty_region.has_value()) {
    ADD_FAILURE() << "Expected missing buffer-pair indexes to use the validated fallback.";
    return;
  }
  ASSERT_TRUE(icts::FastStaTiming::updateRegion(incremental_context, *dirty_region));
  ASSERT_TRUE(icts::FastStaIncremental::changeBufferMasters(full_context, changes));
  ASSERT_TRUE(icts::FastStaTiming::update(full_context));
  EXPECT_TRUE(TimingStatesMatch(incremental_context, full_context));

  ASSERT_TRUE(icts::FastStaPower::update(incremental_context));
  ASSERT_TRUE(icts::FastStaPower::update(full_context));
  EXPECT_NEAR(incremental_context.power.total_power_w, full_context.power.total_power_w, 1e-18);
  EXPECT_NEAR(incremental_context.power.area_um2, full_context.power.area_um2, 1e-12);
}

TEST(FastSTATest, BatchPrevalidationRejectsWholeChangeWithoutMutation)
{
  auto context = MakeTwoLevelContext();
  ASSERT_TRUE(icts::FastStaTiming::update(context));
  const auto original_context = context;
  const std::vector<icts::FastStaBufferMasterChange> changes{
      {.node_id = 1U, .cell_master = "BUF_X2"},
      {.node_id = context.nodes.size(), .cell_master = "BUF_X2"},
  };

  EXPECT_FALSE(icts::FastStaIncremental::validateBufferMasterChanges(context, changes));
  EXPECT_FALSE(icts::FastStaIncremental::changeBufferMastersIncremental(context, changes).has_value());
  EXPECT_TRUE(TimingStatesMatch(context, original_context));
}

TEST(FastSTATest, BatchIncrementalTimingScale)
{
  const auto run_scale = [](std::size_t node_count, std::size_t measured_rounds) -> std::pair<double, double> {
    auto baseline_context = MakeScaleContext(node_count);
    EXPECT_EQ(baseline_context.nodes.size(), node_count);
    EXPECT_TRUE(icts::FastStaTiming::update(baseline_context));
    const auto changes = MakeScaleChanges(node_count);

    double warmup_full_us = 0.0;
    double warmup_incremental_us = 0.0;
    EXPECT_TRUE(MeasureScaleRoutes(baseline_context, changes, warmup_full_us, warmup_incremental_us));

    std::vector<double> full_samples_us;
    std::vector<double> incremental_samples_us;
    full_samples_us.reserve(measured_rounds);
    incremental_samples_us.reserve(measured_rounds);
    for (std::size_t round = 0U; round < measured_rounds; ++round) {
      double full_replay_us = 0.0;
      double incremental_replay_us = 0.0;
      EXPECT_TRUE(MeasureScaleRoutes(baseline_context, changes, full_replay_us, incremental_replay_us)) << "node_count=" << node_count << " round=" << round;
      full_samples_us.push_back(full_replay_us);
      incremental_samples_us.push_back(incremental_replay_us);
    }

    const auto full_median_us = Median(full_samples_us);
    const auto incremental_median_us = Median(incremental_samples_us);
    std::cout << "FASTSTA_SCALE node_count=" << node_count << " full_us=";
    for (const auto sample : full_samples_us) {
      std::cout << sample << ',';
    }
    std::cout << " incremental_us=";
    for (const auto sample : incremental_samples_us) {
      std::cout << sample << ',';
    }
    std::cout << " full_median_us=" << full_median_us << " incremental_median_us=" << incremental_median_us
              << " ratio=" << incremental_median_us / full_median_us << '\n';
    return {full_median_us, incremental_median_us};
  };

  const auto [full_10k_us, incremental_10k_us] = run_scale(10'000U, 5U);
  EXPECT_GT(full_10k_us, 0.0);
  EXPECT_GT(incremental_10k_us, 0.0);
  const auto [full_100k_us, incremental_100k_us] = run_scale(100'000U, 3U);
  EXPECT_GT(full_100k_us, 0.0);
  EXPECT_GT(incremental_100k_us, 0.0);
  EXPECT_LE(incremental_100k_us, full_100k_us * 0.80);
}

}  // namespace
}  // namespace icts_test
