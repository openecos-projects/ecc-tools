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
 * @file FastSTABranch.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-09-05
 * @brief Identity-checked local branch construction; no independent timing model.
 */
#include "FastSTABranch.hh"

#include <algorithm>
#include <chrono>
#include <unordered_set>
#include <utility>

#include "clock_net_parasitic/FastSTAParasitics.hh"
#include "clock_state/FastSTAClockState.hh"
#include "liberty/FastSTALiberty.hh"
#include "timing/FastSTATiming.hh"

namespace icts {

auto FastStaBranch::evaluate(const FastStaContext& resident, const FastStaBranchRequest& request) -> FastStaBranchResult
{
  const auto start = std::chrono::steady_clock::now();
  FastStaBranchResult result{.component_id = request.component_id, .candidate_label = request.candidate_label, .loads = {}};
  const auto fail = [&](const std::string& diagnostic) -> FastStaBranchResult {
    result.diagnostic = diagnostic;
    result.runtime_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    return result;
  };
  if (request.component_id.empty() || request.loads.empty() || request.representative_pin_name.empty() || request.driver_cell_master.empty()
      || request.driver_input_port.empty() || request.driver_output_port.empty()) {
    return fail("branch_identity_or_model_missing");
  }
  std::optional<FastStaLibertyCell> driver;
  if (const auto found = resident.liberty_cell_by_master.find(request.driver_cell_master); found != resident.liberty_cell_by_master.end()) {
    driver = found->second;
  } else if (resident.wrapper != nullptr) {
    driver = FastStaLiberty::extractCellArc(*resident.wrapper, request.driver_cell_master, request.driver_input_port, request.driver_output_port);
  }
  if (!driver.has_value()) {
    return fail("branch_driver_model_unavailable:" + request.driver_cell_master);
  }
  driver->input_port = request.driver_input_port;
  driver->output_port = request.driver_output_port;
  // Re-query the requested input when a multi-input master was already indexed
  // through a different arc; the representative load never supplies driver cap.
  if (resident.wrapper != nullptr) {
    if (auto exact = FastStaLiberty::extractCellArc(*resident.wrapper, request.driver_cell_master, request.driver_input_port, request.driver_output_port)) {
      driver = std::move(exact);
    }
  }
  const auto conditional
      = driver->clock_gate.has_value() || std::ranges::any_of(driver->timing_arcs, [](const auto& arc) -> bool { return !arc.when.empty(); });
  if (conditional && (request.driver_inst_name.empty() || !resident.node_id_by_name.contains(request.driver_inst_name + "/" + request.driver_input_port))) {
    return fail("branch_control_instance_unresolved:" + request.driver_inst_name);
  }
  FastStaContext branch;
  branch.wrapper = resident.wrapper;
  branch.clock_name = resident.clock_name;
  branch.clock_period_ns = resident.clock_period_ns;
  branch.dbu_per_um = resident.dbu_per_um;
  branch.routing_layer = resident.routing_layer;
  branch.wire_width_um = resident.wire_width_um;
  branch.worker_count = 1U;
  branch.source_node_id = 0U;
  branch.constraints = resident.constraints;
  branch.case_values = resident.case_values;
  branch.propagate_all_clocks = true;
  const auto driver_identity = request.driver_inst_name.empty() ? request.component_id + "/driver" : request.driver_inst_name;
  branch.nodes.push_back(FastStaNode{.kind = FastStaNodeKind::kBufferInput,
                                     .name = driver_identity + "/" + request.driver_input_port,
                                     .inst_name = driver_identity,
                                     .cell_master = request.driver_cell_master,
                                     .location = request.driver_location,
                                     .input_cap_pf = driver->input_cap_pf,
                                     .input_cap_pf_by_timing = driver->input_cap_pf_by_timing,
                                     .output_net_ids = {},
                                     .input_cap_profile_available = driver->input_cap_profile_available});
  branch.nodes.push_back(FastStaNode{.kind = FastStaNodeKind::kBufferOutput,
                                     .name = driver_identity + "/" + request.driver_output_port,
                                     .inst_name = driver_identity,
                                     .cell_master = request.driver_cell_master,
                                     .location = request.driver_location,
                                     .output_net_ids = {0U},
                                     .clock_from_port = request.driver_input_port,
                                     .clock_to_port = request.driver_output_port});
  branch.buffer_input_node_id_by_inst.emplace(driver_identity, 0U);
  branch.buffer_output_node_id_by_inst.emplace(driver_identity, 1U);
  branch.liberty_cell_by_master.emplace(request.driver_cell_master, *driver);
  FastStaNet net{.name = request.component_id + "/branch", .driver_node_id = 1U, .load_node_ids = {}, .load_rc_node_ids = {}, .driver_timing_by_state = {}};
  std::unordered_set<std::string> identities;
  bool representative_found = false;
  for (const auto& load : request.loads) {
    const auto found = resident.node_id_by_name.find(load.pin_name);
    if (found == resident.node_id_by_name.end() || found->second >= resident.nodes.size()) {
      return fail("branch_load_identity_unresolved:" + load.pin_name);
    }
    if (!identities.insert(load.pin_name).second) {
      return fail("branch_load_identity_duplicated:" + load.pin_name);
    }
    representative_found = representative_found || load.pin_name == request.representative_pin_name;
    auto node = resident.nodes.at(found->second);
    node.kind = FastStaNodeKind::kSink;
    node.domain = FastStaNodeDomain::kClock;
    node.location = load.location;
    node.incoming_net_id = 0U;
    node.output_net_ids.clear();
    node.clock_inactive = false;
    net.load_node_ids.push_back(branch.nodes.size());
    branch.node_id_by_name.emplace(node.name, branch.nodes.size());
    if (const auto model = resident.liberty_cell_by_master.find(node.cell_master); model != resident.liberty_cell_by_master.end()) {
      branch.liberty_cell_by_master.try_emplace(node.cell_master, model->second);
    }
    branch.nodes.push_back(std::move(node));
  }
  if (!representative_found) {
    return fail("branch_representative_load_missing");
  }
  branch.nets.push_back(std::move(net));
  if (!FastStaParasitics::buildNetParasiticFromSegments(branch, 0U, request.rc_segments)) {
    return fail("branch_rc_topology_invalid");
  }
  if (!FastStaTiming::updateBranch(branch, 0U, request.input_states)) {
    return fail("branch_four_state_timing_unavailable");
  }
  result.upstream_input_cap_pf = driver->input_cap_pf_by_timing;
  result.rc_node_count = branch.nets.front().parasitic.rc_nodes.size();
  result.rc_edge_count = branch.nets.front().parasitic.rc_edges.size();
  for (const auto load_id : branch.nets.front().load_node_ids) {
    const auto& node = branch.nodes.at(load_id);
    FastStaBranchLoadResult load{.pin_name = node.name};
    for (std::size_t analysis = 0U; analysis < 2U; ++analysis) {
      for (std::size_t transition = 0U; transition < 2U; ++transition) {
        const auto& state = analysis == 0U ? node.early_timing.at(transition) : node.late_timing.at(transition);
        if (!state.valid && !node.clock_inactive) {
          return fail("branch_load_state_unavailable:" + node.name);
        }
        load.clock_inactive = node.clock_inactive;
        load.states.at(analysis).at(transition)
            = {.valid = state.valid, .arrival_ns = state.arrival_ns, .slew_ns = state.slew_ns, .source_transition = state.launch_clock_transition};
      }
    }
    result.loads.push_back(std::move(load));
  }
  result.complete = true;
  result.runtime_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  return result;
}

}  // namespace icts
