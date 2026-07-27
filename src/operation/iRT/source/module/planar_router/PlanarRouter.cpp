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
#include "PlanarRouter.hpp"

#include <chrono>
#include <cmath>

#include "GDSPlotter.hpp"
#include "RTInterface.hpp"
#include "TBTask.hpp"
#include "TOPOBuilder.hpp"
#include "PRCandidate.hpp"
#include "Utility.hpp"

namespace irt {

namespace {

std::vector<PlanarRect> getPlanarObsList(const GridMap<bool>& forbidden_map)
{
  std::vector<PlanarRect> planar_obs_list;
  std::map<std::pair<int32_t, int32_t>, int32_t> active_interval_rect_idx_map;

  for (int32_t y = 0; y < forbidden_map.get_y_size(); y++) {
    std::map<std::pair<int32_t, int32_t>, int32_t> curr_interval_rect_idx_map;
    for (int32_t x = 0; x < forbidden_map.get_x_size();) {
      if (!forbidden_map[x][y]) {
        x++;
        continue;
      }
      int32_t ll_x = x;
      while (x + 1 < forbidden_map.get_x_size() && forbidden_map[x + 1][y]) {
        x++;
      }
      int32_t ur_x = x;
      std::pair<int32_t, int32_t> interval(ll_x, ur_x);
      auto active_iter = active_interval_rect_idx_map.find(interval);
      if (active_iter != active_interval_rect_idx_map.end()) {
        planar_obs_list[active_iter->second].set_ur_y(y);
        curr_interval_rect_idx_map[interval] = active_iter->second;
      } else {
        planar_obs_list.emplace_back(ll_x, y, ur_x, y);
        curr_interval_rect_idx_map[interval] = static_cast<int32_t>(planar_obs_list.size()) - 1;
      }
      x++;
    }
    active_interval_rect_idx_map = std::move(curr_interval_rect_idx_map);
  }
  return planar_obs_list;
}

}  // namespace

// public

void PlanarRouter::initInst()
{
  if (_pr_instance == nullptr) {
    _pr_instance = new PlanarRouter();
  }
}

PlanarRouter& PlanarRouter::getInst()
{
  if (_pr_instance == nullptr) {
    RTLOG.error(Loc::current(), "The instance not initialized!");
  }
  return *_pr_instance;
}

void PlanarRouter::destroyInst()
{
  if (_pr_instance != nullptr) {
    delete _pr_instance;
    _pr_instance = nullptr;
  }
}

// function

void PlanarRouter::generate()
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");
  PRModel pr_model = initPRModel();
  setPRComParam(pr_model);
  initPRTaskList(pr_model);
  buildPRNodeMap(pr_model);
  buildPRNodeNeighbor(pr_model);
  buildOrientSupply(pr_model);
  if (pr_model.get_enable_astar_fallback()) {
    buildPRMacroRegion(pr_model);
  }
  // debugCheckPRModel(pr_model);
  generatePRModel(pr_model);
  PRMacroRepairStat& macro_repair_stat = pr_model.get_pr_macro_repair_stat();
  if (!pr_model.get_pr_macro_region_list().empty()) {
    RTLOG.info(Loc::current(), "macro_region_num: ", pr_model.get_pr_macro_region_list().size(),
               ", raw_steiner_in_macro: ", macro_repair_stat.raw_steiner_in_macro,
               ", fixed_steiner_in_macro: ", macro_repair_stat.fixed_steiner_in_macro,
               ", failed_steiner_legalize_num: ", macro_repair_stat.failed_steiner_legalize_num,
               ", filtered_macro_cross_candidate_num: ", macro_repair_stat.filtered_macro_cross_candidate_num,
               ", astar_fallback_attempt_num: ", macro_repair_stat.astar_fallback_attempt_num,
               ", astar_fallback_success_num: ", macro_repair_stat.astar_fallback_success_num,
               ", astar_fallback_failed_num: ", macro_repair_stat.astar_fallback_failed_num,
               ", astar_search_num: ", macro_repair_stat.astar_search_num,
               ", astar_escape_pair_num: ", macro_repair_stat.astar_escape_pair_num,
               ", astar_pruned_pair_num: ", macro_repair_stat.astar_pruned_pair_num,
               ", astar_max_workspace_cell_num: ", macro_repair_stat.astar_max_workspace_cell_num,
               ", astar_expanded_node_num: ", macro_repair_stat.astar_expanded_node_num,
               ", astar_push_node_num: ", macro_repair_stat.astar_push_node_num,
               ", astar_stale_pop_num: ", macro_repair_stat.astar_stale_pop_num,
               ", astar_cost_cache_hit_num: ", macro_repair_stat.astar_cost_cache_hit_num,
               ", astar_cost_cache_miss_num: ", macro_repair_stat.astar_cost_cache_miss_num,
               ", astar_prepare_time_ms: ", macro_repair_stat.astar_prepare_time_ms,
               ", astar_search_time_ms: ", macro_repair_stat.astar_search_time_ms,
               ", astar_validate_time_ms: ", macro_repair_stat.astar_validate_time_ms,
               ", failed_routing_edge_num: ", macro_repair_stat.failed_routing_edge_num,
               ", failed_routing_net_num: ", macro_repair_stat.failed_routing_net_set.size(),
               ", pattern_astar_macro_cross_edge_num: ", macro_repair_stat.pattern_astar_macro_cross_edge_num,
               ", pattern_astar_macro_cross_net_num: ", macro_repair_stat.pattern_astar_macro_cross_net_set.size());
  }
  // debugPlotPRModel(pr_model, "after");
  updateSummary(pr_model);
  printSummary(pr_model);
  outputGuide(pr_model);
  outputNetCSV(pr_model);
  outputOverflowCSV(pr_model);
  outputJson(pr_model);
  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

// private

PlanarRouter* PlanarRouter::_pr_instance = nullptr;

PRModel PlanarRouter::initPRModel()
{
  std::vector<Net>& net_list = RTDM.getDatabase().get_net_list();
  std::vector<Macro>& macro_list = RTDM.getDatabase().get_macro_list();

  PRModel pr_model;
  pr_model.set_pr_net_list(convertToPRNetList(net_list));
  pr_model.set_enable_astar_fallback(!macro_list.empty());
  RTLOG.info(Loc::current(), "enable_astar_fallback: ", pr_model.get_enable_astar_fallback(),
             ", macro_num: ", macro_list.size());
  return pr_model;
}

std::vector<PRNet> PlanarRouter::convertToPRNetList(std::vector<Net>& net_list)
{
  std::vector<PRNet> pr_net_list;
  pr_net_list.reserve(net_list.size());
  for (size_t i = 0; i < net_list.size(); i++) {
    pr_net_list.emplace_back(convertToPRNet(net_list[i]));
  }
  return pr_net_list;
}

PRNet PlanarRouter::convertToPRNet(Net& net)
{
  PRNet pr_net;
  pr_net.set_origin_net(&net);
  pr_net.set_net_idx(net.get_net_idx());
  pr_net.set_connect_type(net.get_connect_type());
  for (Pin& pin : net.get_pin_list()) {
    pr_net.get_pr_pin_list().push_back(PRPin(pin));
  }
  pr_net.set_bounding_box(net.get_bounding_box());
  return pr_net;
}

void PlanarRouter::setPRComParam(PRModel& pr_model)
{
  int32_t topo_spilt_length = 30;
  int32_t expand_step_num = 30;
  int32_t expand_step_length = 1;
  double prefer_wire_unit = 1;
  double non_prefer_wire_unit = 2.5 * prefer_wire_unit;
  double overflow_unit = 4 * non_prefer_wire_unit;
  /**
   * topo_spilt_length, expand_step_num, expand_step_length, overflow_unit
   */
  double corner_weight = 0.3;

  PRComParam pr_com_param(topo_spilt_length, expand_step_num, expand_step_length, overflow_unit, corner_weight);
  RTLOG.info(Loc::current(), "topo_spilt_length: ", pr_com_param.get_topo_spilt_length());
  RTLOG.info(Loc::current(), "expand_step_num: ", pr_com_param.get_expand_step_num());
  RTLOG.info(Loc::current(), "expand_step_length: ", pr_com_param.get_expand_step_length());
  RTLOG.info(Loc::current(), "overflow_unit: ", pr_com_param.get_overflow_unit());
  RTLOG.info(Loc::current(), "corner_weight: ", pr_com_param.get_corner_weight());
  RTLOG.info(Loc::current(), "cost_mode: fast_cached");
  RTLOG.info(Loc::current(), "shadow_mode: legacy_sparse");
  RTLOG.info(Loc::current(), "long_oblique_candidate_mode: exhaustive_inner_3_bends");
  pr_model.set_pr_com_param(pr_com_param);
}

void PlanarRouter::initPRTaskList(PRModel& pr_model)
{
  std::vector<PRNet>& pr_net_list = pr_model.get_pr_net_list();
  std::vector<PRNet*>& pr_task_list = pr_model.get_pr_task_list();
  pr_task_list.reserve(pr_net_list.size());
  for (PRNet& pr_net : pr_net_list) {
    pr_task_list.push_back(&pr_net);
  }
  std::sort(pr_task_list.begin(), pr_task_list.end(), CmpPRNet());
}

void PlanarRouter::buildPRNodeMap(PRModel& pr_model)
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  GridMap<GCell>& gcell_map = RTDM.getDatabase().get_gcell_map();
  GridMap<PRNode>& pr_node_map = pr_model.get_pr_node_map();
  pr_node_map.init(gcell_map.get_x_size(), gcell_map.get_y_size());
#pragma omp parallel for collapse(2)
  for (int32_t x = 0; x < gcell_map.get_x_size(); x++) {
    for (int32_t y = 0; y < gcell_map.get_y_size(); y++) {
      PRNode& pr_node = pr_node_map[x][y];
      pr_node.set_coord(x, y);
      pr_node.set_boundary_wire_unit(gcell_map[x][y].get_boundary_wire_unit());
      pr_node.set_internal_wire_unit(gcell_map[x][y].get_internal_wire_unit());
      pr_node.set_internal_via_unit(gcell_map[x][y].get_internal_via_unit());
      for (auto& [routing_layer_idx, ignore_net_orient_map] : gcell_map[x][y].get_routing_ignore_net_orient_map()) {
        for (auto& [net_idx, orient_set] : ignore_net_orient_map) {
          pr_node.get_ignore_net_orient_map()[net_idx].insert(orient_set.begin(), orient_set.end());
        }
      }
      std::map<Orientation, std::set<int32_t>> planar_orient_allowed_net_map;
      std::set<Orientation> unrestricted_orient_set;
      for (auto& [layer_idx, orient_supply_map] : gcell_map[x][y].get_routing_orient_supply_map()) {
        for (auto& [orient, supply] : orient_supply_map) {
          if (supply <= 0 || RTUTIL.exist(unrestricted_orient_set, orient)) {
            continue;
          }
          RoutingLayerAllowedNetMap& routing_allowed_net_map = gcell_map[x][y].get_routing_allowed_net_map();
          if (!RTUTIL.exist(routing_allowed_net_map, layer_idx) || !RTUTIL.exist(routing_allowed_net_map[layer_idx], orient)) {
            planar_orient_allowed_net_map.erase(orient);
            unrestricted_orient_set.insert(orient);
          } else {
            planar_orient_allowed_net_map[orient].insert(routing_allowed_net_map[layer_idx][orient].begin(),
                                                         routing_allowed_net_map[layer_idx][orient].end());
          }
        }
      }
      pr_node.set_orient_allowed_net_map(planar_orient_allowed_net_map);
    }
  }
  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void PlanarRouter::buildPRNodeNeighbor(PRModel& pr_model)
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  GridMap<GCell>& gcell_map = RTDM.getDatabase().get_gcell_map();

  GridMap<PRNode>& pr_node_map = pr_model.get_pr_node_map();
#pragma omp parallel for collapse(2)
  for (int32_t x = 0; x < gcell_map.get_x_size(); x++) {
    for (int32_t y = 0; y < gcell_map.get_y_size(); y++) {
      std::map<Orientation, PRNode*>& neighbor_node_map = pr_node_map[x][y].get_neighbor_node_map();
      if (x != 0) {
        neighbor_node_map[Orientation::kWest] = &pr_node_map[x - 1][y];
      }
      if (x != (pr_node_map.get_x_size() - 1)) {
        neighbor_node_map[Orientation::kEast] = &pr_node_map[x + 1][y];
      }
      if (y != 0) {
        neighbor_node_map[Orientation::kSouth] = &pr_node_map[x][y - 1];
      }
      if (y != (pr_node_map.get_y_size() - 1)) {
        neighbor_node_map[Orientation::kNorth] = &pr_node_map[x][y + 1];
      }
    }
  }
  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void PlanarRouter::buildOrientSupply(PRModel& pr_model)
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  GridMap<GCell>& gcell_map = RTDM.getDatabase().get_gcell_map();
  GridMap<PRNode>& pr_node_map = pr_model.get_pr_node_map();

#pragma omp parallel for collapse(2)
  for (int32_t x = 0; x < gcell_map.get_x_size(); x++) {
    for (int32_t y = 0; y < gcell_map.get_y_size(); y++) {
      std::map<Orientation, int32_t> planar_orient_supply_map;
      for (auto& [layer_idx, orient_supply_map] : gcell_map[x][y].get_routing_orient_supply_map()) {
        for (auto& [orient, supply] : orient_supply_map) {
          planar_orient_supply_map[orient] += supply;
        }
      }
      pr_node_map[x][y].set_orient_supply_map(planar_orient_supply_map);
    }
  }

  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void PlanarRouter::buildPRMacroRegion(PRModel& pr_model)
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  ScaleAxis& gcell_axis = RTDM.getDatabase().get_gcell_axis();
  GridMap<GCell>& gcell_map = RTDM.getDatabase().get_gcell_map();
  std::vector<Macro>& macro_list = RTDM.getDatabase().get_macro_list();

  std::vector<PRMacroRegion>& pr_macro_region_list = pr_model.get_pr_macro_region_list();
  GridMap<bool>& macro_body_forbidden_map = pr_model.get_macro_body_forbidden_map();
  macro_body_forbidden_map.init(gcell_map.get_x_size(), gcell_map.get_y_size(), false);
  pr_macro_region_list.clear();
  pr_macro_region_list.reserve(macro_list.size());

  int32_t forbidden_gcell_num = 0;
  for (Macro& macro : macro_list) {
    PlanarRect& body_rect = macro.get_body_rect();

    PRMacroRegion pr_macro_region;
    pr_macro_region.inst_name = macro.get_inst_name();
    pr_macro_region.body_grid_rect = RTUTIL.getClosedGCellGridRect(body_rect, gcell_axis);
    pr_macro_region_list.push_back(pr_macro_region);

    PlanarRect& body_grid_rect = pr_macro_region_list.back().body_grid_rect;
    for (int32_t x = body_grid_rect.get_ll_x(); x <= body_grid_rect.get_ur_x(); x++) {
      for (int32_t y = body_grid_rect.get_ll_y(); y <= body_grid_rect.get_ur_y(); y++) {
        if (!macro_body_forbidden_map.isInside(x, y) || !RTUTIL.isOpenOverlap(gcell_map[x][y], body_rect)) {
          continue;
        }
        if (!macro_body_forbidden_map[x][y]) {
          forbidden_gcell_num++;
        }
        macro_body_forbidden_map[x][y] = true;
      }
    }
  }
  pr_model.set_macro_body_obs_list(getPlanarObsList(macro_body_forbidden_map));

  RTLOG.info(Loc::current(), "macro_region_num: ", pr_macro_region_list.size(), ", macro_body_forbidden_gcell_num: ", forbidden_gcell_num);
  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void PlanarRouter::generatePRModel(PRModel& pr_model)
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  std::vector<PRNet*>& pr_task_list = pr_model.get_pr_task_list();

  int32_t batch_size = RTUTIL.getBatchSize(pr_task_list.size());

  Monitor stage_monitor;
  for (size_t i = 0; i < pr_task_list.size(); i++) {
    routePRTask(pr_model, pr_task_list[i]);
    if ((i + 1) % batch_size == 0 || (i + 1) == pr_task_list.size()) {
      RTLOG.info(Loc::current(), "Routed ", (i + 1), "/", pr_task_list.size(), "(", RTUTIL.getPercentage(i + 1, pr_task_list.size()), ") nets",
                 stage_monitor.getStatsInfo());
    }
  }

  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
  PRMacroRepairStat& macro_repair_stat = pr_model.get_pr_macro_repair_stat();
  if (!macro_repair_stat.failed_routing_net_set.empty()) {
    RTLOG.error(Loc::current(), "PR stopped because no complete routing topology was found for ",
                macro_repair_stat.failed_routing_net_set.size(), " nets, edge_num: ", macro_repair_stat.failed_routing_edge_num);
  }
}

