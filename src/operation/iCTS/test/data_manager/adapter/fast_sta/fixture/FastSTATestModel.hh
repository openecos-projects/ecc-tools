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
 * @file FastSTATestModel.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-09-13
 * @brief Synthetic timing models shared by numerical and incremental regression tests.
 */

#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "FastSTA.hh"
#include "clock_state/FastSTAClockState.hh"
#include "liberty/FastSTALibertyModel.hh"

namespace icts_test {

auto MakeAxis(icts::FastStaLibertyAxisKind kind, std::vector<double> values) -> icts::FastStaLibertyAxis;

auto MakeTable(icts::FastStaLibertyTableKind kind, double base, icts::FastStaTransition transition = icts::FastStaTransition::kRise)
    -> icts::FastStaLibertyTable;

auto MakeCell(const std::string& master, double input_cap_pf, double area_um2, double leakage_w) -> icts::FastStaLibertyCell;

auto MakeNode(icts::FastStaNodeKind kind, std::string name, std::string inst_name, std::string pin_name, std::string cell_master, icts::FastStaPoint location,
              double input_cap_pf, icts::FastStaNetId incoming_net_id, std::vector<icts::FastStaNetId> output_net_ids) -> icts::FastStaNode;

auto MakeRcNode(std::string name, double wire_cap_pf, double pin_cap_pf, double cap_pf, double elmore_delay_ns, icts::FastStaNodeId terminal_node_id)
    -> icts::FastStaRcNode;

auto MakeParasitic(std::vector<icts::FastStaRcNode> rc_nodes, std::vector<icts::FastStaRcEdge> rc_edges, icts::FastStaRcNodeId root_rc_node_id,
                   icts::FastStaPiModel pi = {}, double total_cap_pf = 0.0, bool pre_reduced_pi_elmore = false) -> icts::FastStaNetParasitic;

auto MakeNet(std::string name, icts::FastStaNodeId driver_node_id, std::vector<icts::FastStaNodeId> load_node_ids, double max_cap_pf,
             icts::FastStaNetParasitic parasitic = {}) -> icts::FastStaNet;

auto MakeTinyContext() -> icts::FastStaContext;

auto MakeClockAndLogicContext() -> icts::FastStaContext;

auto MakeTwoLevelContext() -> icts::FastStaContext;

auto TimingStatesMatch(const icts::FastStaContext& lhs, const icts::FastStaContext& rhs) -> bool;

}  // namespace icts_test
