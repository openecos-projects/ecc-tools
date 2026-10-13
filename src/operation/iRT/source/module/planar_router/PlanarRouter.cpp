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

#include "GDSPlotter.hpp"
#include "PRCandidate.hpp"
#include "PRTopologyCostCache.hpp"
#include "RTHeader.hpp"
#include "RTInterface.hpp"
#include "TBTask.hpp"
#include "TOPOBuilder.hpp"
#include "Utility.hpp"

namespace irt {

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

  initPRNetList();
  initPRGridGraph();
  setPRComParam();
  initPRTaskList();

  runRouteFlow();
  // debugPlotPRGraph("after");

  updateSummary();
  printSummary();
  outputGuide();
  outputNetCSV();
  outputOverflowCSV();
  // outputUsageCSV();
  // outputCongestionCostCSV();

  RTDM.rebuildGlobalResultRTree();
  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

// private

PlanarRouter* PlanarRouter::_pr_instance = nullptr;

void PlanarRouter::initPRNetList()
{
  std::vector<Net>& net_list = RTDM.getDatabase().get_net_list();

  _pr_net_list.clear();
  _pr_task_list.clear();
  _pr_net_list.reserve(net_list.size());
  for (Net& net : net_list) {
    PRNet& pr_net = _pr_net_list.emplace_back();
    pr_net.set_origin_net(&net);

    std::vector<PlanarCoord> node_coord_list;
    for (Pin& pin : net.get_pin_list()) {
      node_coord_list.push_back(pin.get_access_point().get_grid_coord());
    }
    std::ranges::sort(node_coord_list, CmpPlanarCoordByXASC());
    node_coord_list.erase(std::ranges::unique(node_coord_list).begin(), node_coord_list.end());
    pr_net.set_node_coord_list(std::move(node_coord_list));
  }
}

void PlanarRouter::initPRGridGraph()
{
  _grid_graph.build(RTDM.getDatabase());
}

void PlanarRouter::setPRComParam()
{
  int32_t topo_spilt_length = 100;
  int32_t expand_step_num = 30;
  int32_t astar_search_margin = 30;
  double overflow_unit = 20;
  double corner_weight = 2.5;
  /**
   * topo_spilt_length, expand_step_num, astar_search_margin, overflow_unit, corner_weight
   */

  PRComParam pr_com_param(topo_spilt_length, expand_step_num, astar_search_margin, overflow_unit, corner_weight);
  RTLOG.info(Loc::current(), "topo_spilt_length: ", pr_com_param.get_topo_spilt_length());
  RTLOG.info(Loc::current(), "expand_step_num: ", pr_com_param.get_expand_step_num());
  RTLOG.info(Loc::current(), "astar_search_margin: ", pr_com_param.get_astar_search_margin());
  RTLOG.info(Loc::current(), "overflow_unit: ", pr_com_param.get_overflow_unit());
  RTLOG.info(Loc::current(), "corner_weight: ", pr_com_param.get_corner_weight());
  _pr_com_param = pr_com_param;
}

void PlanarRouter::initPRTaskList()
{
  RegionRoute& region_route = RTDM.getDatabase().get_region_route();
  _pr_task_list.reserve(_pr_net_list.size());
  for (PRNet& pr_net : _pr_net_list) {
    if (!region_route.isActiveNet(pr_net.get_net_idx())) {
      continue;
    }
    _pr_task_list.push_back(&pr_net);
  }
  std::ranges::sort(_pr_task_list, CmpPRNet());
}

void PlanarRouter::runRouteFlow()
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  constexpr int32_t max_astar_iters = 5;

  routePRNetList(_pr_task_list, {"initial LZ pattern", PRRouteMode::kInitialLZPattern});
  _grid_graph.update_congestion(_pr_com_param.get_overflow_unit());
  routePRNetList(_pr_task_list, {"congestion LZ pattern", PRRouteMode::kCongestionLZPattern});
  _grid_graph.update_congestion(_pr_com_param.get_overflow_unit());
  routePRNetList(getOverflowPRNetList(), {"overflow All pattern", PRRouteMode::kAllPattern});
  _grid_graph.update_congestion(_pr_com_param.get_overflow_unit());

  for (int32_t iter = 0; iter < max_astar_iters; iter++) {
    std::vector<PRNet*> reroute_net_list = getOverflowPRNetList();
    if (reroute_net_list.empty()) {
      break;
    }

    routePRNetList(reroute_net_list, {"overflow A*", PRRouteMode::kAStar});
    _grid_graph.update_congestion(_pr_com_param.get_overflow_unit());
    _pr_com_param.set_astar_search_margin(_pr_com_param.get_astar_search_margin() * 2);
    _pr_com_param.set_overflow_unit(_pr_com_param.get_overflow_unit() * 2);
  }