void PlanarRouter::routePRTask(PRModel& pr_model, PRNet* pr_task)
{
  initSingleTask(pr_model, pr_task);
  std::vector<Segment<PlanarCoord>> routing_segment_list = getRoutingSegmentList(pr_model);
  std::set<int32_t>& failed_routing_net_set = pr_model.get_pr_macro_repair_stat().failed_routing_net_set;
  if (failed_routing_net_set.find(pr_task->get_net_idx()) != failed_routing_net_set.end()) {
    resetSingleTask(pr_model);
    return;
  }
  MTree<PlanarCoord> coord_tree = getCoordTree(pr_model, routing_segment_list);
  updateDemandToGraph(pr_model, ChangeType::kAdd, coord_tree);
  uploadNetResult(pr_model, coord_tree);
  resetSingleTask(pr_model);
}

void PlanarRouter::initSingleTask(PRModel& pr_model, PRNet* pr_task)
{
  pr_model.set_curr_pr_task(pr_task);
}

std::vector<Segment<PlanarCoord>> PlanarRouter::getRoutingSegmentList(PRModel& pr_model)
{
  std::vector<Segment<PlanarCoord>> planar_topo_list = getPlanarTopoList(pr_model);
  std::set<PlanarCoord, CmpPlanarCoordByXASC> terminal_coord_set = getCurrTerminalCoordSet(pr_model);

  PRShadowDemandMap self_shadow;
  std::vector<Segment<PlanarCoord>> routing_segment_list;

  for (size_t topo_idx = 0; topo_idx < planar_topo_list.size(); topo_idx++) {
    const PRShadowDemandMap* shadow_ptr = self_shadow.empty() ? nullptr : &self_shadow;
    PRMacroRepairStat& macro_repair_stat = pr_model.get_pr_macro_repair_stat();
    int32_t filtered_macro_cross_candidate_num = macro_repair_stat.filtered_macro_cross_candidate_num;
    std::vector<PRCandidate> candidate_list
        = getPRCandidateListByTopo(pr_model, static_cast<int32_t>(topo_idx), planar_topo_list[topo_idx], terminal_coord_set, shadow_ptr);
    bool has_pattern_macro_cross_candidate = (macro_repair_stat.filtered_macro_cross_candidate_num > filtered_macro_cross_candidate_num);

#pragma omp parallel for
    for (int32_t candidate_idx = 0; candidate_idx < static_cast<int32_t>(candidate_list.size()); candidate_idx++) {
      updatePRCandidate(pr_model, candidate_list[candidate_idx], shadow_ptr);
    }

    bool has_unblocked_candidate = false;
    for (PRCandidate& pr_candidate : candidate_list) {
      if (!pr_candidate.get_is_path_blocked()) {
        has_unblocked_candidate = true;
        break;
      }
    }
    bool pattern_route_failed = !has_unblocked_candidate;
    if (pr_model.get_enable_astar_fallback() && pattern_route_failed) {
      macro_repair_stat.astar_fallback_attempt_num++;
      std::vector<Segment<PlanarCoord>> astar_segment_list
          = getRoutingSegmentListByAStarWithEscape(pr_model, planar_topo_list[topo_idx], terminal_coord_set, shadow_ptr);
      bool astar_macro_cross = (!astar_segment_list.empty() && isMacroBlockedRoutingSegmentList(pr_model, astar_segment_list, terminal_coord_set));
      if (!astar_segment_list.empty() && !astar_macro_cross) {
        candidate_list.emplace_back(static_cast<int32_t>(topo_idx), astar_segment_list, 0, 0, false, 0);
        updatePRCandidate(pr_model, candidate_list.back(), shadow_ptr);
        macro_repair_stat.astar_fallback_success_num++;
      } else {
        macro_repair_stat.astar_fallback_failed_num++;
        if (candidate_list.empty() && has_pattern_macro_cross_candidate && astar_macro_cross) {
          PRNet* curr_pr_task = pr_model.get_curr_pr_task();
          int32_t curr_net_idx = curr_pr_task->get_net_idx();
          macro_repair_stat.pattern_astar_macro_cross_edge_num++;
          if (macro_repair_stat.pattern_astar_macro_cross_net_set.insert(curr_net_idx).second) {
            std::string net_name = curr_pr_task->get_origin_net() == nullptr ? "" : curr_pr_task->get_origin_net()->get_net_name();
            PlanarCoord& first_coord = planar_topo_list[topo_idx].get_first();
            PlanarCoord& second_coord = planar_topo_list[topo_idx].get_second();
            RTLOG.warn(Loc::current(), "Pattern routing and A* both cross macro, net_idx: ", curr_net_idx,
                       ", net_name: ", net_name, ", topo_idx: ", topo_idx, ", topo_edge: (", first_coord.get_x(), ",",
                       first_coord.get_y(), ")-(", second_coord.get_x(), ",", second_coord.get_y(), ")");
          }
        }
      }
    }

    PRCandidate* best_candidate = nullptr;
    for (PRCandidate& pr_candidate : candidate_list) {
      if (best_candidate == nullptr || isBetterCandidate(pr_model, pr_candidate, *best_candidate)) {
        best_candidate = &pr_candidate;
      }
    }
    if (best_candidate == nullptr) {
      PRNet* curr_pr_task = pr_model.get_curr_pr_task();
      int32_t curr_net_idx = curr_pr_task->get_net_idx();
      macro_repair_stat.failed_routing_edge_num++;
      macro_repair_stat.failed_routing_net_set.insert(curr_net_idx);
      std::string net_name = curr_pr_task->get_origin_net() == nullptr ? "" : curr_pr_task->get_origin_net()->get_net_name();
      PlanarCoord& first_coord = planar_topo_list[topo_idx].get_first();
      PlanarCoord& second_coord = planar_topo_list[topo_idx].get_second();
      RTLOG.warn(Loc::current(), "No routing candidate, net_idx: ", curr_net_idx, ", net_name: ", net_name,
                 ", topo_idx: ", topo_idx, ", topo_edge: (", first_coord.get_x(), ",", first_coord.get_y(), ")-(",
                 second_coord.get_x(), ",", second_coord.get_y(), "), astar_fallback: ",
                 pr_model.get_enable_astar_fallback() ? "failed" : "disabled");
      continue;
    }
    for (Segment<PlanarCoord>& routing_segment : best_candidate->get_routing_segment_list()) {
      routing_segment_list.push_back(routing_segment);
    }
    addCandidateToShadow(self_shadow, *best_candidate);
  }
  return routing_segment_list;
}

bool PlanarRouter::isBetterCandidate(PRModel& pr_model, PRCandidate& candidate, PRCandidate& current_best)
{
  double corner_weight = pr_model.get_pr_com_param().get_corner_weight();
  auto computeScore = [&](PRCandidate& c) {
    return c.get_total_wire_length() + c.get_total_cost() + corner_weight * c.get_total_corner_num();
  };

  bool a_blocked = candidate.get_is_path_blocked();
  bool b_blocked = current_best.get_is_path_blocked();
  if (!a_blocked && b_blocked) {
    return true;
  } else if (a_blocked && !b_blocked) {
    return false;
  }
  double score_a = computeScore(candidate);
  double score_b = computeScore(current_best);
  if (std::abs(score_a - score_b) < 1e-9) {
    if (candidate.get_saturation_node_num() != current_best.get_saturation_node_num()) {
      return candidate.get_saturation_node_num() < current_best.get_saturation_node_num();
    }
    if (candidate.get_hotspot_node_num() != current_best.get_hotspot_node_num()) {
      return candidate.get_hotspot_node_num() < current_best.get_hotspot_node_num();
    }
    if (std::abs(candidate.get_max_usage_ratio() - current_best.get_max_usage_ratio()) >= 1e-9) {
      return candidate.get_max_usage_ratio() < current_best.get_max_usage_ratio();
    }
    return candidate.get_total_wire_length() < current_best.get_total_wire_length();
  }
  return score_a < score_b;
}

uint8_t PlanarRouter::getShadowOrientMask(const PRShadowDemandMap* shadow_demand_map, const PlanarCoord& coord)
{
  if (shadow_demand_map == nullptr) {
    return kPRMaskNone;
  }
  auto iter = shadow_demand_map->find(coord);
  if (iter == shadow_demand_map->end()) {
    return kPRMaskNone;
  }
  uint8_t orient_mask = kPRMaskNone;
  for (Orientation orientation : iter->second) {
    orient_mask |= getPROrientMask(orientation);
  }
  return orient_mask;
}

std::vector<PRCandidate> PlanarRouter::getPRCandidateListByTopo(
    PRModel& pr_model, int32_t topo_idx, Segment<PlanarCoord>& planar_topo,
    const std::set<PlanarCoord, CmpPlanarCoordByXASC>& terminal_coord_set,
    const PRShadowDemandMap* shadow_demand_map)
{
  std::vector<PRCandidate> pr_candidate_list;

  auto appendCandidateList = [&](int32_t corner_num, std::vector<std::vector<Segment<PlanarCoord>>> routing_segment_list_list) {
    for (const std::vector<Segment<PlanarCoord>>& routing_segment_list : routing_segment_list_list) {
      std::vector<Segment<PlanarCoord>> candidate_segment_list = routing_segment_list;
      if (isMacroBlockedRoutingSegmentList(pr_model, candidate_segment_list, terminal_coord_set)) {
        pr_model.get_pr_macro_repair_stat().filtered_macro_cross_candidate_num++;
        continue;
      }
      pr_candidate_list.emplace_back(topo_idx, candidate_segment_list, corner_num, 0, false, 0);
    }
  };

  bool long_oblique_topo = isLongObliqueTopo(pr_model, planar_topo);
  if (!long_oblique_topo) {
    appendCandidateList(0, getRoutingSegmentListByStraight(pr_model, planar_topo));
  }
  appendCandidateList(1, getRoutingSegmentListByLPattern(pr_model, planar_topo));
  appendCandidateList(2, getRoutingSegmentListByZPattern(pr_model, planar_topo));
  appendCandidateList(3, getRoutingSegmentListByInner3Bends(pr_model, planar_topo));
  appendCandidateList(4, getRoutingSegmentListByUPattern(pr_model, planar_topo));
  appendCandidateList(5, getRoutingSegmentListByOuter3Bends(pr_model, planar_topo));
  return pr_candidate_list;
}

std::vector<PRCandidate> PlanarRouter::getPRCandidateList(PRModel& pr_model, std::vector<Segment<PlanarCoord>>& planar_topo_list)
{
  std::set<PlanarCoord, CmpPlanarCoordByXASC> terminal_coord_set = getCurrTerminalCoordSet(pr_model);
  std::vector<PRCandidate> pr_candidate_list;
  for (size_t i = 0; i < planar_topo_list.size(); i++) {
    std::vector<PRCandidate> topo_candidate_list
        = getPRCandidateListByTopo(pr_model, static_cast<int32_t>(i), planar_topo_list[i], terminal_coord_set);
    pr_candidate_list.insert(pr_candidate_list.end(), topo_candidate_list.begin(), topo_candidate_list.end());
  }
  return pr_candidate_list;
}

std::vector<Segment<PlanarCoord>> PlanarRouter::getPlanarTopoList(PRModel& pr_model)
{
  std::vector<PlanarCoord> planar_coord_list;
  {
    for (PRPin& pr_pin : pr_model.get_curr_pr_task()->get_pr_pin_list()) {
      planar_coord_list.push_back(pr_pin.get_access_point().get_grid_coord());
    }
    std::sort(planar_coord_list.begin(), planar_coord_list.end(), CmpPlanarCoordByXASC());
    planar_coord_list.erase(std::unique(planar_coord_list.begin(), planar_coord_list.end()), planar_coord_list.end());
  }
  TBTask tb_task;
  tb_task.set_planar_coord_list(planar_coord_list);
  GridMap<bool>& macro_body_forbidden_map = pr_model.get_macro_body_forbidden_map();
  const std::vector<PlanarRect>& macro_body_obs_list = pr_model.get_macro_body_obs_list();
  if (!macro_body_obs_list.empty()) {
    std::vector<PlanarRect> tb_macro_body_obs_list;
    tb_macro_body_obs_list.reserve(macro_body_obs_list.size());
    for (const PlanarRect& macro_body_obs : macro_body_obs_list) {
      tb_macro_body_obs_list.emplace_back(std::max(0, macro_body_obs.get_ll_x() - 1), std::max(0, macro_body_obs.get_ll_y() - 1),
                                          std::min(macro_body_forbidden_map.get_x_size() - 1, macro_body_obs.get_ur_x() + 1),
                                          std::min(macro_body_forbidden_map.get_y_size() - 1, macro_body_obs.get_ur_y() + 1));
    }
    tb_task.set_planar_obs_list(std::move(tb_macro_body_obs_list));
    tb_task.set_planar_search_region(
        PlanarRect(0, 0, macro_body_forbidden_map.get_x_size() - 1, macro_body_forbidden_map.get_y_size() - 1));
  }

  TBSteinerRepairStat tb_stat;
  std::vector<Segment<PlanarCoord>> planar_topo_list = RTTB.getPlanarTopoList(tb_task, tb_stat);
  PRMacroRepairStat& pr_stat = pr_model.get_pr_macro_repair_stat();
  pr_stat.raw_steiner_in_macro += tb_stat.raw_steiner_in_macro;
  pr_stat.fixed_steiner_in_macro += tb_stat.fixed_steiner_in_macro;
  pr_stat.failed_steiner_legalize_num += tb_stat.failed_steiner_legalize_num;
  return planar_topo_list;
}

std::set<PlanarCoord, CmpPlanarCoordByXASC> PlanarRouter::getCurrTerminalCoordSet(PRModel& pr_model)
{
  std::set<PlanarCoord, CmpPlanarCoordByXASC> terminal_coord_set;
  for (PRPin& pr_pin : pr_model.get_curr_pr_task()->get_pr_pin_list()) {
    terminal_coord_set.insert(pr_pin.get_access_point().get_grid_coord());
  }
  return terminal_coord_set;
}

bool PlanarRouter::isMacroForbiddenCoord(PRModel& pr_model, const PlanarCoord& coord)
{
  GridMap<bool>& macro_body_forbidden_map = pr_model.get_macro_body_forbidden_map();
  if (macro_body_forbidden_map.empty() || !macro_body_forbidden_map.isInside(coord.get_x(), coord.get_y())) {
    return false;
  }
  return macro_body_forbidden_map[coord.get_x()][coord.get_y()];
}

bool PlanarRouter::isSameMacroBodyCoord(PRModel& pr_model, const PlanarCoord& first_coord, const PlanarCoord& second_coord)
{
  for (PRMacroRegion& pr_macro_region : pr_model.get_pr_macro_region_list()) {
    PlanarRect& body_grid_rect = pr_macro_region.body_grid_rect;
    if (RTUTIL.isInside(body_grid_rect, first_coord) && RTUTIL.isInside(body_grid_rect, second_coord)) {
      return true;
    }
  }
  return false;
}

int32_t PlanarRouter::getPRMacroRegionId(PRModel& pr_model, const PlanarCoord& coord)
{
  if (!isMacroForbiddenCoord(pr_model, coord)) {
    return -1;
  }
  std::vector<PRMacroRegion>& pr_macro_region_list = pr_model.get_pr_macro_region_list();
  for (int32_t region_idx = 0; region_idx < static_cast<int32_t>(pr_macro_region_list.size()); region_idx++) {
    if (RTUTIL.isInside(pr_macro_region_list[region_idx].body_grid_rect, coord)) {
      return region_idx;
    }
  }
  return -1;
}

bool PlanarRouter::isMacroBlockedSegment(PRModel& pr_model, Segment<PlanarCoord>& planar_segment,
                                              const std::set<PlanarCoord, CmpPlanarCoordByXASC>& terminal_coord_set)
{
  if (pr_model.get_pr_macro_region_list().empty()) {
    return false;
  }
  PlanarCoord first_coord = planar_segment.get_first();
  PlanarCoord second_coord = planar_segment.get_second();
  if (first_coord == second_coord) {
    return false;
  }
  if (!RTUTIL.isRightAngled(first_coord, second_coord)) {
    return true;
  }

  int32_t step_x = first_coord.get_x() == second_coord.get_x() ? 0 : (first_coord.get_x() < second_coord.get_x() ? 1 : -1);
  int32_t step_y = first_coord.get_y() == second_coord.get_y() ? 0 : (first_coord.get_y() < second_coord.get_y() ? 1 : -1);

  std::set<PlanarCoord, CmpPlanarCoordByXASC> forbidden_coord_set;
  for (PlanarCoord coord = first_coord;; coord.set_coord(coord.get_x() + step_x, coord.get_y() + step_y)) {
    if (isMacroForbiddenCoord(pr_model, coord)) {
      forbidden_coord_set.insert(coord);
    }
    if (coord == second_coord) {
      break;
    }
  }
  if (forbidden_coord_set.empty()) {
    return false;
  }

  std::set<PlanarCoord, CmpPlanarCoordByXASC> terminal_stub_coord_set;
  auto addTerminalStubCoord = [&](const PlanarCoord& terminal_coord, const PlanarCoord& stop_coord, int32_t curr_step_x, int32_t curr_step_y) {
    if (terminal_coord_set.find(terminal_coord) == terminal_coord_set.end() || !isMacroForbiddenCoord(pr_model, terminal_coord)) {
      return;
    }
    for (PlanarCoord coord = terminal_coord;; coord.set_coord(coord.get_x() + curr_step_x, coord.get_y() + curr_step_y)) {
      if (!isMacroForbiddenCoord(pr_model, coord) || !isSameMacroBodyCoord(pr_model, terminal_coord, coord)) {
        break;
      }
      terminal_stub_coord_set.insert(coord);
      if (coord == stop_coord) {
        break;
      }
    }
  };
  addTerminalStubCoord(first_coord, second_coord, step_x, step_y);
  addTerminalStubCoord(second_coord, first_coord, -step_x, -step_y);

  for (PlanarCoord forbidden_coord : forbidden_coord_set) {
    if (terminal_stub_coord_set.find(forbidden_coord) == terminal_stub_coord_set.end()) {
      return true;
    }
  }
  return false;
}

