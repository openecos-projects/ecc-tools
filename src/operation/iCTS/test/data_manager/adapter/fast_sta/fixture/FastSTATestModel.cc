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
 * @file FastSTATestModel.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-09-13
 * @brief Build synthetic clock and logic fixtures and compare complete timing states.
 */

#include "FastSTATestModel.hh"

#include <cmath>
#include <utility>

namespace icts_test {

auto MakeAxis(icts::FastStaLibertyAxisKind kind, std::vector<double> values) -> icts::FastStaLibertyAxis
{
  return icts::FastStaLibertyAxis{.kind = kind, .values = std::move(values)};
}

auto MakeTable(icts::FastStaLibertyTableKind kind, double base, icts::FastStaTransition transition) -> icts::FastStaLibertyTable
{
  return icts::FastStaLibertyTable{
      .kind = kind,
      .transition = transition,
      .axes = {MakeAxis(icts::FastStaLibertyAxisKind::kInputSlew, {0.0, 1.0}), MakeAxis(icts::FastStaLibertyAxisKind::kOutputLoad, {0.0, 2.0})},
      .values = {base, base + 0.2, base + 0.1, base + 0.3},
  };
}

auto MakeCell(const std::string& master, double input_cap_pf, double area_um2, double leakage_w) -> icts::FastStaLibertyCell
{
  auto cell = icts::FastStaLibertyCell{
      .library_name = "test_lib",
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
          .library_name = {},
          .from_port = "A",
          .to_port = "Y",
          .delay_tables = {MakeTable(icts::FastStaLibertyTableKind::kCellDelay, 0.10),
                           MakeTable(icts::FastStaLibertyTableKind::kCellDelay, 0.11, icts::FastStaTransition::kFall)},
          .slew_tables = {MakeTable(icts::FastStaLibertyTableKind::kOutputSlew, 0.20),
                          MakeTable(icts::FastStaLibertyTableKind::kOutputSlew, 0.21, icts::FastStaTransition::kFall)},
          .internal_power_tables = {MakeTable(icts::FastStaLibertyTableKind::kInternalPower, 0.50)},
      },
      .timing_arcs = {},
  };
  return cell;
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
                   icts::FastStaPiModel pi, double total_cap_pf, bool pre_reduced_pi_elmore) -> icts::FastStaNetParasitic
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
             icts::FastStaNetParasitic parasitic) -> icts::FastStaNet
{
  icts::FastStaNet net;
  net.name = std::move(name);
  net.driver_node_id = driver_node_id;
  net.load_node_ids = std::move(load_node_ids);
  net.max_cap_pf = max_cap_pf;
  net.parasitic = std::move(parasitic);
  return net;
}

auto MakeTinyContext() -> icts::FastStaContext
{
  icts::FastStaContext context;
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

auto MakeClockAndLogicContext() -> icts::FastStaContext
{
  auto context = MakeTinyContext();
  auto& cell = context.liberty_cell_by_master.at("BUF_X1");
  cell.timing_arcs.push_back(cell.timing_arc);
  const auto logic_source_id = context.nodes.size();
  context.nodes.push_back(MakeNode(icts::FastStaNodeKind::kSource, "data_src", "", "data_src", "", {}, 0.0, icts::kInvalidFastStaNetId, {}));
  context.nodes.back().domain = icts::FastStaNodeDomain::kLogic;
  context.nodes.back().arrival_seed_early_ns = 0.0;
  context.nodes.back().arrival_seed_late_ns = 0.0;
  context.nodes.back().slew_seed_early_ns = 0.2;
  context.nodes.back().slew_seed_late_ns = 0.2;
  const auto logic_sink_id = context.nodes.size();
  context.nodes.push_back(MakeNode(icts::FastStaNodeKind::kSink, "data_sink", "", "data_sink", "BUF_X1", {}, 0.1, icts::kInvalidFastStaNetId, {}));
  context.nodes.back().domain = icts::FastStaNodeDomain::kLogic;
  context.nodes.back().max_slew_ns = 1.0;
  context.node_id_by_name.emplace("data_src", logic_source_id);
  context.node_id_by_name.emplace("data_sink", logic_sink_id);
  context.timing_arcs.push_back(icts::FastStaTimingArc{.from_node_id = logic_source_id,
                                                       .to_node_id = logic_sink_id,
                                                       .cell_master = "BUF_X1",
                                                       .from_port = "A",
                                                       .to_port = "Y",
                                                       .positive_unate = true,
                                                       .negative_unate = false,
                                                       .clock_gate_boundary = false});
  auto launch_arc = context.liberty_cell_by_master.at("BUF_X1").timing_arc;
  launch_arc.from_port = "CLK";
  launch_arc.to_port = "Q";
  launch_arc.edge_triggered = true;
  launch_arc.trigger_transition = icts::FastStaTransition::kRise;
  context.liberty_cell_by_master.at("BUF_X1").timing_arcs.push_back(std::move(launch_arc));
  context.timing_launches.push_back(icts::FastStaTimingLaunch{.output_node_id = logic_source_id,
                                                              .clock_node_id = 3U,
                                                              .cell_master = "BUF_X1",
                                                              .clock_port = "CLK",
                                                              .output_port = "Q",
                                                              .clock_transition = icts::FastStaTransition::kRise});
  context.timing_checks.push_back(icts::FastStaTimingCheck{.data_node_id = logic_sink_id,
                                                           .clock_node_id = 3U,
                                                           .cell_master = "BUF_X1",
                                                           .clock_port = "CLK",
                                                           .data_port = "D",
                                                           .kind = icts::FastStaTimingCheckKind::kSetup,
                                                           .clock_transition = icts::FastStaTransition::kRise,
                                                           .requirement_override_ns = 0.2});
  context.timing_checks.push_back(icts::FastStaTimingCheck{.data_node_id = logic_sink_id,
                                                           .clock_node_id = 3U,
                                                           .cell_master = "BUF_X1",
                                                           .clock_port = "CLK",
                                                           .data_port = "D",
                                                           .kind = icts::FastStaTimingCheckKind::kHold,
                                                           .clock_transition = icts::FastStaTransition::kRise,
                                                           .requirement_override_ns = 0.05});
  return context;
}

auto MakeTwoLevelContext() -> icts::FastStaContext
{
  icts::FastStaContext context;
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

auto TimingStatesMatch(const icts::FastStaContext& lhs, const icts::FastStaContext& rhs) -> bool
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
    for (const auto early : {true, false}) {
      const auto& lhs_timing = early ? lhs_node.early_timing : lhs_node.late_timing;
      const auto& rhs_timing = early ? rhs_node.early_timing : rhs_node.late_timing;
      for (std::size_t transition = 0U; transition < 2U; ++transition) {
        const auto& lhs_point = lhs_timing.at(transition);
        const auto& rhs_point = rhs_timing.at(transition);
        if (lhs_point.valid != rhs_point.valid || std::abs(lhs_point.arrival_ns - rhs_point.arrival_ns) > 1e-12
            || std::abs(lhs_point.slew_ns - rhs_point.slew_ns) > 1e-12) {
          return false;
        }
      }
    }
  }
  return true;
}

}  // namespace icts_test