  uploadNetList();

  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

std::vector<PRNet*> PlanarRouter::getOverflowPRNetList()
{
  std::vector<PRNet*> pr_net_list;
  for (PRNet& pr_net : _pr_net_list) {
    for (PREdgeId edge_id : pr_net.get_routing_edge_set()) {
      const PRGridEdge& edge = _grid_graph.get_edge(edge_id);
      if (edge.ignore_net_set.contains(pr_net.get_net_idx())) {
        continue;
      }
      if (edge.get_overflow() > 0) {
        pr_net_list.push_back(&pr_net);
        break;
      }
    }
  }
  return pr_net_list;
}

void PlanarRouter::routePRNetList(const std::vector<PRNet*>& pr_net_list, const PRRouteParam& pr_route_param)
{
  RTLOG.info(Loc::current(), "Mode: ", pr_route_param.route_name, ", net_num: ", pr_net_list.size());
  size_t next_percent = 10;
  for (size_t i = 0; i < pr_net_list.size(); i++) {
    PRNet* pr_net = pr_net_list[i];
    routePRNet(*pr_net, pr_route_param);
    size_t percent = ((i + 1) * 100) / pr_net_list.size();
    if (percent >= next_percent || (i + 1) == pr_net_list.size()) {
      RTLOG.info(Loc::current(), "Mode: ", pr_route_param.route_name, ", progress: ", percent, "% (", (i + 1), "/", pr_net_list.size(), ")");
      next_percent += 10;
    }
  }
}

void PlanarRouter::routePRNet(PRNet& pr_net, const PRRouteParam& pr_route_param)
{
  PRGridGraph::NetRouteTrial trial = _grid_graph.begin_route(pr_net);

  std::vector<Segment<PlanarCoord>> routing_segment_list = pr_route_param.pr_route_mode == PRRouteMode::kAStar
                                                               ? routeSegmentListByAStar(trial, pr_net)
                                                               : routeSegmentListByPattern(trial, pr_net, pr_route_param.pr_route_mode);
  if (routing_segment_list.empty()) {
    trial.rollback();
    return;
  }

  MTree<PlanarCoord> routing_tree = getCoordTree(pr_net, routing_segment_list);
  std::vector<Segment<PlanarCoord>> final_routing_segment_list;
  for (Segment<TNode<PlanarCoord>*>& segment : RTUTIL.getSegListByTree(routing_tree)) {
    final_routing_segment_list.emplace_back(segment.get_first()->value(), segment.get_second()->value());
  }
  trial.commit(std::move(final_routing_segment_list));
}

std::vector<Segment<PlanarCoord>> PlanarRouter::routeSegmentListByPattern(PRGridGraph::NetRouteTrial& trial, PRNet& pr_net, PRRouteMode pr_route_mode)
{
  PRTopoMode pr_topo_mode = pr_route_mode == PRRouteMode::kInitialLZPattern ? PRTopoMode::kNormal : PRTopoMode::kCongestion;
  std::vector<Segment<PlanarCoord>> planar_topo_list = getPlanarTopoList(trial, pr_net, pr_topo_mode);
  if (pr_route_mode == PRRouteMode::kAllPattern) {
    splitLongPlanarTopoList(trial, planar_topo_list);
  }

  std::vector<Segment<PlanarCoord>> routing_segment_list;
  for (Segment<PlanarCoord>& planar_topo : planar_topo_list) {
    std::vector<Segment<PlanarCoord>> topo_segment_list = getRoutingSegmentListByPattern(trial, planar_topo, pr_route_mode);
    trial.append(topo_segment_list);
    routing_segment_list.insert(routing_segment_list.end(), topo_segment_list.begin(), topo_segment_list.end());
  }
  return routing_segment_list;
}

std::vector<Segment<PlanarCoord>> PlanarRouter::getPlanarTopoList(const PRGridGraph::NetRouteTrial& trial, PRNet& pr_net, PRTopoMode pr_topo_mode)
{
  const std::vector<PlanarCoord>& node_coord_list = pr_net.get_node_coord_list();
  TBTask tb_task;
  tb_task.set_planar_coord_list(node_coord_list);
  tb_task.set_planar_search_region(PlanarRect(0, 0, _grid_graph.get_x_size() - 1, _grid_graph.get_y_size() - 1));
  tb_task.set_point_legal_query([this](const PlanarCoord& coord) { return _grid_graph.isCellLegal(coord); });
  bool use_congestion_topology = pr_topo_mode == PRTopoMode::kCongestion && needCongestionAware(trial);
  tb_task.set_topo_mode(use_congestion_topology ? TBTopoMode::kCongestion : TBTopoMode::kGeometry);
  if (!use_congestion_topology) {
    return RTTB.getPlanarTopoList(tb_task);
  }

  double overflow_unit = _pr_com_param.get_overflow_unit();
  TBSegmentCostQuery segment_cost_query = [this, &trial, overflow_unit](const PlanarCoord& first, const PlanarCoord& second) {
    return _grid_graph.get_segment_cost(trial, first, second, overflow_unit);
  };
  PRTopologyCostCache topology_cost_cache(std::move(segment_cost_query));
  tb_task.set_segment_cost_query(
      [&topology_cost_cache](const PlanarCoord& first, const PlanarCoord& second) { return topology_cost_cache.getCost(first, second); });
  return RTTB.getPlanarTopoList(tb_task);
}

bool PlanarRouter::needCongestionAware(const PRGridGraph::NetRouteTrial& trial)
{
  if (trial.get_pr_net()->get_node_coord_list().size() < 3) {
    return false;
  }
  if (trial.get_old_edge_set().empty()) {
    return true;
  }
  double history_threshold = 0.64 * _pr_com_param.get_overflow_unit();
  for (PREdgeId edge_id : trial.get_old_edge_set()) {
    const PRGridEdge& routing_edge = _grid_graph.get_edge(edge_id);
    if (routing_edge.ignore_net_set.contains(trial.get_net_idx())) {
      continue;
    }
    int32_t supply = routing_edge.supply;
    if (supply <= 0 || routing_edge.demand / static_cast<double>(supply) >= 0.8 || routing_edge.congestion_cost >= history_threshold) {
      return true;
    }
  }
  return false;
}

void PlanarRouter::splitLongPlanarTopoList(const PRGridGraph::NetRouteTrial& trial, std::vector<Segment<PlanarCoord>>& planar_topo_list)
{
  constexpr int32_t min_subsegment_length = 30;
  int32_t split_length = _pr_com_param.get_topo_spilt_length();
  std::vector<Segment<PlanarCoord>> split_topo_list;
  split_topo_list.reserve(planar_topo_list.size() * 3);
  for (Segment<PlanarCoord>& planar_topo : planar_topo_list) {
    PlanarCoord first_coord = planar_topo.get_first();
    PlanarCoord second_coord = planar_topo.get_second();
    int32_t topo_length = RTUTIL.getManhattanDistance(first_coord, second_coord);
    if (!RTUTIL.isOblique(first_coord, second_coord) || topo_length < split_length) {
      split_topo_list.push_back(planar_topo);
      continue;
    }

    // split twice if topo length very long
    int32_t split_num = topo_length <= 2 * split_length ? 1 : 2;
    int32_t piece_num = split_num + 1;
    std::vector<PlanarCoord> legal_coord_list{first_coord};
    bool has_legal_split = true;
    for (int32_t i = 1; i <= split_num; i++) {
      PlanarCoord ideal_coord(std::round(first_coord.get_x() + ((second_coord.get_x() - first_coord.get_x()) * i / static_cast<double>(piece_num))),
                              std::round(first_coord.get_y() + ((second_coord.get_y() - first_coord.get_y()) * i / static_cast<double>(piece_num))));
      bool has_escape = false;
      for (const PlanarCoord& neighbor :
           {PlanarCoord(ideal_coord.get_x() - 1, ideal_coord.get_y()), PlanarCoord(ideal_coord.get_x() + 1, ideal_coord.get_y()),
            PlanarCoord(ideal_coord.get_x(), ideal_coord.get_y() - 1), PlanarCoord(ideal_coord.get_x(), ideal_coord.get_y() + 1)}) {
        if (std::isfinite(_grid_graph.get_segment_cost(trial, ideal_coord, neighbor, _pr_com_param.get_overflow_unit()))) {
          has_escape = true;
          break;
        }
      }
      if (!has_escape) {
        has_legal_split = false;
        break;
      }
      legal_coord_list.push_back(ideal_coord);
    }
    legal_coord_list.push_back(second_coord);

    for (int32_t i = 1; has_legal_split && i <= piece_num; i++) {
      if (RTUTIL.getManhattanDistance(legal_coord_list[i - 1], legal_coord_list[i]) < min_subsegment_length) {
        has_legal_split = false;
      }
    }
    if (!has_legal_split) {
      split_topo_list.push_back(planar_topo);
      continue;
    }
    for (int32_t i = 1; i <= piece_num; i++) {
      split_topo_list.emplace_back(legal_coord_list[i - 1], legal_coord_list[i]);
    }
  }
  planar_topo_list = std::move(split_topo_list);
}

std::vector<Segment<PlanarCoord>> PlanarRouter::getRoutingSegmentListByPattern(const PRGridGraph::NetRouteTrial& trial, Segment<PlanarCoord>& planar_topo,
                                                                              PRRouteMode pr_route_mode)
{
  constexpr int64_t edge_visit_num_per_thread = 8192;
  int32_t max_thread_num = std::max(1, std::min(RTDM.getConfig().thread_number, 16));

  std::vector<PRCandidate> candidate_list = getPRCandidateListByTopo(_pr_com_param.get_expand_step_num(), planar_topo, pr_route_mode);
  auto candidate_num = static_cast<int32_t>(candidate_list.size());
  int64_t edge_visit_num = 0;
  for (PRCandidate& candidate : candidate_list) {
    for (Segment<PlanarCoord>& segment : candidate.get_routing_segment_list()) {
      edge_visit_num += RTUTIL.getManhattanDistance(segment.get_first(), segment.get_second());
    }
  }
  int32_t thread_num = std::min({candidate_num, max_thread_num, static_cast<int32_t>(std::max<int64_t>(1, edge_visit_num / edge_visit_num_per_thread))});

#pragma omp parallel for if (thread_num > 1) num_threads(thread_num) schedule(guided, 8)
  for (int32_t candidate_idx = 0; candidate_idx < candidate_num; candidate_idx++) {
    updatePRCandidate(trial, candidate_list[candidate_idx]);
  }

  auto best_candidate_it = candidate_list.begin();
  for (auto candidate_it = best_candidate_it + 1; candidate_it != candidate_list.end(); candidate_it++) {
    if (isBetterCandidate(_pr_com_param.get_corner_weight(), *candidate_it, *best_candidate_it)) {
      best_candidate_it = candidate_it;
    }
  }
  const PRCandidate::RoutingSegmentList& best_segment_list = best_candidate_it->get_routing_segment_list();
  return std::vector<Segment<PlanarCoord>>(best_segment_list.begin(), best_segment_list.end());
}

std::vector<PRCandidate> PlanarRouter::getPRCandidateListByTopo(int32_t expand_step_num, Segment<PlanarCoord>& planar_topo, PRRouteMode pr_route_mode)
{
  std::vector<PRCandidate> pr_candidate_list;
  int32_t span_x = std::abs(planar_topo.get_first().get_x() - planar_topo.get_second().get_x());
  int32_t span_y = std::abs(planar_topo.get_first().get_y() - planar_topo.get_second().get_y());

  bool is_oblique = RTUTIL.isOblique(planar_topo.get_first(), planar_topo.get_second());
  size_t candidate_num = is_oblique ? span_x + span_y : 1;
  if (pr_route_mode == PRRouteMode::kAllPattern) {
    candidate_num += (2 * static_cast<size_t>(std::max(0, span_x - 1)) * std::max(0, span_y - 1));
    candidate_num += (static_cast<size_t>(is_oblique ? 6 : 2) * expand_step_num);
  }
  pr_candidate_list.reserve(candidate_num);

  addPRCandidateListByStraight(pr_candidate_list, planar_topo);
  addPRCandidateListByLPattern(pr_candidate_list, planar_topo);
  addPRCandidateListByZPattern(pr_candidate_list, planar_topo);
  if (pr_route_mode == PRRouteMode::kAllPattern) {
    addPRCandidateListByInner3Bends(pr_candidate_list, planar_topo);
    addPRCandidateListByUPattern(pr_candidate_list, expand_step_num, planar_topo);
    addPRCandidateListByOuter3Bends(pr_candidate_list, expand_step_num, planar_topo);
  }
  return pr_candidate_list;
}

void PlanarRouter::updatePRCandidate(const PRGridGraph::NetRouteTrial& trial, PRCandidate& pr_candidate)
{
  double overflow_unit = _pr_com_param.get_overflow_unit();
  int32_t curr_net_idx = trial.get_net_idx();

  PRCandidateCost candidate_cost;
  Direction pre_direction = Direction::kNone;
  for (Segment<PlanarCoord>& coord_segment : pr_candidate.get_routing_segment_list()) {
    PlanarCoord& first_coord = coord_segment.get_first();
    PlanarCoord& second_coord = coord_segment.get_second();
    if (!RTUTIL.isRightAngled(first_coord, second_coord)) {
      RTLOG.error(Loc::current(), "The direction is error!");
    }
    Direction direction = RTUTIL.getDirection(first_coord, second_coord);
    if (pre_direction != Direction::kNone && pre_direction != direction) {
      candidate_cost.total_corner_num++;
    }
    pre_direction = direction;
  }
  for (Segment<PlanarCoord>& routing_segment : pr_candidate.get_routing_segment_list()) {
    PlanarCoord first_coord = routing_segment.get_first();
    PlanarCoord second_coord = routing_segment.get_second();
    int32_t first_x = std::min(first_coord.get_x(), second_coord.get_x());
    int32_t second_x = std::max(first_coord.get_x(), second_coord.get_x());
    int32_t first_y = std::min(first_coord.get_y(), second_coord.get_y());
    int32_t second_y = std::max(first_coord.get_y(), second_coord.get_y());
    bool is_horizontal = RTUTIL.isHorizontal(first_coord, second_coord);
    int32_t first_idx = is_horizontal ? first_x : first_y;
    int32_t second_idx = is_horizontal ? second_x : second_y;
    for (int32_t idx = first_idx; idx < second_idx; idx++) {
      PlanarCoord edge_first = is_horizontal ? PlanarCoord(idx, first_y) : PlanarCoord(first_x, idx);
      PlanarCoord edge_second = is_horizontal ? PlanarCoord(idx + 1, first_y) : PlanarCoord(first_x, idx + 1);
      PREdgeId edge_id = _grid_graph.get_edge_id(edge_first, edge_second);
      const PRGridEdge& routing_edge = _grid_graph.get_edge(edge_id);
      bool is_owned = trial.get_new_edge_set().contains(edge_id);
      candidate_cost.total_wire_length += !is_owned;
      if (is_owned || routing_edge.ignore_net_set.contains(curr_net_idx)) {
        continue;
      }
      const PREdgeCost edge_cost = _grid_graph.get_edge_cost(trial, edge_id);
      candidate_cost.is_path_blocked |= routing_edge.supply == 0;
      candidate_cost.is_overflow |= edge_cost.is_overflow;
      candidate_cost.total_cost += edge_cost.getTotalCost(overflow_unit, routing_edge.congestion_cost);
      candidate_cost.saturation_edge_num += edge_cost.is_saturated;
      candidate_cost.hotspot_edge_num += edge_cost.is_hotspot;
    }
  }

  pr_candidate.set_candidate_cost(candidate_cost);
}

bool PlanarRouter::isBetterCandidate(double corner_weight, const PRCandidate& candidate, const PRCandidate& best_candidate)
{
  constexpr double score_epsilon = 1e-9;

  if (candidate.get_is_path_blocked() != best_candidate.get_is_path_blocked()) {
    return !candidate.get_is_path_blocked();
  }
  if (candidate.get_is_overflow() != best_candidate.get_is_overflow()) {
    return !candidate.get_is_overflow();
  }

  double candidate_score = candidate.get_total_wire_length() + candidate.get_total_cost() + (corner_weight * candidate.get_total_corner_num());
  double best_score = best_candidate.get_total_wire_length() + best_candidate.get_total_cost() + (corner_weight * best_candidate.get_total_corner_num());
  if (!(std::abs(candidate_score - best_score) < score_epsilon)) {
    return candidate_score < best_score;
  }
  if (candidate.get_saturation_edge_num() != best_candidate.get_saturation_edge_num()) {
    return candidate.get_saturation_edge_num() < best_candidate.get_saturation_edge_num();
  }
  if (candidate.get_hotspot_edge_num() != best_candidate.get_hotspot_edge_num()) {
    return candidate.get_hotspot_edge_num() < best_candidate.get_hotspot_edge_num();
  }
  return candidate.get_total_wire_length() < best_candidate.get_total_wire_length();
}

std::vector<Segment<PlanarCoord>> PlanarRouter::routeSegmentListByAStar(PRGridGraph::NetRouteTrial& trial, PRNet& pr_net)
{
  const std::vector<PlanarCoord>& node_coord_list = pr_net.get_node_coord_list();
  if (node_coord_list.size() <= 1) {
    return {};
  }

  PRAStarTask astar_task;
  astar_task.source_coord_set.insert(getAStarSeedCoord(node_coord_list));
  astar_task.sink_coord_set.insert(node_coord_list.begin(), node_coord_list.end());
  astar_task.sink_coord_set.erase(*astar_task.source_coord_set.begin());

  PRAStarSearchArea search_area;
  search_area.search_rect = getAStarBaseRect(node_coord_list);
  prepareAStarScratch(search_area);

  std::vector<Segment<PlanarCoord>> routing_segment_list;
  while (!astar_task.sink_coord_set.empty()) {
    std::vector<PlanarCoord> path_coord_list = searchAStarPath(trial, astar_task, search_area);
    if (path_coord_list.empty()) {
      return {};
    }
    std::vector<Segment<PlanarCoord>> connect_segment_list = getRoutingSegmentListByCoordList(path_coord_list);
    trial.append(connect_segment_list);
    routing_segment_list.insert(routing_segment_list.end(), connect_segment_list.begin(), connect_segment_list.end());
    for (const Segment<PlanarCoord>& connect_segment : connect_segment_list) {
      astar_task.source_coord_set.insert(connect_segment.get_first());
      astar_task.source_coord_set.insert(connect_segment.get_second());
    }
    astar_task.sink_coord_set.erase(path_coord_list.back());
  }
  return routing_segment_list;
}

PlanarCoord PlanarRouter::getAStarSeedCoord(const std::vector<PlanarCoord>& node_coord_list)
{
  PlanarCoord center_coord = RTUTIL.getBoundingBox(node_coord_list).getMidPoint();
  PlanarCoord seed_coord = node_coord_list.front();
  int32_t min_distance = RTUTIL.getManhattanDistance(center_coord, seed_coord);
  for (const PlanarCoord& node_coord : node_coord_list) {
    int32_t distance = RTUTIL.getManhattanDistance(center_coord, node_coord);
    if (distance < min_distance) {
      min_distance = distance;
      seed_coord = node_coord;
    }
  }
  return seed_coord;
}

PlanarRect PlanarRouter::getAStarBaseRect(const std::vector<PlanarCoord>& node_coord_list)
{
  PlanarRect node_rect = RTUTIL.getBoundingBox(node_coord_list);
  PlanarRect search_rect = node_rect;
  for (const PlanarRect& macro_grid_rect : _grid_graph.get_macro_grid_rect_list()) {
    if (!RTUTIL.isClosedOverlap(node_rect, macro_grid_rect)) {
      continue;
    }
    search_rect.set_ll_x(std::min(search_rect.get_ll_x(), macro_grid_rect.get_ll_x()));
    search_rect.set_ll_y(std::min(search_rect.get_ll_y(), macro_grid_rect.get_ll_y()));
    search_rect.set_ur_x(std::max(search_rect.get_ur_x(), macro_grid_rect.get_ur_x()));
    search_rect.set_ur_y(std::max(search_rect.get_ur_y(), macro_grid_rect.get_ur_y()));
  }

  int32_t search_margin = _pr_com_param.get_astar_search_margin();
  search_rect.set_ll_x(std::max(0, search_rect.get_ll_x() - search_margin));
  search_rect.set_ll_y(std::max(0, search_rect.get_ll_y() - search_margin));
  search_rect.set_ur_x(std::min(_grid_graph.get_x_size() - 1, search_rect.get_ur_x() + search_margin));
  search_rect.set_ur_y(std::min(_grid_graph.get_y_size() - 1, search_rect.get_ur_y() + search_margin));
  return search_rect;
}

void PlanarRouter::prepareAStarScratch(PRAStarSearchArea& search_area)
{
  search_area.x_size = search_area.search_rect.get_ur_x() - search_area.search_rect.get_ll_x() + 1;
  search_area.y_size = search_area.search_rect.get_ur_y() - search_area.search_rect.get_ll_y() + 1;
  auto state_num = static_cast<size_t>(search_area.x_size) * search_area.y_size * 2;
  if (_astar_scratch.state_list.size() < state_num) {
    _astar_scratch.state_list.resize(state_num);
  }
}

std::vector<PlanarCoord> PlanarRouter::searchAStarPath(const PRGridGraph::NetRouteTrial& trial, const PRAStarTask& astar_task,
                                                      const PRAStarSearchArea& search_area)
{
  std::set<int32_t> sink_cell_idx_set;
  for (const PlanarCoord& sink_coord : astar_task.sink_coord_set) {
    sink_cell_idx_set.insert(getAStarStateIndex(search_area, sink_coord, false) / 2);
  }

  _astar_scratch.search_stamp++;
  if (_astar_scratch.search_stamp == 0) {
    for (PRAStarState& state : _astar_scratch.state_list) {
      state.search_stamp = 0;
    }
    _astar_scratch.search_stamp = 1;
  }
  _astar_scratch.open_heap.clear();

  CmpPRAStarQueueNode cmp_queue_node;
  for (const PlanarCoord& source_coord : astar_task.source_coord_set) {
    int32_t source_cell_idx = getAStarStateIndex(search_area, source_coord, false) / 2;
    double estimated_cost = getAStarEstimatedCost(astar_task, source_coord);
    for (int32_t direction_idx = 0; direction_idx < 2; direction_idx++) {
      int32_t source_state_idx = (source_cell_idx * 2) + direction_idx;
      getAStarState(source_state_idx).known_cost = 0;
      _astar_scratch.open_heap.push_back({source_state_idx, 0, estimated_cost});
    }
  }
  std::ranges::make_heap(_astar_scratch.open_heap, cmp_queue_node);
  int32_t curr_net_idx = trial.get_net_idx();
  double overflow_unit = _pr_com_param.get_overflow_unit();
  double corner_weight = _pr_com_param.get_corner_weight();
  int32_t search_ll_x = search_area.search_rect.get_ll_x();
  int32_t search_ll_y = search_area.search_rect.get_ll_y();
  constexpr std::array<std::pair<int32_t, int32_t>, 4> step_list = {{{-1, 0}, {1, 0}, {0, -1}, {0, 1}}};
  int32_t end_state_idx = -1;
  while (!_astar_scratch.open_heap.empty()) {
    std::ranges::pop_heap(_astar_scratch.open_heap, cmp_queue_node);
    PRAStarQueueNode queue_node = _astar_scratch.open_heap.back();
    _astar_scratch.open_heap.pop_back();

    PRAStarState& curr_state = getAStarState(queue_node.state_idx);
    if (curr_state.closed || queue_node.known_cost != curr_state.known_cost) {
      continue;
    }
    int32_t curr_cell_idx = queue_node.state_idx / 2;
    int32_t other_state_idx = queue_node.state_idx ^ 1;
    PRAStarState& other_state = getAStarState(other_state_idx);
    double dominated_cost = other_state.known_cost + corner_weight;
    if (curr_state.known_cost > dominated_cost || (curr_state.known_cost == dominated_cost && (corner_weight > 0 || queue_node.state_idx > other_state_idx))) {
      curr_state.closed = true;
      continue;
    }
    curr_state.closed = true;
    if (sink_cell_idx_set.contains(curr_cell_idx)) {
      end_state_idx = queue_node.state_idx;
      break;
    }

    int32_t curr_local_x = curr_cell_idx / search_area.y_size;
    int32_t curr_local_y = curr_cell_idx % search_area.y_size;
    int32_t curr_x = search_ll_x + curr_local_x;
    int32_t curr_y = search_ll_y + curr_local_y;
    bool curr_is_horizontal = queue_node.state_idx % 2 == 1;
    double turn_known_cost = curr_state.known_cost + corner_weight;
    for (auto [step_x, step_y] : step_list) {
      bool is_horizontal = step_x != 0;
      if (curr_is_horizontal != is_horizontal && other_state.known_cost < turn_known_cost) {
        continue;
      }
      int32_t neighbor_local_x = curr_local_x + step_x;
      int32_t neighbor_local_y = curr_local_y + step_y;
      if (neighbor_local_x < 0 || search_area.x_size <= neighbor_local_x || neighbor_local_y < 0 || search_area.y_size <= neighbor_local_y) {
        continue;
      }

      int32_t neighbor_cell_idx = curr_cell_idx + (step_x * search_area.y_size) + step_y;
      int32_t neighbor_state_idx = (neighbor_cell_idx * 2) + (is_horizontal ? 1 : 0);
      PRAStarState& neighbor_state = getAStarState(neighbor_state_idx);
      if (neighbor_state.closed) {
        continue;
      }

      int32_t neighbor_x = curr_x + step_x;
      int32_t neighbor_y = curr_y + step_y;
      PREdgeId edge_id = _grid_graph.get_edge_id({curr_x, curr_y}, {neighbor_x, neighbor_y});
      const PRGridEdge& routing_edge = _grid_graph.get_edge(edge_id);
      bool is_owned = trial.get_new_edge_set().contains(edge_id);
      bool is_ignored = routing_edge.ignore_net_set.contains(curr_net_idx);
      if (!is_owned && routing_edge.supply == 0 && !is_ignored) {
        continue;
      }
      double step_cost = is_owned ? 0 : 1.0;
      if (!is_owned && !is_ignored) {
        step_cost += _grid_graph.get_edge_cost(trial, edge_id, overflow_unit) - 1.0;
      }
      if (curr_is_horizontal != is_horizontal) {
        step_cost += corner_weight;
      }
      double next_known_cost = curr_state.known_cost + step_cost;
      if (next_known_cost < neighbor_state.known_cost) {
        int32_t other_neighbor_state_idx = neighbor_state_idx ^ 1;
        PRAStarState& other_neighbor_state = getAStarState(other_neighbor_state_idx);
        double dominated_cost = other_neighbor_state.known_cost + corner_weight;
        if (next_known_cost > dominated_cost || (next_known_cost == dominated_cost && (corner_weight > 0 || neighbor_state_idx > other_neighbor_state_idx))) {
          continue;
        }
        neighbor_state.parent_state_idx = queue_node.state_idx;
        neighbor_state.known_cost = next_known_cost;
        PlanarCoord neighbor_coord(neighbor_x, neighbor_y);
        double next_estimated_cost = getAStarEstimatedCost(astar_task, neighbor_coord);
        _astar_scratch.open_heap.push_back({.state_idx = neighbor_state_idx, .known_cost = next_known_cost, .total_cost = next_known_cost + next_estimated_cost});
        std::ranges::push_heap(_astar_scratch.open_heap, cmp_queue_node);
      }
    }
  }

  std::vector<PlanarCoord> coord_list;
  int32_t curr_state_idx = end_state_idx;
  while (curr_state_idx != -1) {
    coord_list.push_back(getAStarStateCoord(search_area, curr_state_idx));
    curr_state_idx = getAStarState(curr_state_idx).parent_state_idx;
  }
  std::ranges::reverse(coord_list);
  return coord_list;
}

int32_t PlanarRouter::getAStarStateIndex(const PRAStarSearchArea& search_area, const PlanarCoord& coord, bool is_horizontal)
{
  int32_t local_x = coord.get_x() - search_area.search_rect.get_ll_x();
  int32_t local_y = coord.get_y() - search_area.search_rect.get_ll_y();
  if (local_x < 0 || search_area.x_size <= local_x || local_y < 0 || search_area.y_size <= local_y) {
    RTLOG.error(Loc::current(), "The A* node is outside the search area!");
  }
  return (((local_x * search_area.y_size) + local_y) * 2) + (is_horizontal ? 1 : 0);
}

PlanarCoord PlanarRouter::getAStarStateCoord(const PRAStarSearchArea& search_area, int32_t state_idx)
{
  int64_t state_num = static_cast<int64_t>(search_area.x_size) * search_area.y_size * 2;
  if (state_idx < 0 || state_num <= state_idx) {
    RTLOG.error(Loc::current(), "The A* state index is outside the workspace!");
  }
  int32_t cell_idx = state_idx / 2;
  return {search_area.search_rect.get_ll_x() + (cell_idx / search_area.y_size), search_area.search_rect.get_ll_y() + (cell_idx % search_area.y_size)};
}

inline PRAStarState& PlanarRouter::getAStarState(int32_t state_idx)
{
  PRAStarState& state = _astar_scratch.state_list[state_idx];
  if (state.search_stamp != _astar_scratch.search_stamp) {
    state.search_stamp = _astar_scratch.search_stamp;
    state.closed = false;
    state.parent_state_idx = -1;
    state.known_cost = DBL_MAX;
  }
  return state;
}

inline int32_t PlanarRouter::getAStarEstimatedCost(const PRAStarTask& astar_task, const PlanarCoord& coord)
{
  int32_t min_distance = INT_MAX;
  for (const PlanarCoord& sink_coord : astar_task.sink_coord_set) {
    min_distance = std::min(min_distance, RTUTIL.getManhattanDistance(coord, sink_coord));
  }
  return min_distance;
}

std::vector<Segment<PlanarCoord>> PlanarRouter::getRoutingSegmentListByCoordList(const std::vector<PlanarCoord>& coord_list)
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

void PlanarRouter::addPRCandidate(std::vector<PRCandidate>& pr_candidate_list, Segment<PlanarCoord>& planar_topo,
                                  std::initializer_list<PlanarCoord> inflection_list)
{
  PRCandidate::RoutingSegmentList routing_segment_list;
  PlanarCoord prev_coord = planar_topo.get_first();
  for (const PlanarCoord& inflection_coord : inflection_list) {
    routing_segment_list.emplace_back(prev_coord, inflection_coord);
    prev_coord = inflection_coord;
  }
  routing_segment_list.emplace_back(prev_coord, planar_topo.get_second());
  pr_candidate_list.emplace_back(std::move(routing_segment_list));
}

void PlanarRouter::addPRCandidateListByStraight(std::vector<PRCandidate>& pr_candidate_list, Segment<PlanarCoord>& planar_topo)
{
  if (!RTUTIL.isOblique(planar_topo.get_first(), planar_topo.get_second())) {
    addPRCandidate(pr_candidate_list, planar_topo, {});
  }
}

void PlanarRouter::addPRCandidateListByLPattern(std::vector<PRCandidate>& pr_candidate_list, Segment<PlanarCoord>& planar_topo)
{
  PlanarCoord& first_coord = planar_topo.get_first();
  PlanarCoord& second_coord = planar_topo.get_second();
  if (RTUTIL.isRightAngled(first_coord, second_coord)) {
    return;
  }
  addPRCandidate(pr_candidate_list, planar_topo, {PlanarCoord(first_coord.get_x(), second_coord.get_y())});
  addPRCandidate(pr_candidate_list, planar_topo, {PlanarCoord(second_coord.get_x(), first_coord.get_y())});
}

void PlanarRouter::addPRCandidateListByZPattern(std::vector<PRCandidate>& pr_candidate_list, Segment<PlanarCoord>& planar_topo)
{
  PlanarCoord& first_coord = planar_topo.get_first();
  PlanarCoord& second_coord = planar_topo.get_second();
  if (RTUTIL.isRightAngled(first_coord, second_coord)) {
    return;
  }
  int32_t first_x = std::min(first_coord.get_x(), second_coord.get_x());
  int32_t second_x = std::max(first_coord.get_x(), second_coord.get_x());
  int32_t first_y = std::min(first_coord.get_y(), second_coord.get_y());
  int32_t second_y = std::max(first_coord.get_y(), second_coord.get_y());
  for (int32_t x = first_x + 1; x < second_x; x++) {
    addPRCandidate(pr_candidate_list, planar_topo, {PlanarCoord(x, first_coord.get_y()), PlanarCoord(x, second_coord.get_y())});
  }
  for (int32_t y = first_y + 1; y < second_y; y++) {
    addPRCandidate(pr_candidate_list, planar_topo, {PlanarCoord(first_coord.get_x(), y), PlanarCoord(second_coord.get_x(), y)});
  }
}

void PlanarRouter::addPRCandidateListByUPattern(std::vector<PRCandidate>& pr_candidate_list, int32_t expand_step_num, Segment<PlanarCoord>& planar_topo)
{
  Die& die = RTDM.getDatabase().get_die();

  PlanarCoord& first_coord = planar_topo.get_first();
  PlanarCoord& second_coord = planar_topo.get_second();
  if (RTUTIL.getManhattanDistance(first_coord, second_coord) <= 1) {
    return;
  }
  int32_t first_x = first_coord.get_x();
  int32_t second_x = second_coord.get_x();
  int32_t first_y = first_coord.get_y();
  int32_t second_y = second_coord.get_y();
  RTUTIL.swapByASC(first_x, second_x);
  RTUTIL.swapByASC(first_y, second_y);

  if (!RTUTIL.isHorizontal(first_coord, second_coord)) {
    for (int32_t i = 0; i < expand_step_num; i++) {
      first_x--;
      if (first_x >= die.get_grid_ll_x()) {
        addPRCandidate(pr_candidate_list, planar_topo, {PlanarCoord(first_x, first_coord.get_y()), PlanarCoord(first_x, second_coord.get_y())});
      }
      second_x++;
      if (second_x <= die.get_grid_ur_x()) {
        addPRCandidate(pr_candidate_list, planar_topo, {PlanarCoord(second_x, first_coord.get_y()), PlanarCoord(second_x, second_coord.get_y())});
      }
    }
  }
  if (!RTUTIL.isVertical(first_coord, second_coord)) {
    for (int32_t i = 0; i < expand_step_num; i++) {
      first_y--;
      if (first_y >= die.get_grid_ll_y()) {
        addPRCandidate(pr_candidate_list, planar_topo, {PlanarCoord(first_coord.get_x(), first_y), PlanarCoord(second_coord.get_x(), first_y)});
      }
      second_y++;
      if (second_y <= die.get_grid_ur_y()) {
        addPRCandidate(pr_candidate_list, planar_topo, {PlanarCoord(first_coord.get_x(), second_y), PlanarCoord(second_coord.get_x(), second_y)});
      }
    }
  }
}

void PlanarRouter::addPRCandidateListByInner3Bends(std::vector<PRCandidate>& pr_candidate_list, Segment<PlanarCoord>& planar_topo)
{
  PlanarCoord& first_coord = planar_topo.get_first();
  PlanarCoord& second_coord = planar_topo.get_second();
  if (RTUTIL.isRightAngled(first_coord, second_coord)) {
    return;
  }
  int32_t first_x = std::min(first_coord.get_x(), second_coord.get_x());
  int32_t second_x = std::max(first_coord.get_x(), second_coord.get_x());
  int32_t first_y = std::min(first_coord.get_y(), second_coord.get_y());
  int32_t second_y = std::max(first_coord.get_y(), second_coord.get_y());
  for (int32_t x = first_x + 1; x < second_x; x++) {
    for (int32_t y = first_y + 1; y < second_y; y++) {
      addPRCandidate(pr_candidate_list, planar_topo, {PlanarCoord(x, first_coord.get_y()), PlanarCoord(x, y), PlanarCoord(second_coord.get_x(), y)});
    }
  }
  for (int32_t x = first_x + 1; x < second_x; x++) {
    for (int32_t y = first_y + 1; y < second_y; y++) {
      addPRCandidate(pr_candidate_list, planar_topo, {PlanarCoord(first_coord.get_x(), y), PlanarCoord(x, y), PlanarCoord(x, second_coord.get_y())});
    }
  }
}

void PlanarRouter::addPRCandidateListByOuter3Bends(std::vector<PRCandidate>& pr_candidate_list, int32_t expand_step_num, Segment<PlanarCoord>& planar_topo)
{
  Die& die = RTDM.getDatabase().get_die();

  PlanarCoord& first_coord = planar_topo.get_first();
  PlanarCoord& second_coord = planar_topo.get_second();
  if (RTUTIL.isRightAngled(first_coord, second_coord)) {
    return;
  }
  int32_t start_x = first_coord.get_x();
  int32_t end_x = second_coord.get_x();
  int32_t start_y = first_coord.get_y();
  int32_t end_y = second_coord.get_y();

  int32_t box_lb_x = std::min(start_x, end_x);
  int32_t box_rt_x = std::max(start_x, end_x);
  int32_t box_lb_y = std::min(start_y, end_y);
  int32_t box_rt_y = std::max(start_y, end_y);

  for (int32_t i = 0; i < expand_step_num; i++) {
    box_lb_x--;
    box_rt_x++;
    box_lb_y--;
    box_rt_y++;
    if (start_x < end_x) {
      if (start_y < end_y) {
        if (die.get_grid_ll_y() <= box_lb_y && box_rt_x <= die.get_grid_ur_x()) {
          addPRCandidate(pr_candidate_list, planar_topo, {PlanarCoord(start_x, box_lb_y), PlanarCoord(box_rt_x, box_lb_y), PlanarCoord(box_rt_x, end_y)});
        }
        if (die.get_grid_ll_x() <= box_lb_x && box_rt_y <= die.get_grid_ur_y()) {
          addPRCandidate(pr_candidate_list, planar_topo, {PlanarCoord(box_lb_x, start_y), PlanarCoord(box_lb_x, box_rt_y), PlanarCoord(end_x, box_rt_y)});
        }
      } else {
        if (box_rt_x <= die.get_grid_ur_x() && box_rt_y <= die.get_grid_ur_y()) {
          addPRCandidate(pr_candidate_list, planar_topo, {PlanarCoord(start_x, box_rt_y), PlanarCoord(box_rt_x, box_rt_y), PlanarCoord(box_rt_x, end_y)});
        }
        if (die.get_grid_ll_x() <= box_lb_x && die.get_grid_ll_y() <= box_lb_y) {
          addPRCandidate(pr_candidate_list, planar_topo, {PlanarCoord(box_lb_x, start_y), PlanarCoord(box_lb_x, box_lb_y), PlanarCoord(end_x, box_lb_y)});
        }
      }
    } else {
      if (start_y < end_y) {
        if (box_rt_x <= die.get_grid_ur_x() && box_rt_y <= die.get_grid_ur_y()) {
          addPRCandidate(pr_candidate_list, planar_topo, {PlanarCoord(box_rt_x, start_y), PlanarCoord(box_rt_x, box_rt_y), PlanarCoord(end_x, box_rt_y)});
        }
        if (die.get_grid_ll_x() <= box_lb_x && die.get_grid_ll_y() <= box_lb_y) {
          addPRCandidate(pr_candidate_list, planar_topo, {PlanarCoord(start_x, box_lb_y), PlanarCoord(box_lb_x, box_lb_y), PlanarCoord(box_lb_x, end_y)});
        }
      } else {
        if (die.get_grid_ll_y() <= box_lb_y && box_rt_x <= die.get_grid_ur_x()) {
          addPRCandidate(pr_candidate_list, planar_topo, {PlanarCoord(box_rt_x, start_y), PlanarCoord(box_rt_x, box_lb_y), PlanarCoord(end_x, box_lb_y)});
        }
        if (die.get_grid_ll_x() <= box_lb_x && box_rt_y <= die.get_grid_ur_y()) {
          addPRCandidate(pr_candidate_list, planar_topo, {PlanarCoord(start_x, box_rt_y), PlanarCoord(box_lb_x, box_rt_y), PlanarCoord(box_lb_x, end_y)});
        }
      }
    }
  }
}

MTree<PlanarCoord> PlanarRouter::getCoordTree(PRNet& pr_net, std::vector<Segment<PlanarCoord>>& routing_segment_list)
{
  std::vector<PlanarCoord> candidate_root_coord_list;
  std::map<PlanarCoord, std::set<int32_t>, CmpPlanarCoordByXASC> key_coord_pin_map;
  std::vector<Pin>& pin_list = pr_net.get_pin_list();
  for (size_t i = 0; i < pin_list.size(); i++) {
    PlanarCoord coord = pin_list[i].get_access_point().get_grid_coord();
    candidate_root_coord_list.push_back(coord);
    key_coord_pin_map[coord].insert(static_cast<int32_t>(i));
  }
  return RTUTIL.getTreeByFullFlow(candidate_root_coord_list, routing_segment_list, key_coord_pin_map);
}

void PlanarRouter::uploadNetList()
{
  std::map<int32_t, std::vector<Segment<LayerCoord>>> net_global_result_map;
  for (PRNet& pr_net : _pr_net_list) {
    int32_t net_idx = pr_net.get_net_idx();
    for (Segment<PlanarCoord>& routing_segment : pr_net.get_routing_segment_list()) {
      net_global_result_map[net_idx].emplace_back(LayerCoord(routing_segment.get_first(), 0), LayerCoord(routing_segment.get_second(), 0));
    }
  }
  RTDM.getDatabase().get_net_global_result_map() = std::move(net_global_result_map);
}

#if 1  // exhibit

void PlanarRouter::updateSummary()
{
  int32_t micron_dbu = RTDM.getDatabase().get_micron_dbu();
  const PRGridGraph& grid_graph = _grid_graph;
  Summary& summary = RTDM.getDatabase().get_summary();

  double& total_demand = summary.pr_summary.total_demand;
  double& total_overflow = summary.pr_summary.total_overflow;
  double& total_wire_length = summary.pr_summary.total_wire_length;

  total_demand = 0;
  total_overflow = 0;
  total_wire_length = 0;

  for (const PRGridEdge& edge : grid_graph.get_horizontal_edge_list()) {
    total_demand += edge.demand;
    total_overflow += edge.get_overflow();
  }
  for (const PRGridEdge& edge : grid_graph.get_vertical_edge_list()) {
    total_demand += edge.demand;
    total_overflow += edge.get_overflow();
  }
  for (PRNet& pr_net : _pr_net_list) {
    for (Segment<PlanarCoord>& routing_segment : pr_net.get_routing_segment_list()) {
      const PlanarRect& first_gcell = grid_graph.get_gcell_rect(routing_segment.get_first());
      const PlanarRect& second_gcell = grid_graph.get_gcell_rect(routing_segment.get_second());
      total_wire_length += RTUTIL.getManhattanDistance(first_gcell.getMidPoint(), second_gcell.getMidPoint()) / 1.0 / micron_dbu;
    }
  }
}

void PlanarRouter::printSummary()
{
  Summary& summary = RTDM.getDatabase().get_summary();

  double& total_demand = summary.pr_summary.total_demand;
  double& total_overflow = summary.pr_summary.total_overflow;
  double& total_wire_length = summary.pr_summary.total_wire_length;

  fort::char_table summary_table;
  {
    summary_table.set_cell_text_align(fort::text_align::right);
    summary_table << fort::header << "total_demand" << total_demand << fort::endr;
    summary_table << fort::header << "total_overflow" << total_overflow << fort::endr;
    summary_table << fort::header << "total_wire_length" << total_wire_length << fort::endr;
  }
  RTUTIL.printTableList({summary_table});
}

void PlanarRouter::outputGuide()
{
  int32_t micron_dbu = RTDM.getDatabase().get_micron_dbu();
  ScaleAxis& gcell_axis = RTDM.getDatabase().get_gcell_axis();
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();
  std::string& pr_temp_directory_path = RTDM.getConfig().pr_temp_directory_path;
  int32_t output_inter_result = RTDM.getConfig().output_inter_result;
  if (!output_inter_result) {
    return;
  }
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  std::ofstream* guide_file_stream = RTUTIL.getOutputFileStream(RTUTIL.getString(pr_temp_directory_path, "route.guide"));
  if (guide_file_stream == nullptr) {
    return;
  }
  RTUTIL.pushStream(guide_file_stream, "guide net_name\n");
  RTUTIL.pushStream(guide_file_stream, "pin grid_x grid_y real_x real_y layer energy name\n");
  RTUTIL.pushStream(guide_file_stream, "wire grid1_x grid1_y grid2_x grid2_y real1_x real1_y real2_x real2_y layer\n");
  RTUTIL.pushStream(guide_file_stream, "via grid_x grid_y real_x real_y layer1 layer2\n");

  for (PRNet& pr_net : _pr_net_list) {
    if (pr_net.get_routing_segment_list().empty()) {
      continue;
    }
    RTUTIL.pushStream(guide_file_stream, "guide ", pr_net.get_net_name(), "\n");

    for (Pin& pin : pr_net.get_pin_list()) {
      AccessPoint& access_point = pin.get_access_point();
      double grid_x = access_point.get_grid_x();
      double grid_y = access_point.get_grid_y();
      double real_x = access_point.get_real_x() / 1.0 / micron_dbu;
      double real_y = access_point.get_real_y() / 1.0 / micron_dbu;
      std::string layer = routing_layer_list[access_point.get_layer_idx()].get_layer_name();
      std::string connnect;
      if (pin.get_is_driven()) {
        connnect = "driven";
      } else {
        connnect = "load";
      }
      RTUTIL.pushStream(guide_file_stream, "pin ", grid_x, " ", grid_y, " ", real_x, " ", real_y, " ", layer, " ", connnect, " ", pin.get_pin_name(), "\n");
    }
    for (Segment<PlanarCoord>& routing_segment : pr_net.get_routing_segment_list()) {
      PlanarCoord first_coord = routing_segment.get_first();
      double grid1_x = first_coord.get_x();
      double grid1_y = first_coord.get_y();
      PlanarCoord first_mid_coord = RTUTIL.getRealRectByGCell(first_coord, gcell_axis).getMidPoint();
      double real1_x = first_mid_coord.get_x() / 1.0 / micron_dbu;
      double real1_y = first_mid_coord.get_y() / 1.0 / micron_dbu;

      PlanarCoord second_coord = routing_segment.get_second();
      double grid2_x = second_coord.get_x();
      double grid2_y = second_coord.get_y();
      PlanarCoord second_mid_coord = RTUTIL.getRealRectByGCell(second_coord, gcell_axis).getMidPoint();
      double real2_x = second_mid_coord.get_x() / 1.0 / micron_dbu;
      double real2_y = second_mid_coord.get_y() / 1.0 / micron_dbu;

      std::string layer = routing_layer_list[0].get_layer_name();
      RTUTIL.pushStream(guide_file_stream, "wire ", grid1_x, " ", grid1_y, " ", grid2_x, " ", grid2_y, " ", real1_x, " ", real1_y, " ", real2_x, " ", real2_y, " ",
                        layer, "\n");
    }
  }
  RTUTIL.closeFileStream(guide_file_stream);
  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void PlanarRouter::outputNetCSV()
{
  std::string& pr_temp_directory_path = RTDM.getConfig().pr_temp_directory_path;
  int32_t output_inter_result = RTDM.getConfig().output_inter_result;
  if (!output_inter_result) {
    return;
  }
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  const PRGridGraph& grid_graph = _grid_graph;
  for (bool is_horizontal : {true, false}) {
    std::string file_name = is_horizontal ? "h_net_map.csv" : "v_net_map.csv";
    std::ofstream* net_csv_file = RTUTIL.getOutputFileStream(RTUTIL.getString(pr_temp_directory_path, file_name));
    int32_t x_size = is_horizontal ? grid_graph.get_horizontal_x_size() : grid_graph.get_x_size();
    int32_t y_size = is_horizontal ? grid_graph.get_y_size() : grid_graph.get_vertical_y_size();
    for (int32_t y = y_size - 1; y >= 0; y--) {
      for (int32_t x = 0; x < x_size; x++) {
        const PRGridEdge& edge = is_horizontal ? grid_graph.get_horizontal_edge(x, y) : grid_graph.get_vertical_edge(x, y);
        RTUTIL.pushStream(net_csv_file, edge.demand, ",");
      }
      RTUTIL.pushStream(net_csv_file, "\n");
    }
    RTUTIL.closeFileStream(net_csv_file);
  }
  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void PlanarRouter::outputOverflowCSV()
{
  std::string& pr_temp_directory_path = RTDM.getConfig().pr_temp_directory_path;
  int32_t output_inter_result = RTDM.getConfig().output_inter_result;
  if (!output_inter_result) {
    return;
  }
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  const PRGridGraph& grid_graph = _grid_graph;
  for (bool is_horizontal : {true, false}) {
    std::string file_name = is_horizontal ? "h_overflow_map.csv" : "v_overflow_map.csv";
    std::ofstream* overflow_csv_file = RTUTIL.getOutputFileStream(RTUTIL.getString(pr_temp_directory_path, file_name));
    int32_t x_size = is_horizontal ? grid_graph.get_horizontal_x_size() : grid_graph.get_x_size();
    int32_t y_size = is_horizontal ? grid_graph.get_y_size() : grid_graph.get_vertical_y_size();
    for (int32_t y = y_size - 1; y >= 0; y--) {
      for (int32_t x = 0; x < x_size; x++) {
        const PRGridEdge& edge = is_horizontal ? grid_graph.get_horizontal_edge(x, y) : grid_graph.get_vertical_edge(x, y);
        RTUTIL.pushStream(overflow_csv_file, edge.get_overflow(), ",");
      }
      RTUTIL.pushStream(overflow_csv_file, "\n");
    }
    RTUTIL.closeFileStream(overflow_csv_file);
  }
  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void PlanarRouter::outputUsageCSV()
{
  std::string& pr_temp_directory_path = RTDM.getConfig().pr_temp_directory_path;
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  const PRGridGraph& grid_graph = _grid_graph;
  for (bool is_horizontal : {true, false}) {
    std::string file_name = is_horizontal ? "h_usage_map.csv" : "v_usage_map.csv";
    std::ofstream csv_file(pr_temp_directory_path + file_name);
    if (!csv_file.is_open()) {
      RTLOG.error(Loc::current(), "Failed to open file '", pr_temp_directory_path + file_name, "'!");
      continue;
    }
    int32_t x_size = is_horizontal ? grid_graph.get_horizontal_x_size() : grid_graph.get_x_size();
    int32_t y_size = is_horizontal ? grid_graph.get_y_size() : grid_graph.get_vertical_y_size();
    for (int32_t y = y_size - 1; y >= 0; y--) {
      for (int32_t x = 0; x < x_size; x++) {
        const PRGridEdge& edge = is_horizontal ? grid_graph.get_horizontal_edge(x, y) : grid_graph.get_vertical_edge(x, y);
        csv_file << edge.demand << ",";
      }
      csv_file << "\n";
    }
    csv_file.close();
  }
  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void PlanarRouter::outputCongestionCostCSV()
{
  std::string& pr_temp_directory_path = RTDM.getConfig().pr_temp_directory_path;
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  const PRGridGraph& grid_graph = _grid_graph;
  for (bool is_horizontal : {true, false}) {
    std::string file_name = is_horizontal ? "h_congestion_cost_map.csv" : "v_congestion_cost_map.csv";
    std::ofstream csv_file(pr_temp_directory_path + file_name);
    if (!csv_file.is_open()) {
      RTLOG.error(Loc::current(), "Failed to open file '", pr_temp_directory_path + file_name, "'!");
      continue;
    }
    int32_t x_size = is_horizontal ? grid_graph.get_horizontal_x_size() : grid_graph.get_x_size();
    int32_t y_size = is_horizontal ? grid_graph.get_y_size() : grid_graph.get_vertical_y_size();
    for (int32_t y = y_size - 1; y >= 0; y--) {
      for (int32_t x = 0; x < x_size; x++) {
        const PRGridEdge& edge = is_horizontal ? grid_graph.get_horizontal_edge(x, y) : grid_graph.get_vertical_edge(x, y);
        csv_file << edge.congestion_cost << ",";
      }
      csv_file << "\n";
    }
    csv_file.close();
  }
  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

#endif

#if 1  // debug

void PlanarRouter::debugPlotPRGraph(std::string flag)
{
  const PRGridGraph& grid_graph = _grid_graph;
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

  // edge_axis
  {
    for (bool is_horizontal : {true, false}) {
      GPStruct edge_axis_struct(is_horizontal ? "h_edge_axis" : "v_edge_axis");
      int32_t x_size = is_horizontal ? grid_graph.get_horizontal_x_size() : grid_graph.get_x_size();
      int32_t y_size = is_horizontal ? grid_graph.get_y_size() : grid_graph.get_vertical_y_size();
      for (int32_t x = 0; x < x_size; x++) {
        for (int32_t y = 0; y < y_size; y++) {
          PlanarCoord first_grid_coord(x, y);
          PlanarCoord second_grid_coord = is_horizontal ? PlanarCoord(x + 1, y) : PlanarCoord(x, y + 1);
          PlanarRect first_real_rect = RTUTIL.getRealRectByGCell(first_grid_coord, gcell_axis);
          PlanarRect second_real_rect = RTUTIL.getRealRectByGCell(second_grid_coord, gcell_axis);
          PlanarCoord first_coord = first_real_rect.getMidPoint();
          PlanarCoord second_coord = second_real_rect.getMidPoint();
          PlanarRect edge_rect = is_horizontal
                                     ? PlanarRect(first_coord.get_x(), first_real_rect.get_ll_y(), second_coord.get_x(), first_real_rect.get_ur_y())
                                     : PlanarRect(first_real_rect.get_ll_x(), first_coord.get_y(), first_real_rect.get_ur_x(), second_coord.get_y());

          GPBoundary gp_boundary;
          gp_boundary.set_layer_idx(RTGP.getGDSIdxByRouting(0));
          gp_boundary.set_data_type(static_cast<int32_t>(is_horizontal ? GPDataType::kHEdgeAxis : GPDataType::kVEdgeAxis));
          gp_boundary.set_rect(edge_rect);
          edge_axis_struct.push(gp_boundary);
        }
      }
      gp_gds.addStruct(edge_axis_struct);
    }
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
  auto& type_layer_fixed_rect_rtree_map = RTDM.getDatabase().get_type_layer_fixed_rect_rtree_map();
  for (bool is_routing : {false, true}) {
    for (auto& [layer_idx, fixed_rect_rtree] : type_layer_fixed_rect_rtree_map[is_routing]) {
      std::map<int32_t, GPStruct> net_fixed_rect_struct_map;
      for (const auto& [rect, net_fixed_rect] : fixed_rect_rtree) {
        auto [net_idx, fixed_rect] = net_fixed_rect;
        auto struct_iter = net_fixed_rect_struct_map.try_emplace(net_idx, RTUTIL.getString("fixed_rect(net_", net_idx, ")")).first;
        GPBoundary gp_boundary;
        gp_boundary.set_data_type(static_cast<int32_t>(GPDataType::kShape));
        gp_boundary.set_rect(fixed_rect->get_real_rect());
        gp_boundary.set_layer_idx(is_routing ? RTGP.getGDSIdxByRouting(layer_idx) : RTGP.getGDSIdxByCut(layer_idx));
        struct_iter->second.push(gp_boundary);
      }
      for (auto& [net_idx, fixed_rect_struct] : net_fixed_rect_struct_map) {
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

  // routing_edge
  {
    for (bool is_horizontal : {true, false}) {
      GPStruct routing_edge_struct(is_horizontal ? "h_edge_info" : "v_edge_info");
      int32_t x_size = is_horizontal ? grid_graph.get_horizontal_x_size() : grid_graph.get_x_size();
      int32_t y_size = is_horizontal ? grid_graph.get_y_size() : grid_graph.get_vertical_y_size();
      for (int32_t x = 0; x < x_size; x++) {
        for (int32_t y = 0; y < y_size; y++) {
          const PRGridEdge& routing_edge = is_horizontal ? grid_graph.get_horizontal_edge(x, y) : grid_graph.get_vertical_edge(x, y);
          PlanarCoord first_grid_coord(x, y);
          PlanarCoord second_grid_coord = is_horizontal ? PlanarCoord(x + 1, y) : PlanarCoord(x, y + 1);
          PlanarRect first_real_rect = RTUTIL.getRealRectByGCell(first_grid_coord, gcell_axis);
          PlanarRect second_real_rect = RTUTIL.getRealRectByGCell(second_grid_coord, gcell_axis);
          PlanarCoord first_coord = first_real_rect.getMidPoint();
          PlanarCoord second_coord = second_real_rect.getMidPoint();
          PlanarRect edge_rect = is_horizontal
                                     ? PlanarRect(first_coord.get_x(), first_real_rect.get_ll_y(), second_coord.get_x(), first_real_rect.get_ur_y())
                                     : PlanarRect(first_real_rect.get_ll_x(), first_coord.get_y(), first_real_rect.get_ur_x(), second_coord.get_y());

          int32_t info_data_type = static_cast<int32_t>(is_horizontal ? GPDataType::kHEdgeInfo : GPDataType::kVEdgeInfo);
          GPBoundary gp_boundary;
          gp_boundary.set_layer_idx(RTGP.getGDSIdxByRouting(0));
          gp_boundary.set_data_type(info_data_type);
          gp_boundary.set_rect(edge_rect);
          routing_edge_struct.push(gp_boundary);

          std::string ignore_net_message;
          for (int32_t net_idx : routing_edge.ignore_net_set) {
            ignore_net_message += RTUTIL.getString(ignore_net_message.empty() ? "" : ",", net_idx);
          }
          std::string demand_net_message;
          for (int32_t net_idx : routing_edge.demand_net_idx_list) {
            demand_net_message += RTUTIL.getString(demand_net_message.empty() ? "" : ",", net_idx);
          }
          std::vector<std::string> message_list;
          message_list.push_back(RTUTIL.getString(is_horizontal ? "H" : "V", " (", first_grid_coord.get_x(), ",", first_grid_coord.get_y(), ")-(",
                                                  second_grid_coord.get_x(), ",", second_grid_coord.get_y(), ")"));
          message_list.push_back(RTUTIL.getString("demand: ", routing_edge.demand));
          message_list.push_back(RTUTIL.getString("demand_net: [", demand_net_message, "]"));
          message_list.push_back(RTUTIL.getString("supply: ", routing_edge.supply));
          message_list.push_back(RTUTIL.getString("ignore_net: [", ignore_net_message, "]"));
          message_list.push_back(RTUTIL.getString("congestion_cost: ", routing_edge.congestion_cost));

          int32_t text_y = edge_rect.get_ur_y();
          int32_t y_reduced_span = std::max(1, edge_rect.getYSpan() / 7);
          for (std::string& message : message_list) {
            text_y -= y_reduced_span;
            GPText gp_text;
            gp_text.set_coord(edge_rect.get_ll_x(), text_y);
            gp_text.set_text_type(info_data_type);
            gp_text.set_message(message);
            gp_text.set_layer_idx(RTGP.getGDSIdxByRouting(0));
            gp_text.set_presentation(GPTextPresentation::kLeftMiddle);
            routing_edge_struct.push(gp_text);
          }
        }
      }
      gp_gds.addStruct(routing_edge_struct);
    }
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

  GPStruct overflow_struct("overflow");
  for (bool is_horizontal : {true, false}) {
    int32_t x_size = is_horizontal ? grid_graph.get_horizontal_x_size() : grid_graph.get_x_size();
    int32_t y_size = is_horizontal ? grid_graph.get_y_size() : grid_graph.get_vertical_y_size();
    for (int32_t x = 0; x < x_size; x++) {
      for (int32_t y = 0; y < y_size; y++) {
        const PRGridEdge& edge = is_horizontal ? grid_graph.get_horizontal_edge(x, y) : grid_graph.get_vertical_edge(x, y);
        if (edge.get_overflow() <= 0) {
          continue;
        }
        PlanarCoord first_coord(x, y);
        PlanarCoord second_coord = is_horizontal ? PlanarCoord(x + 1, y) : PlanarCoord(x, y + 1);
        PlanarRect edge_rect = RTUTIL.getBoundingBox({RTUTIL.getRealRectByGCell(first_coord, gcell_axis), RTUTIL.getRealRectByGCell(second_coord, gcell_axis)});

        GPBoundary gp_boundary;
        gp_boundary.set_data_type(static_cast<int32_t>(GPDataType::kOverflow));
        gp_boundary.set_rect(edge_rect);
        gp_boundary.set_layer_idx(RTGP.getGDSIdxByRouting(0));
        overflow_struct.push(gp_boundary);
      }
    }
  }
  gp_gds.addStruct(overflow_struct);

  std::string gds_file_path = RTUTIL.getString(pr_temp_directory_path, flag, "_pr_graph.gds");
  RTGP.plot(gp_gds, gds_file_path);
}

#endif

}  // namespace irt