bool PlanarRouter::isMacroBlockedRoutingSegmentList(PRModel& pr_model, std::vector<Segment<PlanarCoord>>& routing_segment_list,
                                                         const std::set<PlanarCoord, CmpPlanarCoordByXASC>& terminal_coord_set)
{
  for (Segment<PlanarCoord>& routing_segment : routing_segment_list) {
    if (isMacroBlockedSegment(pr_model, routing_segment, terminal_coord_set)) {
      return true;
    }
  }
  return false;
}

std::vector<PlanarRouter::PRAStarEscapeNode> PlanarRouter::getAStarEscapeNodeList(
    PRModel& pr_model, const PlanarCoord& terminal_coord,
    const std::set<PlanarCoord, CmpPlanarCoordByXASC>& terminal_coord_set,
    const PRShadowDemandMap* shadow_demand_map)
{
  constexpr int32_t kEscapeCandidateNumPerDir = 8;
  constexpr int32_t kEscapeCandidateTopK = 8;

  std::vector<PRAStarEscapeNode> escape_node_list;
  GridMap<PRNode>& pr_node_map = pr_model.get_pr_node_map();
  if (!pr_node_map.isInside(terminal_coord.get_x(), terminal_coord.get_y())) {
    return escape_node_list;
  }
  if (!isMacroForbiddenCoord(pr_model, terminal_coord)) {
    PRAStarEscapeNode escape_node;
    escape_node.terminal_coord = terminal_coord;
    escape_node.route_coord = terminal_coord;
    escape_node_list.push_back(escape_node);
    return escape_node_list;
  }
  if (terminal_coord_set.find(terminal_coord) == terminal_coord_set.end()) {
    return escape_node_list;
  }
  int32_t macro_region_id = getPRMacroRegionId(pr_model, terminal_coord);
  if (macro_region_id == -1) {
    return escape_node_list;
  }

  auto addEscapeCandidate = [&](const PlanarCoord& route_coord) {
    Segment<PlanarCoord> stub_segment(terminal_coord, route_coord);
    if (isMacroBlockedSegment(pr_model, stub_segment, terminal_coord_set)) {
      return;
    }
    double cost = getPatternSegmentScore(pr_model, stub_segment, terminal_coord_set, shadow_demand_map);
    PRAStarEscapeNode escape_node;
    escape_node.terminal_coord = terminal_coord;
    escape_node.route_coord = route_coord;
    escape_node.stub_segment_list.push_back(stub_segment);
    escape_node.cost = cost;
    escape_node_list.push_back(escape_node);
  };

  std::vector<std::pair<int32_t, int32_t>> step_list = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
  for (auto& [step_x, step_y] : step_list) {
    int32_t candidate_num = 0;
    for (PlanarCoord coord(terminal_coord.get_x() + step_x, terminal_coord.get_y() + step_y);
         pr_node_map.isInside(coord.get_x(), coord.get_y());
         coord.set_coord(coord.get_x() + step_x, coord.get_y() + step_y)) {
      if (isMacroForbiddenCoord(pr_model, coord)) {
        if (getPRMacroRegionId(pr_model, coord) == macro_region_id) {
          continue;
        }
        break;
      }
      addEscapeCandidate(coord);
      candidate_num++;
      if (candidate_num >= kEscapeCandidateNumPerDir) {
        break;
      }
    }
  }

  std::sort(escape_node_list.begin(), escape_node_list.end(), [&](PRAStarEscapeNode& a, PRAStarEscapeNode& b) {
    if (!RTUTIL.equalDoubleByError(a.cost, b.cost, RT_ERROR)) {
      return a.cost < b.cost;
    }
    int32_t a_dist = RTUTIL.getManhattanDistance(a.terminal_coord, a.route_coord);
    int32_t b_dist = RTUTIL.getManhattanDistance(b.terminal_coord, b.route_coord);
    if (a_dist != b_dist) {
      return a_dist < b_dist;
    }
    return CmpPlanarCoordByXASC()(a.route_coord, b.route_coord);
  });
  escape_node_list.erase(std::unique(escape_node_list.begin(), escape_node_list.end(),
                                     [](PRAStarEscapeNode& a, PRAStarEscapeNode& b) { return a.route_coord == b.route_coord; }),
                         escape_node_list.end());
  if (static_cast<int32_t>(escape_node_list.size()) > kEscapeCandidateTopK) {
    escape_node_list.resize(kEscapeCandidateTopK);
  }
  return escape_node_list;
}

std::vector<Segment<PlanarCoord>> PlanarRouter::getRoutingSegmentListByAStarWithEscape(
    PRModel& pr_model, Segment<PlanarCoord>& planar_topo,
    const std::set<PlanarCoord, CmpPlanarCoordByXASC>& terminal_coord_set,
    const PRShadowDemandMap* shadow_demand_map)
{
  auto prepare_begin_time = std::chrono::steady_clock::now();
  PlanarCoord start_coord = planar_topo.get_first();
  PlanarCoord end_coord = planar_topo.get_second();
  if (start_coord == end_coord) {
    return {};
  }

  std::vector<PRAStarEscapeNode> start_escape_node_list
      = getAStarEscapeNodeList(pr_model, start_coord, terminal_coord_set, shadow_demand_map);
  std::vector<PRAStarEscapeNode> end_escape_node_list
      = getAStarEscapeNodeList(pr_model, end_coord, terminal_coord_set, shadow_demand_map);
  if (start_escape_node_list.empty() || end_escape_node_list.empty()) {
    return {};
  }

  PRMacroRepairStat& macro_repair_stat = pr_model.get_pr_macro_repair_stat();
  std::vector<PRAStarPairTask> pair_task_list;
  pair_task_list.reserve(start_escape_node_list.size() * end_escape_node_list.size());
  PlanarRect workspace_rect;
  bool has_workspace_rect = false;
  auto updateWorkspaceRect = [&](const PlanarRect& search_rect) {
    if (!has_workspace_rect) {
      workspace_rect = search_rect;
      has_workspace_rect = true;
      return;
    }
    workspace_rect.set_ll_x(std::min(workspace_rect.get_ll_x(), search_rect.get_ll_x()));
    workspace_rect.set_ll_y(std::min(workspace_rect.get_ll_y(), search_rect.get_ll_y()));
    workspace_rect.set_ur_x(std::max(workspace_rect.get_ur_x(), search_rect.get_ur_x()));
    workspace_rect.set_ur_y(std::max(workspace_rect.get_ur_y(), search_rect.get_ur_y()));
  };
  for (int32_t start_idx = 0; start_idx < static_cast<int32_t>(start_escape_node_list.size()); start_idx++) {
    for (int32_t end_idx = 0; end_idx < static_cast<int32_t>(end_escape_node_list.size()); end_idx++) {
      PRAStarEscapeNode& start_escape_node = start_escape_node_list[start_idx];
      PRAStarEscapeNode& end_escape_node = end_escape_node_list[end_idx];
      PRAStarPairTask pair_task;
      pair_task.start_idx = start_idx;
      pair_task.end_idx = end_idx;
      pair_task.lower_bound = start_escape_node.cost + end_escape_node.cost
                              + RTUTIL.getManhattanDistance(start_escape_node.route_coord, end_escape_node.route_coord);
      pair_task.need_search = start_escape_node.route_coord != end_escape_node.route_coord;
      if (pair_task.need_search) {
        Segment<PlanarCoord> escaped_topo(start_escape_node.route_coord, end_escape_node.route_coord);
        pair_task.search_rect = getAStarSearchRect(pr_model, escaped_topo);
        updateWorkspaceRect(pair_task.search_rect);
      }
      pair_task_list.push_back(pair_task);
    }
  }
  macro_repair_stat.astar_escape_pair_num += pair_task_list.size();
  if (has_workspace_rect) {
    prepareAStarWorkspace(pr_model, workspace_rect, _astar_workspace);
  }
  auto prepare_end_time = std::chrono::steady_clock::now();
  macro_repair_stat.astar_prepare_time_ms
      += std::chrono::duration<double, std::milli>(prepare_end_time - prepare_begin_time).count();

  double best_score = DBL_MAX;
  std::vector<Segment<PlanarCoord>> best_segment_list;
  for (PRAStarPairTask& pair_task : pair_task_list) {
    if (best_score < DBL_MAX / 2 && pair_task.lower_bound > best_score
        && !RTUTIL.equalDoubleByError(pair_task.lower_bound, best_score, RT_ERROR)) {
      macro_repair_stat.astar_pruned_pair_num++;
      continue;
    }
    PRAStarEscapeNode& start_escape_node = start_escape_node_list[pair_task.start_idx];
    PRAStarEscapeNode& end_escape_node = end_escape_node_list[pair_task.end_idx];
    std::vector<Segment<PlanarCoord>> routing_segment_list = start_escape_node.stub_segment_list;

    if (pair_task.need_search) {
      std::vector<Segment<PlanarCoord>> astar_segment_list;
      if (!searchRoutingSegmentByAStar(pr_model, start_escape_node.route_coord, end_escape_node.route_coord, pair_task.search_rect,
                                       terminal_coord_set, shadow_demand_map, _astar_workspace, astar_segment_list)) {
        continue;
      }
      routing_segment_list.insert(routing_segment_list.end(), astar_segment_list.begin(), astar_segment_list.end());
    }

    for (auto stub_iter = end_escape_node.stub_segment_list.rbegin(); stub_iter != end_escape_node.stub_segment_list.rend(); stub_iter++) {
      routing_segment_list.emplace_back(stub_iter->get_second(), stub_iter->get_first());
    }
    auto validate_begin_time = std::chrono::steady_clock::now();
    bool path_blocked = routing_segment_list.empty() || isMacroBlockedRoutingSegmentList(pr_model, routing_segment_list, terminal_coord_set);
    double score = path_blocked ? DBL_MAX : getLegalRoutingSegmentListScore(pr_model, routing_segment_list, shadow_demand_map);
    auto validate_end_time = std::chrono::steady_clock::now();
    macro_repair_stat.astar_validate_time_ms
        += std::chrono::duration<double, std::milli>(validate_end_time - validate_begin_time).count();
    if (path_blocked) {
      continue;
    }

    if (score < best_score
        || (RTUTIL.equalDoubleByError(score, best_score, RT_ERROR)
            && routing_segment_list.size() < best_segment_list.size())) {
      best_score = score;
      best_segment_list = routing_segment_list;
    }
  }
  return best_segment_list;
}

double PlanarRouter::getLegalRoutingSegmentListScore(PRModel& pr_model, std::vector<Segment<PlanarCoord>>& routing_segment_list,
                                                          const PRShadowDemandMap* shadow_demand_map)
{
  constexpr double kBlockedSegmentListScore = DBL_MAX / 4;
  double score = 0;
  for (Segment<PlanarCoord>& routing_segment : routing_segment_list) {
    score += getPatternSegmentCost(pr_model, routing_segment, shadow_demand_map);
    if (score >= kBlockedSegmentListScore) {
      return kBlockedSegmentListScore;
    }
  }
  return score;
}

void PlanarRouter::prepareAStarWorkspace(PRModel& pr_model, const PlanarRect& workspace_rect, PRAStarWorkspace& workspace)
{
  workspace.workspace_rect = workspace_rect;
  workspace.x_size = workspace_rect.get_ur_x() - workspace_rect.get_ll_x() + 1;
  workspace.y_size = workspace_rect.get_ur_y() - workspace_rect.get_ll_y() + 1;
  if (workspace.x_size <= 0 || workspace.y_size <= 0) {
    RTLOG.error(Loc::current(), "The A* workspace is empty!");
  }
  size_t cell_num = static_cast<size_t>(workspace.x_size) * workspace.y_size;
  if (cell_num > static_cast<size_t>(INT_MAX)) {
    RTLOG.error(Loc::current(), "The A* workspace is too large!");
  }
  if (workspace.node_state_list.size() < cell_num) {
    workspace.node_state_list.resize(cell_num);
  }
  if (workspace.node_cost_list.size() < cell_num) {
    workspace.node_cost_list.resize(cell_num);
  }
  workspace.open_heap.clear();
  workspace.context_stamp++;
  if (workspace.context_stamp == 0) {
    for (PRAStarNodeCostCache& node_cost : workspace.node_cost_list) {
      node_cost.context_stamp = 0;
    }
    workspace.context_stamp = 1;
  }
  PRMacroRepairStat& macro_repair_stat = pr_model.get_pr_macro_repair_stat();
  macro_repair_stat.astar_max_workspace_cell_num
      = std::max(macro_repair_stat.astar_max_workspace_cell_num, static_cast<int64_t>(cell_num));
}

int32_t PlanarRouter::getAStarNodeIndex(const PRAStarWorkspace& workspace, const PlanarCoord& coord)
{
  int32_t local_x = coord.get_x() - workspace.workspace_rect.get_ll_x();
  int32_t local_y = coord.get_y() - workspace.workspace_rect.get_ll_y();
  if (local_x < 0 || workspace.x_size <= local_x || local_y < 0 || workspace.y_size <= local_y) {
    RTLOG.error(Loc::current(), "The A* node is outside the workspace!");
  }
  return local_x * workspace.y_size + local_y;
}

PlanarCoord PlanarRouter::getAStarNodeCoord(const PRAStarWorkspace& workspace, int32_t node_idx)
{
  int64_t cell_num = static_cast<int64_t>(workspace.x_size) * workspace.y_size;
  if (node_idx < 0 || cell_num <= node_idx) {
    RTLOG.error(Loc::current(), "The A* node index is outside the workspace!");
  }
  return PlanarCoord(workspace.workspace_rect.get_ll_x() + node_idx / workspace.y_size,
                     workspace.workspace_rect.get_ll_y() + node_idx % workspace.y_size);
}

bool PlanarRouter::searchRoutingSegmentByAStar(
    PRModel& pr_model, const PlanarCoord& start_coord, const PlanarCoord& end_coord, const PlanarRect& search_rect,
    const std::set<PlanarCoord, CmpPlanarCoordByXASC>& terminal_coord_set, const PRShadowDemandMap* shadow_demand_map,
    PRAStarWorkspace& workspace, std::vector<Segment<PlanarCoord>>& routing_segment_list)
{
  auto search_begin_time = std::chrono::steady_clock::now();
  PRMacroRepairStat& macro_repair_stat = pr_model.get_pr_macro_repair_stat();
  macro_repair_stat.astar_search_num++;
  auto finishSearch = [&](bool success) {
    auto search_end_time = std::chrono::steady_clock::now();
    macro_repair_stat.astar_search_time_ms
        += std::chrono::duration<double, std::milli>(search_end_time - search_begin_time).count();
    return success;
  };

  GridMap<PRNode>& pr_node_map = pr_model.get_pr_node_map();
  if (start_coord == end_coord || !pr_node_map.isInside(start_coord.get_x(), start_coord.get_y())
      || !pr_node_map.isInside(end_coord.get_x(), end_coord.get_y())) {
    return finishSearch(false);
  }
  if (!RTUTIL.isInside(search_rect, start_coord) || !RTUTIL.isInside(search_rect, end_coord)) {
    return finishSearch(false);
  }
  PlanarRect& workspace_rect = workspace.workspace_rect;
  if (search_rect.get_ll_x() < workspace_rect.get_ll_x() || search_rect.get_ll_y() < workspace_rect.get_ll_y()
      || workspace_rect.get_ur_x() < search_rect.get_ur_x() || workspace_rect.get_ur_y() < search_rect.get_ur_y()) {
    RTLOG.error(Loc::current(), "The A* search region is outside the workspace!");
  }
  Segment<PlanarCoord> planar_topo(start_coord, end_coord);
  if (!isAStarAccessibleCoord(pr_model, start_coord, planar_topo, terminal_coord_set)
      || !isAStarAccessibleCoord(pr_model, end_coord, planar_topo, terminal_coord_set)) {
    return finishSearch(false);
  }

  workspace.search_stamp++;
  if (workspace.search_stamp == 0) {
    for (PRAStarNodeState& node_state : workspace.node_state_list) {
      node_state.search_stamp = 0;
    }
    workspace.search_stamp = 1;
  }
  workspace.open_heap.clear();

  auto getNodeState = [&](int32_t node_idx) -> PRAStarNodeState& {
    PRAStarNodeState& node_state = workspace.node_state_list[node_idx];
    if (node_state.search_stamp != workspace.search_stamp) {
      node_state.search_stamp = workspace.search_stamp;
      node_state.closed = false;
      node_state.parent_idx = -1;
      node_state.known_cost = DBL_MAX;
    }
    return node_state;
  };
  auto cmpQueueNode = [&](const PRAStarQueueNode& a, const PRAStarQueueNode& b) {
    if (std::abs(a.getTotalCost() - b.getTotalCost()) < 1e-9) {
      if (std::abs(a.estimated_cost - b.estimated_cost) < 1e-9) {
        PlanarCoord a_coord = getAStarNodeCoord(workspace, a.node_idx);
        PlanarCoord b_coord = getAStarNodeCoord(workspace, b.node_idx);
        return CmpPlanarCoordByXASC()(b_coord, a_coord);
      }
      return a.estimated_cost > b.estimated_cost;
    }
    return a.getTotalCost() > b.getTotalCost();
  };
  auto pushToOpenList = [&](int32_t node_idx, double known_cost, double estimated_cost) {
    workspace.open_heap.push_back({node_idx, known_cost, estimated_cost});
    std::push_heap(workspace.open_heap.begin(), workspace.open_heap.end(), cmpQueueNode);
    macro_repair_stat.astar_push_node_num++;
  };

  int32_t start_idx = getAStarNodeIndex(workspace, start_coord);
  int32_t end_idx = getAStarNodeIndex(workspace, end_coord);
  PRAStarNodeState& start_state = getNodeState(start_idx);
  start_state.known_cost = 0;
  pushToOpenList(start_idx, 0, getAStarEstimateCost(pr_model, start_coord, end_coord));
  while (!workspace.open_heap.empty()) {
    std::pop_heap(workspace.open_heap.begin(), workspace.open_heap.end(), cmpQueueNode);
    PRAStarQueueNode queue_node = workspace.open_heap.back();
    workspace.open_heap.pop_back();

    PRAStarNodeState& curr_node_state = getNodeState(queue_node.node_idx);
    if (curr_node_state.closed
        || (queue_node.known_cost > curr_node_state.known_cost
            && !RTUTIL.equalDoubleByError(queue_node.known_cost, curr_node_state.known_cost, RT_ERROR))) {
      macro_repair_stat.astar_stale_pop_num++;
      continue;
    }
    curr_node_state.closed = true;
    macro_repair_stat.astar_expanded_node_num++;
    PlanarCoord curr_coord = getAStarNodeCoord(workspace, queue_node.node_idx);
    if (queue_node.node_idx == end_idx) {
      break;
    }

    PRNode& curr_pr_node = pr_node_map[curr_coord.get_x()][curr_coord.get_y()];
    for (auto& [orientation, neighbor_node] : curr_pr_node.get_neighbor_node_map()) {
      if (neighbor_node == nullptr) {
        continue;
      }
      PlanarCoord neighbor_coord = *neighbor_node;
      if (!RTUTIL.isInside(search_rect, neighbor_coord)
          || !isAStarAccessibleCoord(pr_model, neighbor_coord, planar_topo, terminal_coord_set)) {
        continue;
      }

      int32_t neighbor_idx = getAStarNodeIndex(workspace, neighbor_coord);
      PRAStarNodeState& neighbor_node_state = getNodeState(neighbor_idx);
      if (neighbor_node_state.closed) {
        continue;
      }
      PlanarCoord parent_coord(-1, -1);
      if (curr_node_state.parent_idx != -1) {
        parent_coord = getAStarNodeCoord(workspace, curr_node_state.parent_idx);
      }
      double step_cost = getAStarStepCost(pr_model, curr_coord, neighbor_coord, parent_coord, shadow_demand_map, workspace);
      if (step_cost >= DBL_MAX / 2) {
        continue;
      }
      double known_cost = curr_node_state.known_cost + step_cost;
      if (known_cost < neighbor_node_state.known_cost) {
        neighbor_node_state.parent_idx = queue_node.node_idx;
        neighbor_node_state.known_cost = known_cost;
        pushToOpenList(neighbor_idx, known_cost, getAStarEstimateCost(pr_model, neighbor_coord, end_coord));
      }
    }
  }

  PRAStarNodeState& end_state = getNodeState(end_idx);
  if (!end_state.closed) {
    return finishSearch(false);
  }

  std::vector<PlanarCoord> coord_list;
  int32_t curr_idx = end_idx;
  while (true) {
    coord_list.push_back(getAStarNodeCoord(workspace, curr_idx));
    if (curr_idx == start_idx) {
      break;
    }
    curr_idx = getNodeState(curr_idx).parent_idx;
    if (curr_idx == -1) {
      return finishSearch(false);
    }
  }
  std::reverse(coord_list.begin(), coord_list.end());
  routing_segment_list = getRoutingSegmentListByCoordList(coord_list);
  return finishSearch(!routing_segment_list.empty());
}

PlanarRect PlanarRouter::getAStarSearchRect(PRModel& pr_model, Segment<PlanarCoord>& planar_topo)
{
  GridMap<PRNode>& pr_node_map = pr_model.get_pr_node_map();
  PlanarCoord first_coord = planar_topo.get_first();
  PlanarCoord second_coord = planar_topo.get_second();

  PlanarRect topo_rect(std::min(first_coord.get_x(), second_coord.get_x()), std::min(first_coord.get_y(), second_coord.get_y()),
                       std::max(first_coord.get_x(), second_coord.get_x()), std::max(first_coord.get_y(), second_coord.get_y()));
  PlanarRect search_rect = topo_rect;

  auto updateSearchRect = [&](PlanarRect& rect) {
    search_rect.set_ll_x(std::min(search_rect.get_ll_x(), rect.get_ll_x()));
    search_rect.set_ll_y(std::min(search_rect.get_ll_y(), rect.get_ll_y()));
    search_rect.set_ur_x(std::max(search_rect.get_ur_x(), rect.get_ur_x()));
    search_rect.set_ur_y(std::max(search_rect.get_ur_y(), rect.get_ur_y()));
  };

  for (PRMacroRegion& pr_macro_region : pr_model.get_pr_macro_region_list()) {
    PlanarRect& body_grid_rect = pr_macro_region.body_grid_rect;
    if (RTUTIL.isClosedOverlap(topo_rect, body_grid_rect) || RTUTIL.isInside(body_grid_rect, first_coord)
        || RTUTIL.isInside(body_grid_rect, second_coord)) {
      updateSearchRect(body_grid_rect);
    }
  }

  int32_t search_margin = std::max(2, pr_model.get_pr_com_param().get_expand_step_num() * pr_model.get_pr_com_param().get_expand_step_length());
  search_rect.set_ll_x(std::max(0, search_rect.get_ll_x() - search_margin));
  search_rect.set_ll_y(std::max(0, search_rect.get_ll_y() - search_margin));
  search_rect.set_ur_x(std::min(pr_node_map.get_x_size() - 1, search_rect.get_ur_x() + search_margin));
  search_rect.set_ur_y(std::min(pr_node_map.get_y_size() - 1, search_rect.get_ur_y() + search_margin));
  return search_rect;
}

bool PlanarRouter::isAStarAccessibleCoord(PRModel& pr_model, const PlanarCoord& coord, Segment<PlanarCoord>& planar_topo,
                                               const std::set<PlanarCoord, CmpPlanarCoordByXASC>& terminal_coord_set)
{
  (void) planar_topo;
  (void) terminal_coord_set;
  return !isMacroForbiddenCoord(pr_model, coord);
}

double PlanarRouter::getAStarStepCost(PRModel& pr_model, const PlanarCoord& start_coord, const PlanarCoord& end_coord,
                                           const PlanarCoord& parent_coord, const PRShadowDemandMap* shadow_demand_map,
                                           PRAStarWorkspace& workspace)
{
  Direction direction = RTUTIL.getDirection(start_coord, end_coord);
  if (direction != Direction::kHorizontal && direction != Direction::kVertical) {
    return DBL_MAX;
  }

  double step_cost = 1.0;
  step_cost += getAStarNodeCost(pr_model, start_coord, direction, shadow_demand_map, workspace);
  step_cost += getAStarNodeCost(pr_model, end_coord, direction, shadow_demand_map, workspace);
  if (parent_coord.get_x() != -1 || parent_coord.get_y() != -1) {
    Direction parent_direction = RTUTIL.getDirection(parent_coord, start_coord);
    if (parent_direction != Direction::kProximal && parent_direction != direction) {
      step_cost += pr_model.get_pr_com_param().get_corner_weight();
    }
  }
  return step_cost;
}

double PlanarRouter::getAStarNodeCost(PRModel& pr_model, const PlanarCoord& coord, Direction direction,
                                           const PRShadowDemandMap* shadow_demand_map, PRAStarWorkspace& workspace)
{
  int32_t direction_idx = -1;
  if (direction == Direction::kHorizontal) {
    direction_idx = 0;
  } else if (direction == Direction::kVertical) {
    direction_idx = 1;
  } else {
    RTLOG.error(Loc::current(), "The A* direction is error!");
  }

  int32_t node_idx = getAStarNodeIndex(workspace, coord);
  PRAStarNodeCostCache& node_cost_cache = workspace.node_cost_list[node_idx];
  if (node_cost_cache.context_stamp != workspace.context_stamp) {
    node_cost_cache.context_stamp = workspace.context_stamp;
    node_cost_cache.valid_mask = 0;
  }
  uint8_t direction_bit = static_cast<uint8_t>(1U << direction_idx);
  PRMacroRepairStat& macro_repair_stat = pr_model.get_pr_macro_repair_stat();
  if (node_cost_cache.valid_mask & direction_bit) {
    macro_repair_stat.astar_cost_cache_hit_num++;
    return node_cost_cache.cost[direction_idx];
  }

  double overflow_unit = pr_model.get_pr_com_param().get_overflow_unit();
  int32_t curr_net_idx = pr_model.get_curr_pr_task()->get_net_idx();
  uint8_t direction_mask = getPRDirectionMask(direction);
  uint8_t shadow_mask = getShadowOrientMask(shadow_demand_map, coord);
  GridMap<PRNode>& pr_node_map = pr_model.get_pr_node_map();
  double node_cost = pr_node_map[coord.get_x()][coord.get_y()]
                         .getFastCost(curr_net_idx, direction_mask | shadow_mask, overflow_unit, false)
                         .getTotalCost();
  node_cost_cache.cost[direction_idx] = node_cost;
  node_cost_cache.valid_mask |= direction_bit;
  macro_repair_stat.astar_cost_cache_miss_num++;
  return node_cost;
}

double PlanarRouter::getAStarEstimateCost(PRModel& pr_model, const PlanarCoord& start_coord, const PlanarCoord& end_coord)
{
  (void) pr_model;
  return RTUTIL.getManhattanDistance(start_coord, end_coord);
}

std::vector<Segment<PlanarCoord>> PlanarRouter::getRoutingSegmentListByCoordList(std::vector<PlanarCoord>& coord_list)
{
  std::vector<Segment<PlanarCoord>> routing_segment_list;
  if (coord_list.size() <= 1) {
    return routing_segment_list;
  }

  PlanarCoord segment_first_coord = coord_list.front();
  PlanarCoord prev_coord = coord_list.front();
  Direction prev_direction = Direction::kNone;
  for (size_t i = 1; i < coord_list.size(); i++) {
    PlanarCoord curr_coord = coord_list[i];
    if (curr_coord == prev_coord) {
      continue;
    }
    Direction curr_direction = RTUTIL.getDirection(prev_coord, curr_coord);
    if (curr_direction == Direction::kOblique) {
      return {};
    }
    if (prev_direction != Direction::kNone && curr_direction != prev_direction) {
      routing_segment_list.emplace_back(segment_first_coord, prev_coord);
      segment_first_coord = prev_coord;
    }
    prev_coord = curr_coord;
    prev_direction = curr_direction;
  }
  if (segment_first_coord != prev_coord) {
    routing_segment_list.emplace_back(segment_first_coord, prev_coord);
  }
  return routing_segment_list;
}

bool PlanarRouter::isLongObliqueTopo(PRModel& pr_model, Segment<PlanarCoord>& planar_topo)
{
  int32_t topo_spilt_length = pr_model.get_pr_com_param().get_topo_spilt_length();
  PlanarCoord& first_coord = planar_topo.get_first();
  PlanarCoord& second_coord = planar_topo.get_second();
  int32_t span_x = std::abs(first_coord.get_x() - second_coord.get_x());
  int32_t span_y = std::abs(first_coord.get_y() - second_coord.get_y());
  return (span_x > 1 && span_y > 1 && (span_x > topo_spilt_length || span_y > topo_spilt_length));
}

std::vector<std::vector<Segment<PlanarCoord>>> PlanarRouter::getRoutingSegmentListByStraight(PRModel& pr_model, Segment<PlanarCoord>& planar_topo)
{
  PlanarCoord& first_coord = planar_topo.get_first();
  PlanarCoord& second_coord = planar_topo.get_second();
  if (RTUTIL.isOblique(first_coord, second_coord)) {
    return {};
  }
  std::vector<std::vector<Segment<PlanarCoord>>> routing_segment_list_list;
  {
    std::vector<Segment<PlanarCoord>> routing_segment_list;
    routing_segment_list.emplace_back(first_coord, second_coord);
    routing_segment_list_list.push_back(routing_segment_list);
  }
  return routing_segment_list_list;
}

std::vector<std::vector<Segment<PlanarCoord>>> PlanarRouter::getRoutingSegmentListByLPattern(PRModel& pr_model, Segment<PlanarCoord>& planar_topo)
{
  PlanarCoord& first_coord = planar_topo.get_first();
  PlanarCoord& second_coord = planar_topo.get_second();
  if (RTUTIL.isRightAngled(first_coord, second_coord)) {
    return {};
  }
  std::vector<std::vector<PlanarCoord>> inflection_list_list;
  PlanarCoord inflection_coord1(first_coord.get_x(), second_coord.get_y());
  inflection_list_list.push_back({inflection_coord1});
  PlanarCoord inflection_coord2(second_coord.get_x(), first_coord.get_y());
  inflection_list_list.push_back({inflection_coord2});

  std::vector<std::vector<Segment<PlanarCoord>>> routing_segment_list_list;
  for (std::vector<PlanarCoord>& inflection_list : inflection_list_list) {
    std::vector<Segment<PlanarCoord>> routing_segment_list;
    routing_segment_list.emplace_back(planar_topo.get_first(), inflection_list.front());
    for (size_t i = 1; i < inflection_list.size(); i++) {
      routing_segment_list.emplace_back(inflection_list[i - 1], inflection_list[i]);
    }
    routing_segment_list.emplace_back(inflection_list.back(), planar_topo.get_second());
    routing_segment_list_list.push_back(routing_segment_list);
  }
  return routing_segment_list_list;
}

std::vector<std::vector<Segment<PlanarCoord>>> PlanarRouter::getRoutingSegmentListByZPattern(PRModel& pr_model, Segment<PlanarCoord>& planar_topo)
{
  PlanarCoord& first_coord = planar_topo.get_first();
  PlanarCoord& second_coord = planar_topo.get_second();
  if (RTUTIL.isRightAngled(first_coord, second_coord)) {
    return {};
  }
  std::vector<int32_t> x_mid_index_list = getMidIndexList(first_coord.get_x(), second_coord.get_x());
  std::vector<int32_t> y_mid_index_list = getMidIndexList(first_coord.get_y(), second_coord.get_y());
  if (x_mid_index_list.empty() && y_mid_index_list.empty()) {
    return {};
  }
  std::vector<std::vector<PlanarCoord>> inflection_list_list;
  for (size_t i = 0; i < x_mid_index_list.size(); i++) {
    PlanarCoord inflection_coord1(x_mid_index_list[i], first_coord.get_y());
    PlanarCoord inflection_coord2(x_mid_index_list[i], second_coord.get_y());
    inflection_list_list.push_back({inflection_coord1, inflection_coord2});
  }
  for (size_t i = 0; i < y_mid_index_list.size(); i++) {
    PlanarCoord inflection_coord1(first_coord.get_x(), y_mid_index_list[i]);
    PlanarCoord inflection_coord2(second_coord.get_x(), y_mid_index_list[i]);
    inflection_list_list.push_back({inflection_coord1, inflection_coord2});
  }
  std::vector<std::vector<Segment<PlanarCoord>>> routing_segment_list_list;
  for (std::vector<PlanarCoord>& inflection_list : inflection_list_list) {
    std::vector<Segment<PlanarCoord>> routing_segment_list;
    routing_segment_list.emplace_back(planar_topo.get_first(), inflection_list.front());
    for (size_t i = 1; i < inflection_list.size(); i++) {
      routing_segment_list.emplace_back(inflection_list[i - 1], inflection_list[i]);
    }
    routing_segment_list.emplace_back(inflection_list.back(), planar_topo.get_second());
    routing_segment_list_list.push_back(routing_segment_list);
  }
  return routing_segment_list_list;
}

std::vector<int32_t> PlanarRouter::getMidIndexList(int32_t first_idx, int32_t second_idx)
{
  std::vector<int32_t> mid_index_list;
  RTUTIL.swapByASC(first_idx, second_idx);
  mid_index_list.reserve(second_idx - first_idx - 1);
  for (int32_t i = (first_idx + 1); i <= (second_idx - 1); i++) {
    mid_index_list.push_back(i);
  }
  return mid_index_list;
}

std::vector<std::vector<Segment<PlanarCoord>>> PlanarRouter::getRoutingSegmentListByUPattern(PRModel& pr_model, Segment<PlanarCoord>& planar_topo)
{
  Die& die = RTDM.getDatabase().get_die();
  int32_t expand_step_num = pr_model.get_pr_com_param().get_expand_step_num();
  int32_t expand_step_length = pr_model.get_pr_com_param().get_expand_step_length();

  PlanarCoord& first_coord = planar_topo.get_first();
  PlanarCoord& second_coord = planar_topo.get_second();
  if (RTUTIL.getManhattanDistance(first_coord, second_coord) <= 1) {
    return {};
  }
  int32_t first_x = first_coord.get_x();
  int32_t second_x = second_coord.get_x();
  int32_t first_y = first_coord.get_y();
  int32_t second_y = second_coord.get_y();
  RTUTIL.swapByASC(first_x, second_x);
  RTUTIL.swapByASC(first_y, second_y);

  std::vector<std::vector<PlanarCoord>> inflection_list_list;
  if (!RTUTIL.isHorizontal(first_coord, second_coord)) {
    for (int32_t i = 0; i < expand_step_num; i++) {
      first_x -= expand_step_length;
      if (first_x >= die.get_grid_ll_x()) {
        PlanarCoord inflection_coord1(first_x, first_coord.get_y());
        PlanarCoord inflection_coord2(first_x, second_coord.get_y());
        inflection_list_list.push_back({inflection_coord1, inflection_coord2});
      }
      second_x += expand_step_length;
      if (second_x <= die.get_grid_ur_x()) {
        PlanarCoord inflection_coord1(second_x, first_coord.get_y());
        PlanarCoord inflection_coord2(second_x, second_coord.get_y());
        inflection_list_list.push_back({inflection_coord1, inflection_coord2});
      }
    }
  }
  if (!RTUTIL.isVertical(first_coord, second_coord)) {
    for (int32_t i = 0; i < expand_step_num; i++) {
      first_y -= expand_step_length;
      if (first_y >= die.get_grid_ll_y()) {
        PlanarCoord inflection_coord1(first_coord.get_x(), first_y);
        PlanarCoord inflection_coord2(second_coord.get_x(), first_y);
        inflection_list_list.push_back({inflection_coord1, inflection_coord2});
      }
      second_y += expand_step_length;
      if (second_y <= die.get_grid_ur_y()) {
        PlanarCoord inflection_coord1(first_coord.get_x(), second_y);
        PlanarCoord inflection_coord2(second_coord.get_x(), second_y);
        inflection_list_list.push_back({inflection_coord1, inflection_coord2});
      }
    }
  }
  std::vector<std::vector<Segment<PlanarCoord>>> routing_segment_list_list;
  for (std::vector<PlanarCoord>& inflection_list : inflection_list_list) {
    std::vector<Segment<PlanarCoord>> routing_segment_list;
    routing_segment_list.emplace_back(planar_topo.get_first(), inflection_list.front());
    for (size_t i = 1; i < inflection_list.size(); i++) {
      routing_segment_list.emplace_back(inflection_list[i - 1], inflection_list[i]);
    }
    routing_segment_list.emplace_back(inflection_list.back(), planar_topo.get_second());
    routing_segment_list_list.push_back(routing_segment_list);
  }
  return routing_segment_list_list;
}

std::vector<std::vector<Segment<PlanarCoord>>> PlanarRouter::getRoutingSegmentListByInner3Bends(PRModel& pr_model, Segment<PlanarCoord>& planar_topo)
{
  PlanarCoord& first_coord = planar_topo.get_first();
  PlanarCoord& second_coord = planar_topo.get_second();
  if (RTUTIL.isRightAngled(first_coord, second_coord)) {
    return {};
  }
  std::vector<int32_t> x_mid_index_list = getMidIndexList(first_coord.get_x(), second_coord.get_x());
  std::vector<int32_t> y_mid_index_list = getMidIndexList(first_coord.get_y(), second_coord.get_y());
  if (x_mid_index_list.empty() || y_mid_index_list.empty()) {
    return {};
  }
  std::vector<std::vector<PlanarCoord>> inflection_list_list;
  for (size_t i = 0; i < x_mid_index_list.size(); i++) {
    for (size_t j = 0; j < y_mid_index_list.size(); j++) {
      PlanarCoord inflection_coord1(x_mid_index_list[i], first_coord.get_y());
      PlanarCoord inflection_coord2(x_mid_index_list[i], y_mid_index_list[j]);
      PlanarCoord inflection_coord3(second_coord.get_x(), y_mid_index_list[j]);
      inflection_list_list.push_back({inflection_coord1, inflection_coord2, inflection_coord3});
    }
  }

  for (size_t i = 0; i < x_mid_index_list.size(); i++) {
    for (size_t j = 0; j < y_mid_index_list.size(); j++) {
      PlanarCoord inflection_coord1(first_coord.get_x(), y_mid_index_list[j]);
      PlanarCoord inflection_coord2(x_mid_index_list[i], y_mid_index_list[j]);
      PlanarCoord inflection_coord3(x_mid_index_list[i], second_coord.get_y());
      inflection_list_list.push_back({inflection_coord1, inflection_coord2, inflection_coord3});
    }
  }
  std::vector<std::vector<Segment<PlanarCoord>>> routing_segment_list_list;
  for (std::vector<PlanarCoord>& inflection_list : inflection_list_list) {
    std::vector<Segment<PlanarCoord>> routing_segment_list;
    routing_segment_list.emplace_back(planar_topo.get_first(), inflection_list.front());
    for (size_t i = 1; i < inflection_list.size(); i++) {
      routing_segment_list.emplace_back(inflection_list[i - 1], inflection_list[i]);
    }
    routing_segment_list.emplace_back(inflection_list.back(), planar_topo.get_second());
    routing_segment_list_list.push_back(routing_segment_list);
  }
  return routing_segment_list_list;
}

std::vector<std::vector<Segment<PlanarCoord>>> PlanarRouter::getRoutingSegmentListByLowCostLane3Bends(
    PRModel& pr_model, Segment<PlanarCoord>& planar_topo,
    const std::set<PlanarCoord, CmpPlanarCoordByXASC>& terminal_coord_set,
    const PRShadowDemandMap* shadow_demand_map)
{
  constexpr int32_t kLowCostLaneTopK = 4;
  constexpr int32_t kLowCostLaneMaxLaneNum = 6;
  constexpr int32_t kLowCostLaneMaxScanNum = 128;

  PlanarCoord& first_coord = planar_topo.get_first();
  PlanarCoord& second_coord = planar_topo.get_second();
  if (RTUTIL.isRightAngled(first_coord, second_coord)) {
    return {};
  }

  int32_t min_x = std::min(first_coord.get_x(), second_coord.get_x());
  int32_t max_x = std::max(first_coord.get_x(), second_coord.get_x());
  int32_t min_y = std::min(first_coord.get_y(), second_coord.get_y());
  int32_t max_y = std::max(first_coord.get_y(), second_coord.get_y());
  if (max_x - min_x <= 1 || max_y - min_y <= 1) {
    return {};
  }

  auto makeSegmentScore = [&](const PlanarCoord& start_coord, const PlanarCoord& end_coord) {
    if (start_coord == end_coord) {
      return 0.0;
    }
    Segment<PlanarCoord> segment(start_coord, end_coord);
    return getPatternSegmentScore(pr_model, segment, terminal_coord_set, shadow_demand_map);
  };

  auto getSampledLaneList = [kLowCostLaneMaxScanNum](int32_t min_idx, int32_t max_idx) {
    std::vector<int32_t> lane_list;
    int32_t lane_num = max_idx - min_idx - 1;
    if (lane_num <= 0) {
      return lane_list;
    }
    int32_t scan_num = std::min(lane_num, kLowCostLaneMaxScanNum);
    lane_list.reserve(scan_num);
    for (int32_t i = 0; i < scan_num; i++) {
      int32_t lane_idx = min_idx + 1 + static_cast<int32_t>(std::llround((lane_num - 1) * (i / 1.0 / std::max(1, scan_num - 1))));
      if (lane_list.empty() || lane_list.back() != lane_idx) {
        lane_list.push_back(lane_idx);
      }
    }
    return lane_list;
  };

  auto appendQuantileLane = [](std::vector<int32_t>& lane_list, int32_t min_idx, int32_t max_idx, int32_t numerator, int32_t denominator) {
    int32_t lane_idx = min_idx + static_cast<int32_t>(std::llround((max_idx - min_idx) * (numerator / 1.0 / denominator)));
    if (min_idx < lane_idx && lane_idx < max_idx && !RTUTIL.exist(lane_list, lane_idx)) {
      lane_list.push_back(lane_idx);
    }
  };

  std::vector<std::pair<int32_t, double>> x_lane_score_list;
  for (int32_t x : getSampledLaneList(min_x, max_x)) {
    PlanarCoord vertical_start(x, min_y);
    PlanarCoord vertical_end(x, max_y);
    double score = 0;
    score += makeSegmentScore(first_coord, PlanarCoord(x, first_coord.get_y()));
    score += makeSegmentScore(vertical_start, vertical_end);
    score += makeSegmentScore(PlanarCoord(x, second_coord.get_y()), second_coord);
    x_lane_score_list.emplace_back(x, score);
  }
  std::sort(x_lane_score_list.begin(), x_lane_score_list.end(), [](auto& a, auto& b) {
    if (!RTUTIL.equalDoubleByError(a.second, b.second, RT_ERROR)) {
      return a.second < b.second;
    }
    return a.first < b.first;
  });

  std::vector<std::pair<int32_t, double>> y_lane_score_list;
  for (int32_t y : getSampledLaneList(min_y, max_y)) {
    PlanarCoord horizontal_start(min_x, y);
    PlanarCoord horizontal_end(max_x, y);
    double score = 0;
    score += makeSegmentScore(first_coord, PlanarCoord(first_coord.get_x(), y));
    score += makeSegmentScore(horizontal_start, horizontal_end);
    score += makeSegmentScore(PlanarCoord(second_coord.get_x(), y), second_coord);
    y_lane_score_list.emplace_back(y, score);
  }
  std::sort(y_lane_score_list.begin(), y_lane_score_list.end(), [](auto& a, auto& b) {
    if (!RTUTIL.equalDoubleByError(a.second, b.second, RT_ERROR)) {
      return a.second < b.second;
    }
    return a.first < b.first;
  });

  std::vector<int32_t> selected_x_lane_list;
  std::vector<int32_t> selected_y_lane_list;
  for (int32_t i = 0; i < std::min(kLowCostLaneTopK, static_cast<int32_t>(x_lane_score_list.size())); i++) {
    selected_x_lane_list.push_back(x_lane_score_list[i].first);
  }
  for (int32_t i = 0; i < std::min(kLowCostLaneTopK, static_cast<int32_t>(y_lane_score_list.size())); i++) {
    selected_y_lane_list.push_back(y_lane_score_list[i].first);
  }
  for (auto [numerator, denominator] : {std::pair<int32_t, int32_t>(1, 4), std::pair<int32_t, int32_t>(1, 2),
                                        std::pair<int32_t, int32_t>(3, 4)}) {
    if (static_cast<int32_t>(selected_x_lane_list.size()) < kLowCostLaneMaxLaneNum) {
      appendQuantileLane(selected_x_lane_list, min_x, max_x, numerator, denominator);
    }
    if (static_cast<int32_t>(selected_y_lane_list.size()) < kLowCostLaneMaxLaneNum) {
      appendQuantileLane(selected_y_lane_list, min_y, max_y, numerator, denominator);
    }
  }

  std::sort(selected_x_lane_list.begin(), selected_x_lane_list.end());
  selected_x_lane_list.erase(std::unique(selected_x_lane_list.begin(), selected_x_lane_list.end()), selected_x_lane_list.end());
  std::sort(selected_y_lane_list.begin(), selected_y_lane_list.end());
  selected_y_lane_list.erase(std::unique(selected_y_lane_list.begin(), selected_y_lane_list.end()), selected_y_lane_list.end());

  std::vector<std::vector<Segment<PlanarCoord>>> routing_segment_list_list;
  for (int32_t x : selected_x_lane_list) {
    for (int32_t y : selected_y_lane_list) {
      PlanarCoord horizontal_mid1(x, first_coord.get_y());
      PlanarCoord horizontal_mid2(x, y);
      PlanarCoord horizontal_mid3(second_coord.get_x(), y);
      routing_segment_list_list.push_back({Segment<PlanarCoord>(first_coord, horizontal_mid1),
                                           Segment<PlanarCoord>(horizontal_mid1, horizontal_mid2),
                                           Segment<PlanarCoord>(horizontal_mid2, horizontal_mid3),
                                           Segment<PlanarCoord>(horizontal_mid3, second_coord)});

      PlanarCoord vertical_mid1(first_coord.get_x(), y);
      PlanarCoord vertical_mid2(x, y);
      PlanarCoord vertical_mid3(x, second_coord.get_y());
      routing_segment_list_list.push_back({Segment<PlanarCoord>(first_coord, vertical_mid1),
                                           Segment<PlanarCoord>(vertical_mid1, vertical_mid2),
                                           Segment<PlanarCoord>(vertical_mid2, vertical_mid3),
                                           Segment<PlanarCoord>(vertical_mid3, second_coord)});
    }
  }
  return routing_segment_list_list;
}

double PlanarRouter::getPatternSegmentScore(PRModel& pr_model, Segment<PlanarCoord>& segment,
                                                 const std::set<PlanarCoord, CmpPlanarCoordByXASC>& terminal_coord_set,
                                                 const PRShadowDemandMap* shadow_demand_map)
{
  constexpr double kBlockedSegmentScore = DBL_MAX / 4;
  if (isMacroBlockedSegment(pr_model, segment, terminal_coord_set)) {
    return kBlockedSegmentScore;
  }
  return getPatternSegmentCost(pr_model, segment, shadow_demand_map);
}

double PlanarRouter::getPatternSegmentCost(PRModel& pr_model, Segment<PlanarCoord>& segment,
                                                const PRShadowDemandMap* shadow_demand_map)
{
  constexpr double kBlockedSegmentScore = DBL_MAX / 4;

  PlanarCoord& first_coord = segment.get_first();
  PlanarCoord& second_coord = segment.get_second();
  if (first_coord == second_coord) {
    return 0;
  }
  if (!RTUTIL.isRightAngled(first_coord, second_coord)) {
    RTLOG.error(Loc::current(), "The direction is error!");
  }

  GridMap<PRNode>& pr_node_map = pr_model.get_pr_node_map();
  double overflow_unit = pr_model.get_pr_com_param().get_overflow_unit();
  int32_t curr_net_idx = pr_model.get_curr_pr_task()->get_net_idx();

  int32_t first_x = first_coord.get_x();
  int32_t second_x = second_coord.get_x();
  int32_t first_y = first_coord.get_y();
  int32_t second_y = second_coord.get_y();
  RTUTIL.swapByASC(first_x, second_x);
  RTUTIL.swapByASC(first_y, second_y);

  double score = RTUTIL.getManhattanDistance(first_coord, second_coord);
  uint8_t direction_mask = getPRDirectionMask(RTUTIL.getDirection(first_coord, second_coord));
  for (int32_t x = first_x; x <= second_x; x++) {
    for (int32_t y = first_y; y <= second_y; y++) {
      if (!pr_node_map.isInside(x, y)) {
        return kBlockedSegmentScore;
      }
      uint8_t shadow_mask = getShadowOrientMask(shadow_demand_map, PlanarCoord(x, y));
      PRNodeCost node_cost = pr_node_map[x][y].getFastCost(curr_net_idx, direction_mask | shadow_mask, overflow_unit, false);
      score += node_cost.getTotalCost();
    }
  }
  return score;
}

std::vector<std::vector<Segment<PlanarCoord>>> PlanarRouter::getRoutingSegmentListByOuter3Bends(PRModel& pr_model, Segment<PlanarCoord>& planar_topo)
{
  Die& die = RTDM.getDatabase().get_die();
  int32_t expand_step_num = pr_model.get_pr_com_param().get_expand_step_num();
  int32_t expand_step_length = pr_model.get_pr_com_param().get_expand_step_length();

  PlanarCoord& first_coord = planar_topo.get_first();
  PlanarCoord& second_coord = planar_topo.get_second();
  if (RTUTIL.isRightAngled(first_coord, second_coord)) {
    return {};
  }
  int32_t start_x = first_coord.get_x();
  int32_t end_x = second_coord.get_x();
  int32_t start_y = first_coord.get_y();
  int32_t end_y = second_coord.get_y();

  int32_t box_lb_x = std::min(start_x, end_x);
  int32_t box_rt_x = std::max(start_x, end_x);
  int32_t box_lb_y = std::min(start_y, end_y);
  int32_t box_rt_y = std::max(start_y, end_y);

  std::vector<std::vector<PlanarCoord>> inflection_list_list;
  for (int32_t i = 0; i < expand_step_num; i++) {
    box_lb_x -= expand_step_length;
    box_rt_x += expand_step_length;
    box_lb_y -= expand_step_length;
    box_rt_y += expand_step_length;
    if (start_x < end_x) {
      if (start_y < end_y) {
        /**
         *    line style
         *
         *            x(e)
         *          x
         *        x
         *      x(s)
         *
         */
        if (die.get_grid_ll_y() <= box_lb_y && box_rt_x <= die.get_grid_ur_x()) {
          PlanarCoord inflection_coord1(start_x, box_lb_y);
          PlanarCoord inflection_coord2(box_rt_x, box_lb_y);
          PlanarCoord inflection_coord3(box_rt_x, end_y);
          inflection_list_list.push_back({inflection_coord1, inflection_coord2, inflection_coord3});
        }
        if (die.get_grid_ll_x() <= box_lb_x && box_rt_y <= die.get_grid_ur_y()) {
          PlanarCoord inflection_coord1(box_lb_x, start_y);
          PlanarCoord inflection_coord2(box_lb_x, box_rt_y);
          PlanarCoord inflection_coord3(end_x, box_rt_y);
          inflection_list_list.push_back({inflection_coord1, inflection_coord2, inflection_coord3});
        }
      } else {
        /**
         *    line style
         *
         *   x(s)
         *     x
         *       x
         *         x(e)
         *
         */
        if (box_rt_x <= die.get_grid_ur_x() && box_rt_y <= die.get_grid_ur_y()) {
          PlanarCoord inflection_coord1(start_x, box_rt_y);
          PlanarCoord inflection_coord2(box_rt_x, box_rt_y);
          PlanarCoord inflection_coord3(box_rt_x, end_y);
          inflection_list_list.push_back({inflection_coord1, inflection_coord2, inflection_coord3});
        }
        if (die.get_grid_ll_x() <= box_lb_x && die.get_grid_ll_y() <= box_lb_y) {
          PlanarCoord inflection_coord1(box_lb_x, start_y);
          PlanarCoord inflection_coord2(box_lb_x, box_lb_y);
          PlanarCoord inflection_coord3(end_x, box_lb_y);
          inflection_list_list.push_back({inflection_coord1, inflection_coord2, inflection_coord3});
        }
      }

    } else {
      if (start_y < end_y) {
        /**
         *    line style
         *
         *   x(e)
         *     x
         *       x
         *         x(s)
         *
         */
        if (box_rt_x <= die.get_grid_ur_x() && box_rt_y <= die.get_grid_ur_y()) {
          PlanarCoord inflection_coord1(box_rt_x, start_y);
          PlanarCoord inflection_coord2(box_rt_x, box_rt_y);
          PlanarCoord inflection_coord3(end_x, box_rt_y);
          inflection_list_list.push_back({inflection_coord1, inflection_coord2, inflection_coord3});
        }
        if (die.get_grid_ll_x() <= box_lb_x && die.get_grid_ll_y() <= box_lb_y) {
          PlanarCoord inflection_coord1(start_x, box_lb_y);
          PlanarCoord inflection_coord2(box_lb_x, box_lb_y);
          PlanarCoord inflection_coord3(box_lb_x, end_y);
          inflection_list_list.push_back({inflection_coord1, inflection_coord2, inflection_coord3});
        }
      } else {
        /**
         *    line style
         *
         *            x(s)
         *          x
         *        x
         *      x(e)
         *
         */
        if (die.get_grid_ll_y() <= box_lb_y && box_rt_x <= die.get_grid_ur_x()) {
          PlanarCoord inflection_coord1(box_rt_x, start_y);
          PlanarCoord inflection_coord2(box_rt_x, box_lb_y);
          PlanarCoord inflection_coord3(end_x, box_lb_y);
          inflection_list_list.push_back({inflection_coord1, inflection_coord2, inflection_coord3});
        }
        if (die.get_grid_ll_x() <= box_lb_x && box_rt_y <= die.get_grid_ur_y()) {
          PlanarCoord inflection_coord1(start_x, box_rt_y);
          PlanarCoord inflection_coord2(box_lb_x, box_rt_y);
          PlanarCoord inflection_coord3(box_lb_x, end_y);
          inflection_list_list.push_back({inflection_coord1, inflection_coord2, inflection_coord3});
        }
      }
    }
  }
  std::vector<std::vector<Segment<PlanarCoord>>> routing_segment_list_list;
  for (std::vector<PlanarCoord>& inflection_list : inflection_list_list) {
    std::vector<Segment<PlanarCoord>> routing_segment_list;
    routing_segment_list.emplace_back(planar_topo.get_first(), inflection_list.front());
    for (size_t i = 1; i < inflection_list.size(); i++) {
      routing_segment_list.emplace_back(inflection_list[i - 1], inflection_list[i]);
    }
    routing_segment_list.emplace_back(inflection_list.back(), planar_topo.get_second());
    routing_segment_list_list.push_back(routing_segment_list);
  }
  return routing_segment_list_list;
}

void PlanarRouter::updatePRCandidate(PRModel& pr_model, PRCandidate& pr_candidate,
                                         const PRShadowDemandMap* shadow_demand_map)
{
  double overflow_unit = pr_model.get_pr_com_param().get_overflow_unit();
  GridMap<PRNode>& pr_node_map = pr_model.get_pr_node_map();
  int32_t curr_net_idx = pr_model.get_curr_pr_task()->get_net_idx();

  PRCandidateCost candidate_cost;
  Direction pre_direction = Direction::kNone;
  for (Segment<PlanarCoord>& coord_segment : pr_candidate.get_routing_segment_list()) {
    PlanarCoord& first_coord = coord_segment.get_first();
    PlanarCoord& second_coord = coord_segment.get_second();
    if (!RTUTIL.isRightAngled(first_coord, second_coord)) {
      RTLOG.error(Loc::current(), "The direction is error!");
    }
    candidate_cost.total_wire_length += RTUTIL.getManhattanDistance(first_coord, second_coord);

    int32_t first_x = first_coord.get_x();
    int32_t second_x = second_coord.get_x();
    int32_t first_y = first_coord.get_y();
    int32_t second_y = second_coord.get_y();
    RTUTIL.swapByASC(first_x, second_x);
    RTUTIL.swapByASC(first_y, second_y);
    Direction direction = RTUTIL.getDirection(first_coord, second_coord);
    if (pre_direction != Direction::kNone && pre_direction != direction) {
      candidate_cost.total_corner_num++;
    }
    pre_direction = direction;
    uint8_t direction_mask = getPRDirectionMask(direction);
    for (int32_t x = first_x; x <= second_x; x++) {
      for (int32_t y = first_y; y <= second_y; y++) {
        uint8_t shadow_mask = getShadowOrientMask(shadow_demand_map, PlanarCoord(x, y));
        PRNodeCost node_cost = pr_node_map[x][y].getFastCost(curr_net_idx, direction_mask | shadow_mask, overflow_unit, false);
        if (node_cost.overflow > 0) {
          candidate_cost.is_path_blocked = true;
          candidate_cost.overflow_node_num++;
        }
        candidate_cost.total_usage_cost += node_cost.usage_cost;
        candidate_cost.total_saturation_cost += node_cost.saturation_cost;
        candidate_cost.total_hotspot_cost += node_cost.hotspot_cost;
        candidate_cost.total_overflow_cost += node_cost.overflow_cost;
        candidate_cost.total_overflow += node_cost.overflow;
        candidate_cost.max_usage_ratio = std::max(candidate_cost.max_usage_ratio, node_cost.max_usage_ratio);
        if (node_cost.saturation_orient_num > 0) {
          candidate_cost.saturation_node_num++;
        }
        if (node_cost.hotspot_orient_num > 0) {
          candidate_cost.hotspot_node_num++;
        }
      }
    }
  }

  pr_candidate.set_candidate_cost(candidate_cost);
}

MTree<PlanarCoord> PlanarRouter::getCoordTree(PRModel& pr_model, std::vector<Segment<PlanarCoord>>& routing_segment_list)
{
  std::vector<PlanarCoord> candidate_root_coord_list;
  std::map<PlanarCoord, std::set<int32_t>, CmpPlanarCoordByXASC> key_coord_pin_map;
  std::vector<PRPin>& pr_pin_list = pr_model.get_curr_pr_task()->get_pr_pin_list();
  for (size_t i = 0; i < pr_pin_list.size(); i++) {
    PlanarCoord coord = pr_pin_list[i].get_access_point().get_grid_coord();
    candidate_root_coord_list.push_back(coord);
    key_coord_pin_map[coord].insert(static_cast<int32_t>(i));
  }
  return RTUTIL.getTreeByFullFlow(candidate_root_coord_list, routing_segment_list, key_coord_pin_map);
}

void PlanarRouter::uploadNetResult(PRModel& pr_model, MTree<PlanarCoord>& coord_tree)
{
  for (Segment<TNode<PlanarCoord>*>& coord_segment : RTUTIL.getSegListByTree(coord_tree)) {
    Segment<LayerCoord>* segment = new Segment<LayerCoord>({coord_segment.get_first()->value(), 0}, {coord_segment.get_second()->value(), 0});
    RTDM.updateNetGlobalResultToGCellMap(ChangeType::kAdd, pr_model.get_curr_pr_task()->get_net_idx(), segment);
  }
}

void PlanarRouter::resetSingleTask(PRModel& pr_model)
{
  pr_model.set_curr_pr_task(nullptr);
}

#if 1  // update env

void PlanarRouter::updateDemandToGraph(PRModel& pr_model, ChangeType change_type, MTree<PlanarCoord>& coord_tree)
{
  int32_t curr_net_idx = pr_model.get_curr_pr_task()->get_net_idx();

  std::vector<Segment<PlanarCoord>> routing_segment_list;
  for (Segment<TNode<PlanarCoord>*>& coord_segment : RTUTIL.getSegListByTree(coord_tree)) {
    routing_segment_list.emplace_back(coord_segment.get_first()->value(), coord_segment.get_second()->value());
  }
  std::map<PlanarCoord, std::set<Orientation>, CmpPlanarCoordByXASC> usage_map;
  for (Segment<PlanarCoord>& coord_segment : routing_segment_list) {
    PlanarCoord& first_coord = coord_segment.get_first();
    PlanarCoord& second_coord = coord_segment.get_second();

    Orientation orientation = RTUTIL.getOrientation(first_coord, second_coord);
    if (orientation == Orientation::kNone || orientation == Orientation::kOblique) {
      RTLOG.error(Loc::current(), "The orientation is error!");
    }
    Orientation opposite_orientation = RTUTIL.getOppositeOrientation(orientation);

    int32_t first_x = first_coord.get_x();
    int32_t first_y = first_coord.get_y();
    int32_t second_x = second_coord.get_x();
    int32_t second_y = second_coord.get_y();
    RTUTIL.swapByASC(first_x, second_x);
    RTUTIL.swapByASC(first_y, second_y);

    for (int32_t x = first_x; x <= second_x; x++) {
      for (int32_t y = first_y; y <= second_y; y++) {
        PlanarCoord coord(x, y);
        if (coord != first_coord) {
          usage_map[coord].insert(opposite_orientation);
        }
        if (coord != second_coord) {
          usage_map[coord].insert(orientation);
        }
      }
    }
  }
  GridMap<PRNode>& pr_node_map = pr_model.get_pr_node_map();
  for (auto& [usage_coord, orientation_list] : usage_map) {
    PRNode& pr_node = pr_node_map[usage_coord.get_x()][usage_coord.get_y()];
    pr_node.updateDemand(curr_net_idx, orientation_list, change_type);
  }
}

void PlanarRouter::addCandidateToShadow(PRShadowDemandMap& shadow_map, PRCandidate& pr_candidate)
{
  for (Segment<PlanarCoord>& coord_segment : pr_candidate.get_routing_segment_list()) {
    PlanarCoord& first_coord = coord_segment.get_first();
    PlanarCoord& second_coord = coord_segment.get_second();

    Orientation orientation = RTUTIL.getOrientation(first_coord, second_coord);
    if (orientation == Orientation::kNone || orientation == Orientation::kOblique) {
      RTLOG.error(Loc::current(), "The orientation is error!");
    }
    Orientation opposite_orientation = RTUTIL.getOppositeOrientation(orientation);

    int32_t first_x = first_coord.get_x();
    int32_t first_y = first_coord.get_y();
    int32_t second_x = second_coord.get_x();
    int32_t second_y = second_coord.get_y();
    RTUTIL.swapByASC(first_x, second_x);
    RTUTIL.swapByASC(first_y, second_y);

    for (int32_t x = first_x; x <= second_x; x++) {
      for (int32_t y = first_y; y <= second_y; y++) {
        PlanarCoord coord(x, y);
        if (coord != first_coord) {
          shadow_map[coord].insert(opposite_orientation);
        }
        if (coord != second_coord) {
          shadow_map[coord].insert(orientation);
        }
      }
    }
  }
}

#endif

#if 1  // exhibit

void PlanarRouter::updateSummary(PRModel& pr_model)
{
  int32_t micron_dbu = RTDM.getDatabase().get_micron_dbu();
  ScaleAxis& gcell_axis = RTDM.getDatabase().get_gcell_axis();
  Die& die = RTDM.getDatabase().get_die();
  GridMap<GCell>& gcell_map = RTDM.getDatabase().get_gcell_map();
  Summary& summary = RTDM.getDatabase().get_summary();
  int32_t enable_timing = RTDM.getConfig().enable_timing;

  double& total_demand = summary.pr_summary.total_demand;
  double& total_overflow = summary.pr_summary.total_overflow;
  double& total_wire_length = summary.pr_summary.total_wire_length;
  std::map<std::string, std::map<std::string, double>>& clock_timing_map = summary.pr_summary.clock_timing_map;

  std::vector<PRNet>& pr_net_list = pr_model.get_pr_net_list();
  GridMap<PRNode>& pr_node_map = pr_model.get_pr_node_map();

  total_demand = 0;
  total_overflow = 0;
  total_wire_length = 0;
  clock_timing_map.clear();

  for (int32_t x = 0; x < pr_node_map.get_x_size(); x++) {
    for (int32_t y = 0; y < pr_node_map.get_y_size(); y++) {
      double node_demand = pr_node_map[x][y].getDemand();
      double node_overflow = pr_node_map[x][y].getOverflow();
      total_demand += node_demand;
      total_overflow += node_overflow;
    }
  }
  for (auto& [net_idx, segment_set] : RTDM.getNetGlobalResultMap(die)) {
    for (Segment<LayerCoord>* segment : segment_set) {
      LayerCoord& first_coord = segment->get_first();
      int32_t first_layer_idx = first_coord.get_layer_idx();
      LayerCoord& second_coord = segment->get_second();
      int32_t second_layer_idx = second_coord.get_layer_idx();

      if (first_layer_idx == second_layer_idx) {
        GCell& first_gcell = gcell_map[first_coord.get_x()][first_coord.get_y()];
        GCell& second_gcell = gcell_map[second_coord.get_x()][second_coord.get_y()];
        double wire_length = RTUTIL.getManhattanDistance(first_gcell.getMidPoint(), second_gcell.getMidPoint()) / 1.0 / micron_dbu;
        total_wire_length += wire_length;
      } else {
        RTLOG.error(Loc::current(), "first_layer_idx != second_layer_idx!");
      }
    }
  }
  if (enable_timing) {
    std::vector<std::map<std::string, std::vector<LayerCoord>>> real_pin_coord_map_list;
    real_pin_coord_map_list.resize(pr_net_list.size());
    std::vector<std::vector<Segment<LayerCoord>>> routing_segment_list_list;
    routing_segment_list_list.resize(pr_net_list.size());
    for (PRNet& pr_net : pr_net_list) {
      for (PRPin& pr_pin : pr_net.get_pr_pin_list()) {
        LayerCoord layer_coord = pr_pin.get_access_point().getGridLayerCoord();
        real_pin_coord_map_list[pr_net.get_net_idx()][pr_pin.get_pin_name()].emplace_back(RTUTIL.getRealRectByGCell(layer_coord, gcell_axis).getMidPoint(), 0);
      }
    }
    for (auto& [net_idx, segment_set] : RTDM.getNetGlobalResultMap(die)) {
      for (Segment<LayerCoord>* segment : segment_set) {
        LayerCoord first_layer_coord = segment->get_first();
        LayerCoord first_real_coord(RTUTIL.getRealRectByGCell(first_layer_coord, gcell_axis).getMidPoint(), first_layer_coord.get_layer_idx());
        LayerCoord second_layer_coord = segment->get_second();
        LayerCoord second_real_coord(RTUTIL.getRealRectByGCell(second_layer_coord, gcell_axis).getMidPoint(), second_layer_coord.get_layer_idx());

        routing_segment_list_list[net_idx].emplace_back(first_real_coord, second_real_coord);
      }
    }
    RTI.updateTiming(real_pin_coord_map_list, routing_segment_list_list, clock_timing_map);
  }
}

void PlanarRouter::printSummary(PRModel& pr_model)
{
  Summary& summary = RTDM.getDatabase().get_summary();
  int32_t enable_timing = RTDM.getConfig().enable_timing;

  double& total_demand = summary.pr_summary.total_demand;
  double& total_overflow = summary.pr_summary.total_overflow;
  double& total_wire_length = summary.pr_summary.total_wire_length;
  std::map<std::string, std::map<std::string, double>>& clock_timing_map = summary.pr_summary.clock_timing_map;

  fort::char_table summary_table;
  {
    summary_table.set_cell_text_align(fort::text_align::right);
    summary_table << fort::header << "total_demand" << total_demand << fort::endr;
    summary_table << fort::header << "total_overflow" << total_overflow << fort::endr;
    summary_table << fort::header << "total_wire_length" << total_wire_length << fort::endr;
  }
  fort::char_table timing_table;
  timing_table.set_cell_text_align(fort::text_align::right);
  if (enable_timing) {
    timing_table << fort::header << "clock_name"
                 << "tns"
                 << "wns"
                 << "freq" << fort::endr;
    for (auto& [clock_name, timing_map] : clock_timing_map) {
      timing_table << clock_name << timing_map["TNS"] << timing_map["WNS"] << timing_map["Freq(MHz)"] << fort::endr;
    }
  }
  RTUTIL.printTableList({summary_table});
  RTUTIL.printTableList({timing_table});
}

void PlanarRouter::outputGuide(PRModel& pr_model)
{
  int32_t micron_dbu = RTDM.getDatabase().get_micron_dbu();
  ScaleAxis& gcell_axis = RTDM.getDatabase().get_gcell_axis();
  Die& die = RTDM.getDatabase().get_die();
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();
  std::string& pr_temp_directory_path = RTDM.getConfig().pr_temp_directory_path;
  int32_t output_inter_result = RTDM.getConfig().output_inter_result;
  if (!output_inter_result) {
    return;
  }
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  std::vector<PRNet>& pr_net_list = pr_model.get_pr_net_list();

  std::ofstream* guide_file_stream = RTUTIL.getOutputFileStream(RTUTIL.getString(pr_temp_directory_path, "route.guide"));
  if (guide_file_stream == nullptr) {
    return;
  }
  RTUTIL.pushStream(guide_file_stream, "guide net_name\n");
  RTUTIL.pushStream(guide_file_stream, "pin grid_x grid_y real_x real_y layer energy name\n");
  RTUTIL.pushStream(guide_file_stream, "wire grid1_x grid1_y grid2_x grid2_y real1_x real1_y real2_x real2_y layer\n");
  RTUTIL.pushStream(guide_file_stream, "via grid_x grid_y real_x real_y layer1 layer2\n");

  for (auto& [net_idx, segment_set] : RTDM.getNetGlobalResultMap(die)) {
    PRNet& pr_net = pr_net_list[net_idx];
    RTUTIL.pushStream(guide_file_stream, "guide ", pr_net.get_origin_net()->get_net_name(), "\n");

    for (PRPin& pr_pin : pr_net.get_pr_pin_list()) {
      AccessPoint& access_point = pr_pin.get_access_point();
      double grid_x = access_point.get_grid_x();
      double grid_y = access_point.get_grid_y();
      double real_x = access_point.get_real_x() / 1.0 / micron_dbu;
      double real_y = access_point.get_real_y() / 1.0 / micron_dbu;
      std::string layer = routing_layer_list[access_point.get_layer_idx()].get_layer_name();
      std::string connnect;
      if (pr_pin.get_is_driven()) {
        connnect = "driven";
      } else {
        connnect = "load";
      }
      RTUTIL.pushStream(guide_file_stream, "pin ", grid_x, " ", grid_y, " ", real_x, " ", real_y, " ", layer, " ", connnect, " ", pr_pin.get_pin_name(), "\n");
    }
    for (Segment<LayerCoord>* segment : segment_set) {
      LayerCoord first_layer_coord = segment->get_first();
      double grid1_x = first_layer_coord.get_x();
      double grid1_y = first_layer_coord.get_y();
      int32_t first_layer_idx = first_layer_coord.get_layer_idx();

      PlanarCoord first_mid_coord = RTUTIL.getRealRectByGCell(first_layer_coord, gcell_axis).getMidPoint();
      double real1_x = first_mid_coord.get_x() / 1.0 / micron_dbu;
      double real1_y = first_mid_coord.get_y() / 1.0 / micron_dbu;

      LayerCoord second_layer_coord = segment->get_second();
      double grid2_x = second_layer_coord.get_x();
      double grid2_y = second_layer_coord.get_y();
      int32_t second_layer_idx = second_layer_coord.get_layer_idx();

      PlanarCoord second_mid_coord = RTUTIL.getRealRectByGCell(second_layer_coord, gcell_axis).getMidPoint();
      double real2_x = second_mid_coord.get_x() / 1.0 / micron_dbu;
      double real2_y = second_mid_coord.get_y() / 1.0 / micron_dbu;

      if (first_layer_idx != second_layer_idx) {
        RTUTIL.swapByASC(first_layer_idx, second_layer_idx);
        std::string layer1 = routing_layer_list[first_layer_idx].get_layer_name();
        std::string layer2 = routing_layer_list[second_layer_idx].get_layer_name();
        RTUTIL.pushStream(guide_file_stream, "via ", grid1_x, " ", grid1_y, " ", real1_x, " ", real1_y, " ", layer1, " ", layer2, "\n");
      } else {
        std::string layer = routing_layer_list[first_layer_idx].get_layer_name();
        RTUTIL.pushStream(guide_file_stream, "wire ", grid1_x, " ", grid1_y, " ", grid2_x, " ", grid2_y, " ", real1_x, " ", real1_y, " ", real2_x, " ", real2_y,
                          " ", layer, "\n");
      }
    }
  }
  RTUTIL.closeFileStream(guide_file_stream);
  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void PlanarRouter::outputNetCSV(PRModel& pr_model)
{
  std::string& pr_temp_directory_path = RTDM.getConfig().pr_temp_directory_path;
  int32_t output_inter_result = RTDM.getConfig().output_inter_result;
  if (!output_inter_result) {
    return;
  }
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  std::ofstream* net_csv_file = RTUTIL.getOutputFileStream(RTUTIL.getString(pr_temp_directory_path, "net_map.csv"));
  GridMap<PRNode>& pr_node_map = pr_model.get_pr_node_map();
  for (int32_t y = pr_node_map.get_y_size() - 1; y >= 0; y--) {
    for (int32_t x = 0; x < pr_node_map.get_x_size(); x++) {
      RTUTIL.pushStream(net_csv_file, pr_node_map[x][y].getDemand(), ",");
    }
    RTUTIL.pushStream(net_csv_file, "\n");
  }
  RTUTIL.closeFileStream(net_csv_file);
  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void PlanarRouter::outputOverflowCSV(PRModel& pr_model)
{
  std::string& pr_temp_directory_path = RTDM.getConfig().pr_temp_directory_path;
  int32_t output_inter_result = RTDM.getConfig().output_inter_result;
  if (!output_inter_result) {
    return;
  }
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  std::ofstream* overflow_csv_file = RTUTIL.getOutputFileStream(RTUTIL.getString(pr_temp_directory_path, "overflow_map.csv"));
  GridMap<PRNode>& pr_node_map = pr_model.get_pr_node_map();
  for (int32_t y = pr_node_map.get_y_size() - 1; y >= 0; y--) {
    for (int32_t x = 0; x < pr_node_map.get_x_size(); x++) {
      RTUTIL.pushStream(overflow_csv_file, pr_node_map[x][y].getOverflow(), ",");
    }
    RTUTIL.pushStream(overflow_csv_file, "\n");
  }
  RTUTIL.closeFileStream(overflow_csv_file);
  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void PlanarRouter::outputJson(PRModel& pr_model)
{
  int32_t enable_notification = RTDM.getConfig().enable_notification;
  if (!enable_notification) {
    return;
  }
  std::map<std::string, std::string> json_path_map;
  json_path_map["net_map"] = outputNetJson(pr_model);
  json_path_map["overflow_map"] = outputOverflowJson(pr_model);
  json_path_map["summary"] = outputSummaryJson(pr_model);
  RTI.sendNotification("PR", 1, json_path_map);
}

std::string PlanarRouter::outputNetJson(PRModel& pr_model)
{
  Die& die = RTDM.getDatabase().get_die();
  ScaleAxis& gcell_axis = RTDM.getDatabase().get_gcell_axis();
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();
  std::vector<Net>& net_list = RTDM.getDatabase().get_net_list();
  std::string& pr_temp_directory_path = RTDM.getConfig().pr_temp_directory_path;

  std::vector<nlohmann::json> net_json_list;
  {
    nlohmann::json result_shape_json;
    for (auto& [net_idx, segment_set] : RTDM.getNetGlobalResultMap(die)) {
      std::string net_name = net_list[net_idx].get_net_name();
      for (Segment<LayerCoord>* segment : segment_set) {
        PlanarRect first_gcell = RTUTIL.getRealRectByGCell(segment->get_first(), gcell_axis);
        PlanarRect second_gcell = RTUTIL.getRealRectByGCell(segment->get_second(), gcell_axis);
        if (segment->get_first().get_layer_idx() != segment->get_second().get_layer_idx()) {
          result_shape_json["result_shape"][net_name]["path"].push_back({first_gcell.get_ll_x(), first_gcell.get_ll_y(), first_gcell.get_ur_x(),
                                                                         first_gcell.get_ur_y(),
                                                                         routing_layer_list[segment->get_first().get_layer_idx()].get_layer_name()});
          result_shape_json["result_shape"][net_name]["path"].push_back({second_gcell.get_ll_x(), second_gcell.get_ll_y(), second_gcell.get_ur_x(),
                                                                         second_gcell.get_ur_y(),
                                                                         routing_layer_list[segment->get_second().get_layer_idx()].get_layer_name()});
        } else {
          PlanarRect gcell = RTUTIL.getBoundingBox({first_gcell, second_gcell});
          result_shape_json["result_shape"][net_name]["path"].push_back({gcell.get_ll_x(), gcell.get_ll_y(), gcell.get_ur_x(), gcell.get_ur_y(),
                                                                         routing_layer_list[segment->get_first().get_layer_idx()].get_layer_name()});
        }
      }
    }
    net_json_list.push_back(result_shape_json);
  }
  std::string net_json_file_path = RTUTIL.getString(pr_temp_directory_path, "net_map.json");
  std::ofstream* net_json_file = RTUTIL.getOutputFileStream(net_json_file_path);
  (*net_json_file) << net_json_list;
  RTUTIL.closeFileStream(net_json_file);
  return net_json_file_path;
}

std::string PlanarRouter::outputOverflowJson(PRModel& pr_model)
{
  ScaleAxis& gcell_axis = RTDM.getDatabase().get_gcell_axis();
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();
  std::string& pr_temp_directory_path = RTDM.getConfig().pr_temp_directory_path;

  GridMap<PRNode>& pr_node_map = pr_model.get_pr_node_map();
  std::vector<nlohmann::json> overflow_json_list;
  for (int32_t x = 0; x < pr_node_map.get_x_size(); x++) {
    for (int32_t y = 0; y < pr_node_map.get_y_size(); y++) {
      PlanarRect gcell = RTUTIL.getRealRectByGCell(PlanarCoord(x, y), gcell_axis);
      overflow_json_list.push_back(
          {gcell.get_ll_x(), gcell.get_ll_y(), gcell.get_ur_x(), gcell.get_ur_y(), routing_layer_list[0].get_layer_name(), pr_node_map[x][y].getOverflow()});
    }
  }
  std::string overflow_json_file_path = RTUTIL.getString(pr_temp_directory_path, "overflow_map.json");
  std::ofstream* overflow_json_file = RTUTIL.getOutputFileStream(overflow_json_file_path);
  (*overflow_json_file) << overflow_json_list;
  RTUTIL.closeFileStream(overflow_json_file);
  return overflow_json_file_path;
}

std::string PlanarRouter::outputSummaryJson(PRModel& pr_model)
{
  Summary& summary = RTDM.getDatabase().get_summary();
  std::string& pr_temp_directory_path = RTDM.getConfig().pr_temp_directory_path;

  double& total_demand = summary.pr_summary.total_demand;
  double& total_overflow = summary.pr_summary.total_overflow;
  double& total_wire_length = summary.pr_summary.total_wire_length;
  std::map<std::string, std::map<std::string, double>>& clock_timing_map = summary.pr_summary.clock_timing_map;

  nlohmann::json summary_json;
  summary_json["total_demand"] = total_demand;
  summary_json["total_overflow"] = total_overflow;
  summary_json["total_wire_length"] = total_wire_length;
  for (auto& [clock_name, timing] : clock_timing_map) {
    summary_json["clock_timing_map"]["clock_name"] = clock_name;
    summary_json["clock_timing_map"]["timing"] = timing;
  }
  std::string summary_json_file_path = RTUTIL.getString(pr_temp_directory_path, "summary.json");
  std::ofstream* summary_json_file = RTUTIL.getOutputFileStream(summary_json_file_path);
  (*summary_json_file) << summary_json;
  RTUTIL.closeFileStream(summary_json_file);
  return summary_json_file_path;
}

#endif

#if 1  // debug

void PlanarRouter::debugPlotPRModel(PRModel& pr_model, std::string flag)
{
  ScaleAxis& gcell_axis = RTDM.getDatabase().get_gcell_axis();
  Die& die = RTDM.getDatabase().get_die();
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();
  std::string& pr_temp_directory_path = RTDM.getConfig().pr_temp_directory_path;

  int32_t point_size = 5;

  GPGDS gp_gds;

  // base_region
  {
    GPStruct base_region_struct("base_region");
    GPBoundary gp_boundary;
    gp_boundary.set_layer_idx(0);
    gp_boundary.set_data_type(0);
    gp_boundary.set_rect(die.get_real_rect());
    base_region_struct.push(gp_boundary);
    gp_gds.addStruct(base_region_struct);
  }

  // gcell_axis
  {
    GPStruct gcell_axis_struct("gcell_axis");
    std::vector<int32_t> gcell_x_list = RTUTIL.getScaleList(die.get_real_ll_x(), die.get_real_ur_x(), gcell_axis.get_x_grid_list());
    std::vector<int32_t> gcell_y_list = RTUTIL.getScaleList(die.get_real_ll_y(), die.get_real_ur_y(), gcell_axis.get_y_grid_list());
    for (int32_t x : gcell_x_list) {
      GPPath gp_path;
      gp_path.set_layer_idx(0);
      gp_path.set_data_type(1);
      gp_path.set_segment(x, die.get_real_ll_y(), x, die.get_real_ur_y());
      gcell_axis_struct.push(gp_path);
    }
    for (int32_t y : gcell_y_list) {
      GPPath gp_path;
      gp_path.set_layer_idx(0);
      gp_path.set_data_type(1);
      gp_path.set_segment(die.get_real_ll_x(), y, die.get_real_ur_x(), y);
      gcell_axis_struct.push(gp_path);
    }
    gp_gds.addStruct(gcell_axis_struct);
  }

  // track_axis_struct
  {
    GPStruct track_axis_struct("track_axis_struct");
    for (RoutingLayer& routing_layer : routing_layer_list) {
      std::vector<int32_t> x_list = RTUTIL.getScaleList(die.get_real_ll_x(), die.get_real_ur_x(), routing_layer.getXTrackGridList());
      std::vector<int32_t> y_list = RTUTIL.getScaleList(die.get_real_ll_y(), die.get_real_ur_y(), routing_layer.getYTrackGridList());
      for (int32_t x : x_list) {
        GPPath gp_path;
        gp_path.set_data_type(static_cast<int32_t>(GPDataType::kAxis));
        gp_path.set_segment(x, die.get_real_ll_y(), x, die.get_real_ur_y());
        gp_path.set_layer_idx(RTGP.getGDSIdxByRouting(routing_layer.get_layer_idx()));
        track_axis_struct.push(gp_path);
      }
      for (int32_t y : y_list) {
        GPPath gp_path;
        gp_path.set_data_type(static_cast<int32_t>(GPDataType::kAxis));
        gp_path.set_segment(die.get_real_ll_x(), y, die.get_real_ur_x(), y);
        gp_path.set_layer_idx(RTGP.getGDSIdxByRouting(routing_layer.get_layer_idx()));
        track_axis_struct.push(gp_path);
      }
    }
    gp_gds.addStruct(track_axis_struct);
  }

  // fixed_rect
  for (auto& [is_routing, layer_net_rect_map] : RTDM.getTypeLayerNetFixedRectMap(die)) {
    for (auto& [layer_idx, net_rect_map] : layer_net_rect_map) {
      for (auto& [net_idx, rect_set] : net_rect_map) {
        GPStruct fixed_rect_struct(RTUTIL.getString("fixed_rect(net_", net_idx, ")"));
        for (EXTLayerRect* rect : rect_set) {
          GPBoundary gp_boundary;
          gp_boundary.set_data_type(static_cast<int32_t>(GPDataType::kShape));
          gp_boundary.set_rect(rect->get_real_rect());
          if (is_routing) {
            gp_boundary.set_layer_idx(RTGP.getGDSIdxByRouting(layer_idx));
          } else {
            gp_boundary.set_layer_idx(RTGP.getGDSIdxByCut(layer_idx));
          }
          fixed_rect_struct.push(gp_boundary);
        }
        gp_gds.addStruct(fixed_rect_struct);
      }
    }
  }

  // access_point
  for (auto& [net_idx, access_point_set] : RTDM.getNetAccessPointMap(die)) {
    GPStruct access_point_struct(RTUTIL.getString("access_point(net_", net_idx, ")"));
    for (AccessPoint* access_point : access_point_set) {
      int32_t x = access_point->get_real_x();
      int32_t y = access_point->get_real_y();

      GPBoundary access_point_boundary;
      access_point_boundary.set_layer_idx(RTGP.getGDSIdxByRouting(access_point->get_layer_idx()));
      access_point_boundary.set_data_type(static_cast<int32_t>(GPDataType::kAccessPoint));
      access_point_boundary.set_rect(x - point_size, y - point_size, x + point_size, y + point_size);
      access_point_struct.push(access_point_boundary);
    }
    gp_gds.addStruct(access_point_struct);
  }

  // routing result
  for (auto& [net_idx, segment_set] : RTDM.getNetGlobalResultMap(die)) {
    GPStruct global_result_struct(RTUTIL.getString("global_result(net_", net_idx, ")"));
    for (Segment<LayerCoord>* segment : segment_set) {
      for (NetShape& net_shape : RTDM.getNetGlobalShapeList(net_idx, *segment)) {
        GPBoundary gp_boundary;
        gp_boundary.set_data_type(static_cast<int32_t>(GPDataType::kGlobalPath));
        gp_boundary.set_rect(net_shape.get_rect());
        if (net_shape.get_is_routing()) {
          gp_boundary.set_layer_idx(RTGP.getGDSIdxByRouting(net_shape.get_layer_idx()));
        } else {
          gp_boundary.set_layer_idx(RTGP.getGDSIdxByCut(net_shape.get_layer_idx()));
        }
        global_result_struct.push(gp_boundary);
      }
    }
    gp_gds.addStruct(global_result_struct);
  }

  // routing result
  for (auto& [net_idx, segment_set] : RTDM.getNetDetailedResultMap(die)) {
    GPStruct detailed_result_struct(RTUTIL.getString("detailed_result(net_", net_idx, ")"));
    for (Segment<LayerCoord>* segment : segment_set) {
      for (NetShape& net_shape : RTDM.getNetDetailedShapeList(net_idx, *segment)) {
        GPBoundary gp_boundary;
        gp_boundary.set_data_type(static_cast<int32_t>(GPDataType::kShape));
        gp_boundary.set_rect(net_shape.get_rect());
        if (net_shape.get_is_routing()) {
          gp_boundary.set_layer_idx(RTGP.getGDSIdxByRouting(net_shape.get_layer_idx()));
        } else {
          gp_boundary.set_layer_idx(RTGP.getGDSIdxByCut(net_shape.get_layer_idx()));
        }
        detailed_result_struct.push(gp_boundary);
      }
    }
    gp_gds.addStruct(detailed_result_struct);
  }

  // routing patch
  for (auto& [net_idx, patch_set] : RTDM.getNetDetailedPatchMap(die)) {
    GPStruct detailed_patch_struct(RTUTIL.getString("detailed_patch(net_", net_idx, ")"));
    for (EXTLayerRect* patch : patch_set) {
      GPBoundary gp_boundary;
      gp_boundary.set_data_type(static_cast<int32_t>(GPDataType::kShape));
      gp_boundary.set_rect(patch->get_real_rect());
      gp_boundary.set_layer_idx(RTGP.getGDSIdxByRouting(patch->get_layer_idx()));
      detailed_patch_struct.push(gp_boundary);
    }
    gp_gds.addStruct(detailed_patch_struct);
  }

  {
    GridMap<PRNode>& pr_node_map = pr_model.get_pr_node_map();
    // pr_node_map
    {
      GPStruct pr_node_map_struct("pr_node_map");
      for (int32_t grid_x = 0; grid_x < pr_node_map.get_x_size(); grid_x++) {
        for (int32_t grid_y = 0; grid_y < pr_node_map.get_y_size(); grid_y++) {
          PRNode& pr_node = pr_node_map[grid_x][grid_y];
          PlanarRect real_rect = RTUTIL.getRealRectByGCell(pr_node, gcell_axis);
          int32_t y_reduced_span = std::max(1, real_rect.getYSpan() / 12);
          int32_t y = real_rect.get_ur_y();

          y -= y_reduced_span;
          GPText gp_text_node_real_coord;
          gp_text_node_real_coord.set_coord(real_rect.get_ll_x(), y);
          gp_text_node_real_coord.set_text_type(static_cast<int32_t>(GPDataType::kInfo));
          gp_text_node_real_coord.set_message(RTUTIL.getString("(", pr_node.get_x(), " , ", pr_node.get_y(), " , ", 0, ")"));
          gp_text_node_real_coord.set_layer_idx(RTGP.getGDSIdxByRouting(0));
          gp_text_node_real_coord.set_presentation(GPTextPresentation::kLeftMiddle);
          pr_node_map_struct.push(gp_text_node_real_coord);

          y -= y_reduced_span;
          GPText gp_text_node_grid_coord;
          gp_text_node_grid_coord.set_coord(real_rect.get_ll_x(), y);
          gp_text_node_grid_coord.set_text_type(static_cast<int32_t>(GPDataType::kInfo));
          gp_text_node_grid_coord.set_message(RTUTIL.getString("(", grid_x, " , ", grid_y, " , ", 0, ")"));
          gp_text_node_grid_coord.set_layer_idx(RTGP.getGDSIdxByRouting(0));
          gp_text_node_grid_coord.set_presentation(GPTextPresentation::kLeftMiddle);
          pr_node_map_struct.push(gp_text_node_grid_coord);

          y -= y_reduced_span;
          GPText gp_text_orient_supply_map;
          gp_text_orient_supply_map.set_coord(real_rect.get_ll_x(), y);
          gp_text_orient_supply_map.set_text_type(static_cast<int32_t>(GPDataType::kInfo));
          gp_text_orient_supply_map.set_message("orient_supply_map: ");
          gp_text_orient_supply_map.set_layer_idx(RTGP.getGDSIdxByRouting(0));
          gp_text_orient_supply_map.set_presentation(GPTextPresentation::kLeftMiddle);
          pr_node_map_struct.push(gp_text_orient_supply_map);

          if (!pr_node.get_orient_supply_map().empty()) {
            y -= y_reduced_span;
            GPText gp_text_orient_supply_map_info;
            gp_text_orient_supply_map_info.set_coord(real_rect.get_ll_x(), y);
            gp_text_orient_supply_map_info.set_text_type(static_cast<int32_t>(GPDataType::kInfo));
            std::string orient_supply_map_info_message = "--";
            for (auto& [orient, supply] : pr_node.get_orient_supply_map()) {
              orient_supply_map_info_message += RTUTIL.getString("(", GetOrientationName()(orient), ",", supply, ")");
            }
            gp_text_orient_supply_map_info.set_message(orient_supply_map_info_message);
            gp_text_orient_supply_map_info.set_layer_idx(RTGP.getGDSIdxByRouting(0));
            gp_text_orient_supply_map_info.set_presentation(GPTextPresentation::kLeftMiddle);
            pr_node_map_struct.push(gp_text_orient_supply_map_info);
          }

          y -= y_reduced_span;
          GPText gp_text_ignore_net_orient_map;
          gp_text_ignore_net_orient_map.set_coord(real_rect.get_ll_x(), y);
          gp_text_ignore_net_orient_map.set_text_type(static_cast<int32_t>(GPDataType::kInfo));
          gp_text_ignore_net_orient_map.set_message("ignore_net_orient_map: ");
          gp_text_ignore_net_orient_map.set_layer_idx(RTGP.getGDSIdxByRouting(0));
          gp_text_ignore_net_orient_map.set_presentation(GPTextPresentation::kLeftMiddle);
          pr_node_map_struct.push(gp_text_ignore_net_orient_map);

          if (!pr_node.get_ignore_net_orient_map().empty()) {
            y -= y_reduced_span;
            GPText gp_text_ignore_net_orient_map_info;
            gp_text_ignore_net_orient_map_info.set_coord(real_rect.get_ll_x(), y);
            gp_text_ignore_net_orient_map_info.set_text_type(static_cast<int32_t>(GPDataType::kInfo));
            std::string ignore_net_orient_map_info_message = "--";
            for (auto& [net_idx, orient_set] : pr_node.get_ignore_net_orient_map()) {
              ignore_net_orient_map_info_message += RTUTIL.getString("(", net_idx);
              for (Orientation orient : orient_set) {
                ignore_net_orient_map_info_message += RTUTIL.getString(",", GetOrientationName()(orient));
              }
              ignore_net_orient_map_info_message += RTUTIL.getString(")");
            }
            gp_text_ignore_net_orient_map_info.set_message(ignore_net_orient_map_info_message);
            gp_text_ignore_net_orient_map_info.set_layer_idx(RTGP.getGDSIdxByRouting(0));
            gp_text_ignore_net_orient_map_info.set_presentation(GPTextPresentation::kLeftMiddle);
            pr_node_map_struct.push(gp_text_ignore_net_orient_map_info);
          }

          y -= y_reduced_span;
          GPText gp_text_orient_net_map;
          gp_text_orient_net_map.set_coord(real_rect.get_ll_x(), y);
          gp_text_orient_net_map.set_text_type(static_cast<int32_t>(GPDataType::kInfo));
          gp_text_orient_net_map.set_message("orient_net_map: ");
          gp_text_orient_net_map.set_layer_idx(RTGP.getGDSIdxByRouting(0));
          gp_text_orient_net_map.set_presentation(GPTextPresentation::kLeftMiddle);
          pr_node_map_struct.push(gp_text_orient_net_map);

          if (!pr_node.get_orient_net_map().empty()) {
            y -= y_reduced_span;
            GPText gp_text_orient_net_map_info;
            gp_text_orient_net_map_info.set_coord(real_rect.get_ll_x(), y);
            gp_text_orient_net_map_info.set_text_type(static_cast<int32_t>(GPDataType::kInfo));
            std::string orient_net_map_info_message = "--";
            for (auto& [orient, net_set] : pr_node.get_orient_net_map()) {
              orient_net_map_info_message += RTUTIL.getString("(", GetOrientationName()(orient));
              for (int32_t net_idx : net_set) {
                orient_net_map_info_message += RTUTIL.getString(",", net_idx);
              }
              orient_net_map_info_message += RTUTIL.getString(")");
            }
            gp_text_orient_net_map_info.set_message(orient_net_map_info_message);
            gp_text_orient_net_map_info.set_layer_idx(RTGP.getGDSIdxByRouting(0));
            gp_text_orient_net_map_info.set_presentation(GPTextPresentation::kLeftMiddle);
            pr_node_map_struct.push(gp_text_orient_net_map_info);
          }

          y -= y_reduced_span;
          GPText gp_text_net_orient_map;
          gp_text_net_orient_map.set_coord(real_rect.get_ll_x(), y);
          gp_text_net_orient_map.set_text_type(static_cast<int32_t>(GPDataType::kInfo));
          gp_text_net_orient_map.set_message("net_orient_map: ");
          gp_text_net_orient_map.set_layer_idx(RTGP.getGDSIdxByRouting(0));
          gp_text_net_orient_map.set_presentation(GPTextPresentation::kLeftMiddle);
          pr_node_map_struct.push(gp_text_net_orient_map);

          if (!pr_node.get_net_orient_map().empty()) {
            y -= y_reduced_span;
            GPText gp_text_net_orient_map_info;
            gp_text_net_orient_map_info.set_coord(real_rect.get_ll_x(), y);
            gp_text_net_orient_map_info.set_text_type(static_cast<int32_t>(GPDataType::kInfo));
            std::string net_orient_map_info_message = "--";
            for (auto& [net_idx, orient_set] : pr_node.get_net_orient_map()) {
              net_orient_map_info_message += RTUTIL.getString("(", net_idx);
              for (Orientation orient : orient_set) {
                net_orient_map_info_message += RTUTIL.getString(",", GetOrientationName()(orient));
              }
              net_orient_map_info_message += RTUTIL.getString(")");
            }
            gp_text_net_orient_map_info.set_message(net_orient_map_info_message);
            gp_text_net_orient_map_info.set_layer_idx(RTGP.getGDSIdxByRouting(0));
            gp_text_net_orient_map_info.set_presentation(GPTextPresentation::kLeftMiddle);
            pr_node_map_struct.push(gp_text_net_orient_map_info);
          }

          y -= y_reduced_span;
          GPText gp_text_overflow;
          gp_text_overflow.set_coord(real_rect.get_ll_x(), y);
          gp_text_overflow.set_text_type(static_cast<int32_t>(GPDataType::kInfo));
          gp_text_overflow.set_message(RTUTIL.getString("overflow: ", pr_node.getOverflow()));
          gp_text_overflow.set_layer_idx(RTGP.getGDSIdxByRouting(0));
          gp_text_overflow.set_presentation(GPTextPresentation::kLeftMiddle);
          pr_node_map_struct.push(gp_text_overflow);
        }
      }
      gp_gds.addStruct(pr_node_map_struct);
    }
    // overflow
    {
      GPStruct overflow_struct("overflow");
      for (int32_t grid_x = 0; grid_x < pr_node_map.get_x_size(); grid_x++) {
        for (int32_t grid_y = 0; grid_y < pr_node_map.get_y_size(); grid_y++) {
          PRNode& pr_node = pr_node_map[grid_x][grid_y];
          if (pr_node.getOverflow() <= 0) {
            continue;
          }
          PlanarRect real_rect = RTUTIL.getRealRectByGCell(pr_node, gcell_axis);

          GPBoundary gp_boundary;
          gp_boundary.set_data_type(static_cast<int32_t>(GPDataType::kOverflow));
          gp_boundary.set_rect(real_rect);
          gp_boundary.set_layer_idx(RTGP.getGDSIdxByRouting(0));
          overflow_struct.push(gp_boundary);
        }
      }
      gp_gds.addStruct(overflow_struct);
    }
  }

  std::string gds_file_path = RTUTIL.getString(pr_temp_directory_path, flag, "_pr_model.gds");
  RTGP.plot(gp_gds, gds_file_path);
}

void PlanarRouter::debugCheckPRModel(PRModel& pr_model)
{
  GridMap<PRNode>& pr_node_map = pr_model.get_pr_node_map();
  for (int32_t x = 0; x < pr_node_map.get_x_size(); x++) {
    for (int32_t y = 0; y < pr_node_map.get_y_size(); y++) {
      PRNode& pr_node = pr_node_map[x][y];
      for (auto& [orient, neighbor] : pr_node.get_neighbor_node_map()) {
        Orientation opposite_orient = RTUTIL.getOppositeOrientation(orient);
        if (!RTUTIL.exist(neighbor->get_neighbor_node_map(), opposite_orient)) {
          RTLOG.error(Loc::current(), "The pr_node neighbor is not bidirectional!");
        }
        if (neighbor->get_neighbor_node_map()[opposite_orient] != &pr_node) {
          RTLOG.error(Loc::current(), "The pr_node neighbor is not bidirectional!");
        }
        if (RTUTIL.getOrientation(PlanarCoord(pr_node), PlanarCoord(*neighbor)) == orient) {
          continue;
        }
        RTLOG.error(Loc::current(), "The neighbor orient is different with real region!");
      }
    }
  }
}

#endif

}  // namespace irt
