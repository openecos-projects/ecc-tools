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
#include "RTHeader.hpp"
#include "RTInterface.hpp"
#include "TBTask.hpp"
#include "TOPOBuilder.hpp"
#include "Utility.hpp"

namespace irt {

namespace {

struct PRSegmentKey
{
  int32_t ll_x;
  int32_t ll_y;
  int32_t ur_x;
  int32_t ur_y;

  bool operator==(const PRSegmentKey&) const = default;
};

struct PRSegmentKeyHash
{
  size_t operator()(const PRSegmentKey& key) const
  {
    size_t seed = 0;
    for (int32_t value : {key.ll_x, key.ll_y, key.ur_x, key.ur_y}) {
      seed ^= std::hash<int32_t>{}(value) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    }
    return seed;
  }
};

class PRTopologyCostCache
{
 public:
  explicit PRTopologyCostCache(TBSegmentCostQuery cost_query) : _cost_query(std::move(cost_query)) {}

  double getCost(const PlanarCoord& first, const PlanarCoord& second)
  {
    if (first == second) {
      return 0;
    }
    if (first.get_x() != second.get_x() && first.get_y() != second.get_y()) {
      return std::numeric_limits<double>::infinity();
    }
    int64_t span = std::abs(static_cast<int64_t>(first.get_x()) - second.get_x()) + std::abs(static_cast<int64_t>(first.get_y()) - second.get_y());
    if (span == 1) {
      return _cost_query(first, second);
    }

    PRSegmentKey key{std::min(first.get_x(), second.get_x()), std::min(first.get_y(), second.get_y()), std::max(first.get_x(), second.get_x()),
                     std::max(first.get_y(), second.get_y())};
    if (auto iter = _segment_cost_map.find(key); iter != _segment_cost_map.end()) {
      return iter->second;
    }

    double cost = _cost_query(first, second);
    _segment_cost_map.emplace(key, cost);
    return cost;
  }

 private:
  TBSegmentCostQuery _cost_query;
  std::unordered_map<PRSegmentKey, double, PRSegmentKeyHash> _segment_cost_map;
};

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

  buildPlanarRoutingEdgeMap();
  initMacroGridRectList();

  runRouteFlow(pr_model);

  // debugPlotPRModel(pr_model, "after");
  updateSummary(pr_model);
  printSummary(pr_model);
  outputGuide(pr_model);
  outputNetCSV(pr_model);
  // outputUsageCSV(pr_model);
  // outputCongestionCostCSV(pr_model);
  RTDM.getDatabase().get_net_global_result_map() = std::move(pr_model.get_net_global_result_map());
  RTDM.rebuildGlobalResultRTree();
  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

// private

PlanarRouter* PlanarRouter::_pr_instance = nullptr;

PRModel PlanarRouter::initPRModel()
{
  std::vector<Net>& net_list = RTDM.getDatabase().get_net_list();

  PRModel pr_model;
  pr_model.set_pr_net_list(convertToPRNetList(net_list));
  return pr_model;
}

std::vector<PRNet> PlanarRouter::convertToPRNetList(std::vector<Net>& net_list)
{
  std::vector<PRNet> pr_net_list;
  pr_net_list.reserve(net_list.size());
  for (Net& net : net_list) {
    pr_net_list.emplace_back(convertToPRNet(net));
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
    pr_net.get_pr_pin_list().emplace_back(pin);
  }
  pr_net.set_bounding_box(net.get_bounding_box());
  return pr_net;
}

void PlanarRouter::setPRComParam(PRModel& pr_model)
{
  int32_t topo_spilt_length = 100;
  int32_t expand_step_num = 30;
  int32_t astar_search_margin = 30;
  double prefer_wire_unit = 1;
  double non_prefer_wire_unit = 2.5 * prefer_wire_unit;
  double corner_weight = non_prefer_wire_unit;
  double overflow_unit = 8 * non_prefer_wire_unit;
  /**
   * topo_spilt_length, expand_step_num, astar_search_margin, overflow_unit
   */

  PRComParam pr_com_param(topo_spilt_length, expand_step_num, astar_search_margin, overflow_unit, corner_weight);
  RTLOG.info(Loc::current(), "topo_spilt_length: ", pr_com_param.get_topo_spilt_length());
  RTLOG.info(Loc::current(), "expand_step_num: ", pr_com_param.get_expand_step_num());
  RTLOG.info(Loc::current(), "astar_search_margin: ", pr_com_param.get_astar_search_margin());
  RTLOG.info(Loc::current(), "overflow_unit: ", pr_com_param.get_overflow_unit());
  RTLOG.info(Loc::current(), "corner_weight: ", pr_com_param.get_corner_weight());
  RTLOG.info(Loc::current(), "cost_mode: routing_edge");
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
  std::ranges::sort(pr_task_list, CmpPRNet());
}

void PlanarRouter::buildPlanarRoutingEdgeMap()
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  GridMap<PlanarRect>& gcell_map = RTDM.getDatabase().get_gcell_map();
  GridMap<RoutingEdge>& planar_routing_h_edge_map = RTDM.getDatabase().get_planar_routing_h_edge_map();
  GridMap<RoutingEdge>& planar_routing_v_edge_map = RTDM.getDatabase().get_planar_routing_v_edge_map();
  std::vector<GridMap<RoutingEdge>>& routing_h_edge_map = RTDM.getDatabase().get_routing_h_edge_map();
  std::vector<GridMap<RoutingEdge>>& routing_v_edge_map = RTDM.getDatabase().get_routing_v_edge_map();

  planar_routing_h_edge_map.init(std::max(0, gcell_map.get_x_size() - 1), gcell_map.get_y_size());
  planar_routing_v_edge_map.init(gcell_map.get_x_size(), std::max(0, gcell_map.get_y_size() - 1));
  for (GridMap<RoutingEdge>* planar_routing_edge_map : {&planar_routing_h_edge_map, &planar_routing_v_edge_map}) {
    for (int32_t layer_idx = 0; layer_idx < static_cast<int32_t>(routing_h_edge_map.size()); layer_idx++) {
      GridMap<RoutingEdge>& routing_edge_map
          = planar_routing_edge_map == &planar_routing_h_edge_map ? routing_h_edge_map[layer_idx] : routing_v_edge_map[layer_idx];
#pragma omp parallel for
      for (int32_t x = 0; x < routing_edge_map.get_x_size(); x++) {
        for (int32_t y = 0; y < routing_edge_map.get_y_size(); y++) {
          RoutingEdge& planar_routing_edge = (*planar_routing_edge_map)[x][y];
          RoutingEdge& routing_edge = routing_edge_map[x][y];
          planar_routing_edge.set_supply(planar_routing_edge.get_supply() + routing_edge.get_supply());
          planar_routing_edge.set_congestion_cost(planar_routing_edge.get_congestion_cost() + routing_edge.get_congestion_cost());
          planar_routing_edge.get_ignore_net_set().insert(routing_edge.get_ignore_net_set().begin(), routing_edge.get_ignore_net_set().end());
        }
      }
    }
  }

  // record edge cost for fast query
  for (GridMap<RoutingEdge>* routing_edge_map : {&planar_routing_h_edge_map, &planar_routing_v_edge_map}) {
    GridMap<PREdgeCost>& edge_cost_map = routing_edge_map == &planar_routing_h_edge_map ? _routing_h_edge_cost_map : _routing_v_edge_cost_map;
    edge_cost_map.init(routing_edge_map->get_x_size(), routing_edge_map->get_y_size());
#pragma omp parallel for
    for (int32_t x = 0; x < routing_edge_map->get_x_size(); x++) {
      for (int32_t y = 0; y < routing_edge_map->get_y_size(); y++) {
        edge_cost_map[x][y] = getRoutingEdgeCost((*routing_edge_map)[x][y]);
      }
    }
  }

  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void PlanarRouter::initMacroGridRectList()
{
  ScaleAxis& gcell_axis = RTDM.getDatabase().get_gcell_axis();
  std::vector<Macro>& macro_list = RTDM.getDatabase().get_macro_list();
  _macro_grid_rect_list.clear();
  _macro_grid_rect_list.reserve(macro_list.size());
  for (Macro& macro : macro_list) {
    _macro_grid_rect_list.push_back(RTUTIL.getClosedGCellGridRect(macro.get_body_rect(), gcell_axis));
  }
}

PREdgeCost PlanarRouter::getRoutingEdgeCost(int32_t supply, int32_t demand)
{
  constexpr double saturation_start_ratio = 0.8;
  constexpr double hotspot_start_ratio = 0.9;

  PREdgeCost edge_cost;
  if (supply <= 0 || demand > supply) {
    double overflow_ratio = demand - std::max(supply, 0) + 1;
    double overflow_ratio_square = overflow_ratio * overflow_ratio;
    edge_cost.is_overflow = true;
    edge_cost.unit_cost = overflow_ratio_square * overflow_ratio_square;
    return edge_cost;
  }

  double usage_ratio = demand / 1.0 / supply;
  double usage_ratio_square = usage_ratio * usage_ratio;
  edge_cost.unit_cost = usage_ratio_square * usage_ratio_square;
  if (usage_ratio < saturation_start_ratio) {
    return edge_cost;
  }

  // Add progressively stronger penalties near capacity while preserving the base usage cost.
  double saturation_ratio = (usage_ratio - saturation_start_ratio) / (1.0 - saturation_start_ratio);
  edge_cost.is_saturated = true;
  edge_cost.unit_cost += saturation_ratio * saturation_ratio;
  if (usage_ratio < hotspot_start_ratio) {
    return edge_cost;
  }

  double hotspot_ratio = (usage_ratio - hotspot_start_ratio) / (1.0 - hotspot_start_ratio);
  edge_cost.is_hotspot = true;
  edge_cost.unit_cost += 2.0 * hotspot_ratio * hotspot_ratio;
  return edge_cost;
}

PREdgeCost PlanarRouter::getRoutingEdgeCost(const RoutingEdge& routing_edge)
{
  return getRoutingEdgeCost(routing_edge.get_supply(), routing_edge.get_demand() + 1);
}

double PlanarRouter::getTopologyEdgeCost(PRModel& pr_model, RoutingEdge& routing_edge)
{
  constexpr double wire_cost = 1;
  PRNet* curr_net = pr_model.get_curr_pr_task();
  int32_t net_idx = curr_net->get_net_idx();
  if (routing_edge.get_ignore_net_set().contains(net_idx)) {
    return wire_cost;
  }
  if (routing_edge.get_supply() <= 0) {
    return std::numeric_limits<double>::infinity();
  }

  int32_t effective_demand = routing_edge.get_demand() - curr_net->get_routing_edge_set().contains(&routing_edge);
  effective_demand = std::max(0, effective_demand);
  PREdgeCost edge_cost = getRoutingEdgeCost(routing_edge.get_supply(), effective_demand + 1);
  return wire_cost + edge_cost.getTotalCost(pr_model.get_pr_com_param().get_overflow_unit(), routing_edge.get_congestion_cost());
}

double PlanarRouter::getTopologySegmentCost(PRModel& pr_model, const PlanarCoord& first_coord, const PlanarCoord& second_coord)
{
  if (first_coord == second_coord) {
    return 0;
  }
  if (!RTUTIL.isRightAngled(first_coord, second_coord)) {
    return std::numeric_limits<double>::infinity();
  }

  GridMap<RoutingEdge>& routing_h_edge_map = RTDM.getDatabase().get_planar_routing_h_edge_map();
  GridMap<RoutingEdge>& routing_v_edge_map = RTDM.getDatabase().get_planar_routing_v_edge_map();
  bool is_horizontal = RTUTIL.isHorizontal(first_coord, second_coord);
  int32_t first_x = std::min(first_coord.get_x(), second_coord.get_x());
  int32_t second_x = std::max(first_coord.get_x(), second_coord.get_x());
  int32_t first_y = std::min(first_coord.get_y(), second_coord.get_y());
  int32_t second_y = std::max(first_coord.get_y(), second_coord.get_y());
  double segment_cost = 0;
  int32_t first_idx = is_horizontal ? first_x : first_y;
  int32_t second_idx = is_horizontal ? second_x : second_y;
  for (int32_t idx = first_idx; idx < second_idx; idx++) {
    int32_t edge_x = is_horizontal ? idx : first_x;
    int32_t edge_y = is_horizontal ? first_y : idx;
    GridMap<RoutingEdge>& edge_map = is_horizontal ? routing_h_edge_map : routing_v_edge_map;
    if (!edge_map.isInside(edge_x, edge_y)) {
      return std::numeric_limits<double>::infinity();
    }
    double edge_cost = getTopologyEdgeCost(pr_model, edge_map[edge_x][edge_y]);
    if (!std::isfinite(edge_cost)) {
      return edge_cost;
    }
    segment_cost += edge_cost;
  }
  return segment_cost;
}

void PlanarRouter::updateRoutingEdgeToGraph(RoutingEdge& routing_edge, PREdgeCost& edge_cost, int32_t curr_net_idx, ChangeType change_type,
                                            std::unordered_set<RoutingEdge*>& routing_edge_set)
{
  int32_t delta = 0;
  if (change_type == ChangeType::kAdd) {
    delta = 1;
  } else if (change_type == ChangeType::kDel) {
    delta = -1;
  } else {
    RTLOG.error(Loc::current(), "The change type is error!");
  }
  bool is_changed = delta > 0 ? routing_edge_set.insert(&routing_edge).second : routing_edge_set.erase(&routing_edge) > 0;
  if (!is_changed) {
    return;
  }
  if (routing_edge.get_ignore_net_set().contains(curr_net_idx)) {
    return;
  }
  if (delta < 0 && routing_edge.get_demand() <= 0) {
    RTLOG.error(Loc::current(), "The planar routing edge demand is error!");
  }
  std::vector<int32_t>& demand_net_idx_list = routing_edge.get_demand_net_idx_list();
  if (delta > 0) {
    demand_net_idx_list.push_back(curr_net_idx);
  } else {
    auto iter = std::find(demand_net_idx_list.begin(), demand_net_idx_list.end(), curr_net_idx);
    if (iter == demand_net_idx_list.end()) {
      RTLOG.error(Loc::current(), "The planar routing edge demand net is error!");
    }
    demand_net_idx_list.erase(iter);
  }
  routing_edge.set_demand(routing_edge.get_demand() + delta);
  edge_cost = getRoutingEdgeCost(routing_edge);
}

void PlanarRouter::updateRoutingSegmentListToGraph(PRModel& pr_model, std::span<const Segment<PlanarCoord>> routing_segment_list, ChangeType change_type,
                                                   std::unordered_set<RoutingEdge*>& routing_edge_set)
{
  int32_t curr_net_idx = pr_model.get_curr_pr_task()->get_net_idx();
  GridMap<RoutingEdge>& routing_h_edge_map = RTDM.getDatabase().get_planar_routing_h_edge_map();
  GridMap<RoutingEdge>& routing_v_edge_map = RTDM.getDatabase().get_planar_routing_v_edge_map();
  GridMap<PREdgeCost>& routing_h_edge_cost_map = _routing_h_edge_cost_map;
  GridMap<PREdgeCost>& routing_v_edge_cost_map = _routing_v_edge_cost_map;
  for (const Segment<PlanarCoord>& routing_segment : routing_segment_list) {
    PlanarCoord first_coord = routing_segment.get_first();
    PlanarCoord second_coord = routing_segment.get_second();
    if (first_coord == second_coord) {
      continue;
    }
    if (!RTUTIL.isRightAngled(first_coord, second_coord)) {
      RTLOG.error(Loc::current(), "The routing segment is oblique!");
    }
    int32_t first_x = std::min(first_coord.get_x(), second_coord.get_x());
    int32_t second_x = std::max(first_coord.get_x(), second_coord.get_x());
    int32_t first_y = std::min(first_coord.get_y(), second_coord.get_y());
    int32_t second_y = std::max(first_coord.get_y(), second_coord.get_y());
    if (RTUTIL.isHorizontal(first_coord, second_coord)) {
      for (int32_t x = first_x; x < second_x; x++) {
        updateRoutingEdgeToGraph(routing_h_edge_map[x][first_y], routing_h_edge_cost_map[x][first_y], curr_net_idx, change_type, routing_edge_set);
      }
    } else {
      for (int32_t y = first_y; y < second_y; y++) {
        updateRoutingEdgeToGraph(routing_v_edge_map[first_x][y], routing_v_edge_cost_map[first_x][y], curr_net_idx, change_type, routing_edge_set);
      }
    }
  }
}

void PlanarRouter::runRouteFlow(PRModel& pr_model)
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  constexpr int32_t max_iter = 5;
  constexpr bool enable_partial_rip_up = true;
  constexpr int32_t partial_rip_up_guard = 1;
  std::vector<PRNet*>& pr_task_list = pr_model.get_pr_task_list();

  routePRNetList(pr_model, pr_task_list, "initial LZ pattern", PRRouteMode::kLZPattern, PRTopoMode::kNormal);
  updateCongestion(pr_model);
  routePRNetList(pr_model, pr_task_list, "congestion LZ pattern", PRRouteMode::kLZPattern, PRTopoMode::kCongestion);
  updateCongestion(pr_model);
  routePRNetList(pr_model, getOverflowPRNetList(pr_model), "overflow All pattern", PRRouteMode::kAllPattern, PRTopoMode::kCongestion);
  updateCongestion(pr_model);

  for (int32_t iter = 0; iter < max_iter; iter++) {
    std::vector<PRNet*> reroute_net_list = getOverflowPRNetList(pr_model);
    if (reroute_net_list.empty()) {
      break;
    }

    bool is_partial_rip_up = enable_partial_rip_up && iter == 0;
    int32_t rip_up_guard = is_partial_rip_up ? partial_rip_up_guard : 0;
    routePRNetList(pr_model, reroute_net_list, "overflow A*", PRRouteMode::kAStar, PRTopoMode::kCongestion, is_partial_rip_up, rip_up_guard);
    updateCongestion(pr_model);
    auto& param = pr_model.get_pr_com_param();
    param.set_astar_search_margin(param.get_astar_search_margin() * 2);
    param.set_overflow_unit(param.get_overflow_unit() * 2);
  }

  uploadNetList(pr_model, pr_task_list);

  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void PlanarRouter::routePRNetList(PRModel& pr_model, const std::vector<PRNet*>& pr_net_list, const char* route_mode, PRRouteMode pr_route_mode,
                                  PRTopoMode pr_topo_mode, bool is_partial_rip_up, int32_t rip_up_guard)
{
  RTLOG.info(Loc::current(), "Mode: ", route_mode, ", net_num: ", pr_net_list.size());
  size_t next_percent = 10;
  for (size_t i = 0; i < pr_net_list.size(); i++) {
    PRNet* pr_net = pr_net_list[i];
    routePRNet(pr_model, pr_net, pr_route_mode, pr_topo_mode, is_partial_rip_up, rip_up_guard);
    size_t percent = ((i + 1) * 100) / pr_net_list.size();
    if (percent >= next_percent || (i + 1) == pr_net_list.size()) {
      RTLOG.info(Loc::current(), "Mode: ", route_mode, ", progress: ", percent, "% (", (i + 1), "/", pr_net_list.size(), ")");
      next_percent += 10;
    }
  }
}

void PlanarRouter::routePRNet(PRModel& pr_model, PRNet* pr_net, PRRouteMode pr_route_mode, PRTopoMode pr_topo_mode, bool is_partial_rip_up,
                              int32_t rip_up_guard)
{
  pr_model.set_curr_pr_task(pr_net);
  std::vector<Segment<PlanarCoord>> old_routing_segment_list = pr_net->get_routing_segment_list();
  std::vector<Segment<PlanarCoord>> planar_topo_list;
  std::vector<Segment<PlanarCoord>> routing_segment_list;
  bool is_partial_route = false;
  // partial rip up overflow segment with guard
  if (is_partial_rip_up && !old_routing_segment_list.empty()) {
    PROverflowTask overflow_task = getOverflowTask(pr_model, rip_up_guard);
    if (overflow_task.rip_up_segment_list.empty()) {
      pr_model.set_curr_pr_task(nullptr);
      return;
    }
    if (!overflow_task.planar_topo_list.empty()) {
      is_partial_route = true;
      planar_topo_list = std::move(overflow_task.planar_topo_list);
      routing_segment_list = std::move(overflow_task.kept_segment_list);
      updateRoutingSegmentListToGraph(pr_model, overflow_task.rip_up_segment_list, ChangeType::kDel, pr_net->get_routing_edge_set());
    }
  }

  // rip up all segments
  if (!is_partial_route) {
    planar_topo_list = getPlanarTopoList(pr_model, pr_topo_mode);
    updateRoutingSegmentListToGraph(pr_model, old_routing_segment_list, ChangeType::kDel, pr_net->get_routing_edge_set());
  }

  bool is_routed = routePlanarTopoList(pr_model, planar_topo_list, pr_route_mode, routing_segment_list);
  std::vector<Segment<PlanarCoord>> final_routing_segment_list;
  if (is_routed) {
    MTree<PlanarCoord> routing_tree = getCoordTree(pr_model, routing_segment_list);
    for (Segment<TNode<PlanarCoord>*>& segment : RTUTIL.getSegListByTree(routing_tree)) {
      final_routing_segment_list.emplace_back(segment.get_first()->value(), segment.get_second()->value());
    }
  }

  updateRoutingSegmentListToGraph(pr_model, old_routing_segment_list, ChangeType::kDel, pr_net->get_routing_edge_set());
  updateRoutingSegmentListToGraph(pr_model, routing_segment_list, ChangeType::kDel, pr_net->get_routing_edge_set());

  if (is_routed) {
    updateRoutingSegmentListToGraph(pr_model, final_routing_segment_list, ChangeType::kAdd, pr_net->get_routing_edge_set());
    pr_net->set_routing_segment_list(std::move(final_routing_segment_list));
  } else {
    pr_net->set_routing_segment_list(old_routing_segment_list);
    updateRoutingSegmentListToGraph(pr_model, old_routing_segment_list, ChangeType::kAdd, pr_net->get_routing_edge_set());
  }
  pr_model.set_curr_pr_task(nullptr);
}

void PlanarRouter::updateCongestion(PRModel& pr_model)
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  constexpr int32_t congestion_radius = 1;
  constexpr double congestion_decay = 0.5;
  double congestion_unit = pr_model.get_pr_com_param().get_overflow_unit();
  for (GridMap<RoutingEdge>* routing_edge_map : {&RTDM.getDatabase().get_planar_routing_h_edge_map(), &RTDM.getDatabase().get_planar_routing_v_edge_map()}) {
    int32_t x_size = routing_edge_map->get_x_size();
    int32_t y_size = routing_edge_map->get_y_size();
    int64_t edge_count = static_cast<int64_t>(x_size) * y_size;
#pragma omp parallel for
    for (int64_t edge_idx = 0; edge_idx < edge_count; edge_idx++) {
      int32_t x = static_cast<int32_t>(edge_idx / y_size);
      int32_t y = static_cast<int32_t>(edge_idx % y_size);
      double total_usage_ratio = 0;
      int32_t neighbor_num = 0;
      for (int32_t neighbor_x = std::max(0, x - congestion_radius); neighbor_x <= std::min(x_size - 1, x + congestion_radius); neighbor_x++) {
        for (int32_t neighbor_y = std::max(0, y - congestion_radius); neighbor_y <= std::min(y_size - 1, y + congestion_radius); neighbor_y++) {
          RoutingEdge& neighbor_edge = (*routing_edge_map)[neighbor_x][neighbor_y];
          if (neighbor_edge.get_supply() == 0) {
            continue;
          }
          total_usage_ratio += neighbor_edge.get_demand() / 1.0 / neighbor_edge.get_supply();
          neighbor_num++;
        }
      }
      RoutingEdge& routing_edge = (*routing_edge_map)[x][y];
      double usage_ratio = routing_edge.get_supply() == 0 ? 0 : routing_edge.get_demand() / 1.0 / routing_edge.get_supply();
      usage_ratio = std::max(0.0, usage_ratio - 0.8);
      double average_usage_ratio = neighbor_num == 0 ? 0 : total_usage_ratio / neighbor_num;
      double congestion_ratio = average_usage_ratio + usage_ratio;
      double new_congestion_cost = congestion_unit * congestion_ratio * congestion_ratio;
      routing_edge.set_congestion_cost((routing_edge.get_congestion_cost() * congestion_decay) + new_congestion_cost);
    }
  }

  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

std::vector<PRNet*> PlanarRouter::getOverflowPRNetList(PRModel& pr_model)
{
  std::vector<PRNet*> pr_net_list;
  for (PRNet& pr_net : pr_model.get_pr_net_list()) {
    for (RoutingEdge* routing_edge : pr_net.get_routing_edge_set()) {
      if (routing_edge->get_ignore_net_set().contains(pr_net.get_net_idx())) {
        continue;
      }
      if (routing_edge->get_overflow() > 0) {
        pr_net_list.push_back(&pr_net);
        break;
      }
    }
  }
  return pr_net_list;
}

PROverflowTask PlanarRouter::getOverflowTask(PRModel& pr_model, int32_t rip_up_guard)
{
  PROverflowTask overflow_task;
  PRNet* pr_net = pr_model.get_curr_pr_task();
  int32_t net_idx = pr_net->get_net_idx();
  GridMap<RoutingEdge>& routing_h_edge_map = RTDM.getDatabase().get_planar_routing_h_edge_map();
  GridMap<RoutingEdge>& routing_v_edge_map = RTDM.getDatabase().get_planar_routing_v_edge_map();
  std::vector<Segment<PlanarCoord>> unit_segment_list;
  std::vector<int32_t> rip_up_distance_list;
  std::vector<int32_t> segment_end_idx_list;
  std::map<PlanarCoord, std::vector<int32_t>, CmpPlanarCoordByXASC> coord_edge_idx_map;
  std::queue<int32_t> edge_queue;

  for (Segment<PlanarCoord>& segment : pr_net->get_routing_segment_list()) {
    PlanarCoord first_coord = segment.get_first();
    PlanarCoord second_coord = segment.get_second();
    int32_t step_x = first_coord.get_x() < second_coord.get_x() ? 1 : (second_coord.get_x() < first_coord.get_x() ? -1 : 0);
    int32_t step_y = first_coord.get_y() < second_coord.get_y() ? 1 : (second_coord.get_y() < first_coord.get_y() ? -1 : 0);
    int32_t segment_length = RTUTIL.getManhattanDistance(first_coord, second_coord);
    for (int32_t i = 0; i < segment_length; i++) {
      PlanarCoord unit_first(first_coord.get_x() + (step_x * i), first_coord.get_y() + (step_y * i));
      PlanarCoord unit_second(unit_first.get_x() + step_x, unit_first.get_y() + step_y);
      int32_t edge_x = std::min(unit_first.get_x(), unit_second.get_x());
      int32_t edge_y = std::min(unit_first.get_y(), unit_second.get_y());
      RoutingEdge& routing_edge = step_x == 0 ? routing_v_edge_map[edge_x][edge_y] : routing_h_edge_map[edge_x][edge_y];
      bool is_overflow = !routing_edge.get_ignore_net_set().contains(net_idx) && routing_edge.get_overflow() > 0;
      int32_t edge_idx = static_cast<int32_t>(unit_segment_list.size());
      unit_segment_list.emplace_back(unit_first, unit_second);
      rip_up_distance_list.push_back(is_overflow ? 0 : -1);
      coord_edge_idx_map[unit_first].push_back(edge_idx);
      coord_edge_idx_map[unit_second].push_back(edge_idx);
      if (is_overflow) {
        edge_queue.push(edge_idx);
      }
    }
    segment_end_idx_list.push_back(static_cast<int32_t>(unit_segment_list.size()));
  }

  while (!edge_queue.empty()) {
    int32_t curr_edge_idx = edge_queue.front();
    edge_queue.pop();
    if (rip_up_distance_list[curr_edge_idx] >= rip_up_guard) {
      continue;
    }
    Segment<PlanarCoord>& unit_segment = unit_segment_list[curr_edge_idx];
    for (PlanarCoord coord : {unit_segment.get_first(), unit_segment.get_second()}) {
      for (int32_t next_edge_idx : coord_edge_idx_map[coord]) {
        if (rip_up_distance_list[next_edge_idx] == -1) {
          rip_up_distance_list[next_edge_idx] = rip_up_distance_list[curr_edge_idx] + 1;
          edge_queue.push(next_edge_idx);
        }
      }
    }
  }

  int32_t segment_start_idx = 0;
  for (int32_t segment_end_idx : segment_end_idx_list) {
    if (segment_start_idx == segment_end_idx) {
      continue;
    }
    PlanarCoord split_coord = unit_segment_list[segment_start_idx].get_first();
    bool pre_is_rip_up = rip_up_distance_list[segment_start_idx] != -1;
    for (int32_t edge_idx = segment_start_idx + 1; edge_idx < segment_end_idx; edge_idx++) {
      bool is_rip_up = rip_up_distance_list[edge_idx] != -1;
      if (is_rip_up == pre_is_rip_up) {
        continue;
      }
      PlanarCoord boundary_coord = unit_segment_list[edge_idx].get_first();
      (pre_is_rip_up ? overflow_task.rip_up_segment_list : overflow_task.kept_segment_list).emplace_back(split_coord, boundary_coord);
      split_coord = boundary_coord;
      pre_is_rip_up = is_rip_up;
    }
    PlanarCoord end_coord = unit_segment_list[segment_end_idx - 1].get_second();
    (pre_is_rip_up ? overflow_task.rip_up_segment_list : overflow_task.kept_segment_list).emplace_back(split_coord, end_coord);
    segment_start_idx = segment_end_idx;
  }

  std::set<PlanarCoord, CmpPlanarCoordByXASC> pin_coord_set;
  for (PRPin& pr_pin : pr_net->get_pr_pin_list()) {
    pin_coord_set.insert(pr_pin.get_access_point().get_grid_coord());
  }
  std::vector<bool> visited_list(unit_segment_list.size(), false);
  // A partial task must be replaceable by one two-anchor topology per rip-up component.
  for (int32_t edge_idx = 0; edge_idx < static_cast<int32_t>(unit_segment_list.size()); edge_idx++) {
    if (rip_up_distance_list[edge_idx] == -1 || visited_list[edge_idx]) {
      continue;
    }
    std::set<PlanarCoord, CmpPlanarCoordByXASC> anchor_coord_set;
    edge_queue.push(edge_idx);
    visited_list[edge_idx] = true;
    while (!edge_queue.empty()) {
      int32_t curr_edge_idx = edge_queue.front();
      edge_queue.pop();
      Segment<PlanarCoord>& unit_segment = unit_segment_list[curr_edge_idx];
      for (PlanarCoord coord : {unit_segment.get_first(), unit_segment.get_second()}) {
        bool is_anchor = pin_coord_set.contains(coord);
        for (int32_t next_edge_idx : coord_edge_idx_map[coord]) {
          if (rip_up_distance_list[next_edge_idx] == -1) {
            is_anchor = true;
          } else if (!visited_list[next_edge_idx]) {
            visited_list[next_edge_idx] = true;
            edge_queue.push(next_edge_idx);
          }
        }
        if (is_anchor) {
          anchor_coord_set.insert(coord);
        }
      }
    }
    if (anchor_coord_set.size() != 2) {
      overflow_task.planar_topo_list.clear();
      return overflow_task;
    }
    auto anchor_it = anchor_coord_set.begin();
    PlanarCoord first_anchor = *anchor_it++;
    overflow_task.planar_topo_list.emplace_back(first_anchor, *anchor_it);
  }
  return overflow_task;
}

void PlanarRouter::splitLongPlanarTopoList(PRModel& pr_model, std::vector<Segment<PlanarCoord>>& planar_topo_list)
{
  constexpr int32_t min_subsegment_length = 30;
  int32_t split_length = pr_model.get_pr_com_param().get_topo_spilt_length();
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
        if (std::isfinite(getTopologySegmentCost(pr_model, ideal_coord, neighbor))) {
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

bool PlanarRouter::routePlanarTopoList(PRModel& pr_model, std::vector<Segment<PlanarCoord>>& planar_topo_list, PRRouteMode pr_route_mode,
                                       std::vector<Segment<PlanarCoord>>& routing_segment_list)
{
  constexpr int64_t edge_visit_num_per_thread = 8192;
  int32_t max_thread_num = std::max(1, std::min(RTDM.getConfig().thread_number, 16));

  if (pr_route_mode == PRRouteMode::kAllPattern) {
    splitLongPlanarTopoList(pr_model, planar_topo_list);
  }

  for (Segment<PlanarCoord>& planar_topo : planar_topo_list) {
    if (pr_route_mode == PRRouteMode::kAStar) {
      std::vector<Segment<PlanarCoord>> astar_segment_list = getRoutingSegmentListByAStar(pr_model, planar_topo, routing_segment_list);
      if (astar_segment_list.empty()) {
        return false;
      }
      routing_segment_list.insert(routing_segment_list.end(), astar_segment_list.begin(), astar_segment_list.end());
      updateRoutingSegmentListToGraph(pr_model, astar_segment_list, ChangeType::kAdd, pr_model.get_curr_pr_task()->get_routing_edge_set());
      continue;
    }

    std::vector<PRCandidate> candidate_list = getPRCandidateListByTopo(pr_model, planar_topo, pr_route_mode);
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
      updatePRCandidate(pr_model, candidate_list[candidate_idx]);
    }

    auto best_candidate_it = candidate_list.begin();
    for (auto candidate_it = best_candidate_it + 1; candidate_it != candidate_list.end(); candidate_it++) {
      if (isBetterCandidate(pr_model, *candidate_it, *best_candidate_it)) {
        best_candidate_it = candidate_it;
      }
    }
    const auto& best_segment_list = best_candidate_it->get_routing_segment_list();
    routing_segment_list.insert(routing_segment_list.end(), best_segment_list.begin(), best_segment_list.end());
    updateRoutingSegmentListToGraph(pr_model, {best_segment_list.data(), best_segment_list.size()}, ChangeType::kAdd,
                                    pr_model.get_curr_pr_task()->get_routing_edge_set());
  }
  return true;
}

bool PlanarRouter::isBetterCandidate(PRModel& pr_model, const PRCandidate& candidate, const PRCandidate& best_candidate)
{
  constexpr double score_epsilon = 1e-9;
  double corner_weight = pr_model.get_pr_com_param().get_corner_weight();

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

std::vector<PRCandidate> PlanarRouter::getPRCandidateListByTopo(PRModel& pr_model, Segment<PlanarCoord>& planar_topo, PRRouteMode pr_route_mode)
{
  std::vector<PRCandidate> pr_candidate_list;
  int32_t span_x = std::abs(planar_topo.get_first().get_x() - planar_topo.get_second().get_x());
  int32_t span_y = std::abs(planar_topo.get_first().get_y() - planar_topo.get_second().get_y());

  bool is_oblique = RTUTIL.isOblique(planar_topo.get_first(), planar_topo.get_second());
  size_t candidate_num = is_oblique ? span_x + span_y : 1;
  if (pr_route_mode == PRRouteMode::kAllPattern) {
    candidate_num += (2 * static_cast<size_t>(std::max(0, span_x - 1)) * std::max(0, span_y - 1));
    candidate_num += (static_cast<size_t>(is_oblique ? 6 : 2) * pr_model.get_pr_com_param().get_expand_step_num());
  }
  pr_candidate_list.reserve(candidate_num);

  addPRCandidateListByStraight(pr_candidate_list, planar_topo);
  addPRCandidateListByLPattern(pr_candidate_list, planar_topo);
  addPRCandidateListByZPattern(pr_candidate_list, planar_topo);
  if (pr_route_mode == PRRouteMode::kAllPattern) {
    addPRCandidateListByInner3Bends(pr_candidate_list, planar_topo);
    addPRCandidateListByUPattern(pr_candidate_list, pr_model, planar_topo);
    addPRCandidateListByOuter3Bends(pr_candidate_list, pr_model, planar_topo);
  }
  return pr_candidate_list;
}

bool PlanarRouter::shouldUseCongestionFlute(PRModel& pr_model, size_t unique_pin_num)
{
  if (unique_pin_num < 3) {
    return false;
  }
  PRNet* curr_net = pr_model.get_curr_pr_task();
  if (curr_net->get_routing_edge_set().empty()) {
    return true;
  }
  double history_threshold = 0.64 * pr_model.get_pr_com_param().get_overflow_unit();
  for (RoutingEdge* routing_edge : curr_net->get_routing_edge_set()) {
    if (routing_edge->get_ignore_net_set().contains(curr_net->get_net_idx())) {
      continue;
    }
    int32_t supply = routing_edge->get_supply();
    if (supply <= 0 || routing_edge->get_demand() / static_cast<double>(supply) >= 0.8 || routing_edge->get_congestion_cost() >= history_threshold) {
      return true;
    }
  }
  return false;
}

std::vector<Segment<PlanarCoord>> PlanarRouter::getPlanarTopoList(PRModel& pr_model, PRTopoMode pr_topo_mode)
{
  std::vector<PlanarCoord> planar_coord_list;
  {
    for (PRPin& pr_pin : pr_model.get_curr_pr_task()->get_pr_pin_list()) {
      planar_coord_list.push_back(pr_pin.get_access_point().get_grid_coord());
    }
    std::ranges::sort(planar_coord_list, CmpPlanarCoordByXASC());
    planar_coord_list.erase(std::ranges::unique(planar_coord_list).begin(), planar_coord_list.end());
  }
  TBTask tb_task;
  tb_task.set_planar_coord_list(planar_coord_list);
  GridMap<PlanarRect>& gcell_map = RTDM.getDatabase().get_gcell_map();
  tb_task.set_planar_search_region(PlanarRect(0, 0, gcell_map.get_x_size() - 1, gcell_map.get_y_size() - 1));
  TBSegmentCostQuery segment_cost_query
      = [this, &pr_model](const PlanarCoord& first, const PlanarCoord& second) { return getTopologySegmentCost(pr_model, first, second); };
  bool congestion_driven = pr_topo_mode == PRTopoMode::kCongestion && shouldUseCongestionFlute(pr_model, planar_coord_list.size());
  tb_task.set_topo_mode(congestion_driven ? TBTopoMode::kCongestion : TBTopoMode::kCost);
  if (pr_topo_mode == PRTopoMode::kNormal) {
    tb_task.set_segment_cost_query(std::move(segment_cost_query));
    return RTTB.getPlanarTopoList(tb_task);
  }

  PRTopologyCostCache topology_cost_cache(std::move(segment_cost_query));
  tb_task.set_segment_cost_query(
      [&topology_cost_cache](const PlanarCoord& first, const PlanarCoord& second) { return topology_cost_cache.getCost(first, second); });
  return RTTB.getPlanarTopoList(tb_task);
}

std::vector<Segment<PlanarCoord>> PlanarRouter::getRoutingSegmentListByAStar(PRModel& pr_model, const Segment<PlanarCoord>& planar_topo,
                                                                             const std::vector<Segment<PlanarCoord>>& routed_segment_list)
{
  PlanarCoord start_coord = planar_topo.get_first();
  PlanarCoord end_coord = planar_topo.get_second();
  if (start_coord == end_coord) {
    return {};
  }
  GridMap<PlanarRect>& gcell_map = RTDM.getDatabase().get_gcell_map();
  int32_t max_search_margin = std::max(gcell_map.get_x_size(), gcell_map.get_y_size());
  int32_t search_margin_step = std::max(1, pr_model.get_pr_com_param().get_astar_search_margin());
  int32_t search_margin = std::min(max_search_margin, search_margin_step);
  PlanarRect base_rect = getAStarBaseRect(planar_topo);

  _astar_workspace.has_owned_rect = !routed_segment_list.empty();
  if (_astar_workspace.has_owned_rect) {
    PlanarCoord first_coord = routed_segment_list.front().get_first();
    _astar_workspace.owned_rect = PlanarRect(first_coord.get_x(), first_coord.get_y(), first_coord.get_x(), first_coord.get_y());
    for (const Segment<PlanarCoord>& segment : routed_segment_list) {
      for (PlanarCoord coord : {segment.get_first(), segment.get_second()}) {
        _astar_workspace.owned_rect.set_ll_x(std::min(_astar_workspace.owned_rect.get_ll_x(), coord.get_x()));
        _astar_workspace.owned_rect.set_ll_y(std::min(_astar_workspace.owned_rect.get_ll_y(), coord.get_y()));
        _astar_workspace.owned_rect.set_ur_x(std::max(_astar_workspace.owned_rect.get_ur_x(), coord.get_x()));
        _astar_workspace.owned_rect.set_ur_y(std::max(_astar_workspace.owned_rect.get_ur_y(), coord.get_y()));
      }
    }
  }
  while (true) {
    PlanarRect search_rect(std::max(0, base_rect.get_ll_x() - search_margin), std::max(0, base_rect.get_ll_y() - search_margin),
                           std::min(gcell_map.get_x_size() - 1, base_rect.get_ur_x() + search_margin),
                           std::min(gcell_map.get_y_size() - 1, base_rect.get_ur_y() + search_margin));
    if (!prepareAStarWorkspace(search_rect, _astar_workspace)) {
      return {};
    }
    std::vector<Segment<PlanarCoord>> routing_segment_list;
    if (searchRoutingSegmentByAStar(pr_model, start_coord, end_coord, _astar_workspace, routing_segment_list)) {
      return routing_segment_list;
    }
    if (search_rect.get_ll_x() == 0 && search_rect.get_ll_y() == 0 && search_rect.get_ur_x() == gcell_map.get_x_size() - 1
        && search_rect.get_ur_y() == gcell_map.get_y_size() - 1) {
      return {};
    }
    search_margin = std::min(max_search_margin, search_margin + search_margin_step);
  }
}

bool PlanarRouter::prepareAStarWorkspace(const PlanarRect& workspace_rect, PRAStarWorkspace& workspace)
{
  workspace.workspace_rect = workspace_rect;
  workspace.x_size = (workspace_rect.get_ur_x() - workspace_rect.get_ll_x()) + 1;
  workspace.y_size = (workspace_rect.get_ur_y() - workspace_rect.get_ll_y()) + 1;
  if (workspace.x_size <= 0 || workspace.y_size <= 0) {
    RTLOG.error(Loc::current(), "The A* workspace is empty!");
    return false;
  }
  int64_t cell_num = static_cast<int64_t>(workspace.x_size) * workspace.y_size;
  if (cell_num > INT_MAX / 2) {
    RTLOG.error(Loc::current(), "The A* workspace is too large!");
    return false;
  }
  auto state_num = static_cast<size_t>(cell_num * 2);
  if (workspace.state_list.size() < state_num) {
    workspace.state_list.resize(state_num);
  }
  return true;
}

int32_t PlanarRouter::getAStarStateIndex(const PRAStarWorkspace& workspace, const PlanarCoord& coord, bool is_horizontal)
{
  int32_t local_x = coord.get_x() - workspace.workspace_rect.get_ll_x();
  int32_t local_y = coord.get_y() - workspace.workspace_rect.get_ll_y();
  if (local_x < 0 || workspace.x_size <= local_x || local_y < 0 || workspace.y_size <= local_y) {
    RTLOG.error(Loc::current(), "The A* node is outside the workspace!");
  }
  return (((local_x * workspace.y_size) + local_y) * 2) + (is_horizontal ? 1 : 0);
}

PlanarCoord PlanarRouter::getAStarStateCoord(const PRAStarWorkspace& workspace, int32_t state_idx)
{
  int64_t state_num = static_cast<int64_t>(workspace.x_size) * workspace.y_size * 2;
  if (state_idx < 0 || state_num <= state_idx) {
    RTLOG.error(Loc::current(), "The A* state index is outside the workspace!");
  }
  int32_t cell_idx = state_idx / 2;
  return {workspace.workspace_rect.get_ll_x() + (cell_idx / workspace.y_size), workspace.workspace_rect.get_ll_y() + (cell_idx % workspace.y_size)};
}

PRAStarState& PlanarRouter::getAStarState(PRAStarWorkspace& workspace, int32_t state_idx)
{
  PRAStarState& state = workspace.state_list[state_idx];
  if (state.search_stamp != workspace.search_stamp) {
    state.search_stamp = workspace.search_stamp;
    state.closed = false;
    state.parent_state_idx = -1;
    state.known_cost = DBL_MAX;
  }
  return state;
}

int32_t PlanarRouter::getAStarEstimatedCost(const PRAStarWorkspace& workspace, const PlanarCoord& coord, const PlanarCoord& end_coord, bool has_owned_edge)
{
  int32_t direct_distance = RTUTIL.getManhattanDistance(coord, end_coord);
  if (!has_owned_edge) {
    return direct_distance;
  }
  if (!workspace.has_owned_rect) {
    return 0;
  }
  int32_t coord_dx = std::max({workspace.owned_rect.get_ll_x() - coord.get_x(), 0, coord.get_x() - workspace.owned_rect.get_ur_x()});
  int32_t coord_dy = std::max({workspace.owned_rect.get_ll_y() - coord.get_y(), 0, coord.get_y() - workspace.owned_rect.get_ur_y()});
  int32_t end_dx = std::max({workspace.owned_rect.get_ll_x() - end_coord.get_x(), 0, end_coord.get_x() - workspace.owned_rect.get_ur_x()});
  int32_t end_dy = std::max({workspace.owned_rect.get_ll_y() - end_coord.get_y(), 0, end_coord.get_y() - workspace.owned_rect.get_ur_y()});
  return std::min(direct_distance, coord_dx + coord_dy + end_dx + end_dy);
}

bool PlanarRouter::searchRoutingSegmentByAStar(PRModel& pr_model, const PlanarCoord& start_coord, const PlanarCoord& end_coord, PRAStarWorkspace& workspace,
                                               std::vector<Segment<PlanarCoord>>& routing_segment_list)
{
  if (start_coord == end_coord || !RTUTIL.isInside(workspace.workspace_rect, start_coord) || !RTUTIL.isInside(workspace.workspace_rect, end_coord)) {
    return false;
  }
  workspace.search_stamp++;
  if (workspace.search_stamp == 0) {
    for (PRAStarState& state : workspace.state_list) {
      state.search_stamp = 0;
    }
    workspace.search_stamp = 1;
  }
  workspace.open_heap.clear();

  CmpPRAStarQueueNode cmp_queue_node;
  int32_t start_cell_idx = getAStarStateIndex(workspace, start_coord, false) / 2;
  int32_t end_cell_idx = getAStarStateIndex(workspace, end_coord, false) / 2;
  const std::unordered_set<RoutingEdge*>& routing_edge_set = pr_model.get_curr_pr_task()->get_routing_edge_set();
  bool has_owned_edge = !routing_edge_set.empty();
  double estimated_cost = getAStarEstimatedCost(workspace, start_coord, end_coord, has_owned_edge);
  for (int32_t direction_idx = 0; direction_idx < 2; direction_idx++) {
    int32_t start_state_idx = (start_cell_idx * 2) + direction_idx;
    getAStarState(workspace, start_state_idx).known_cost = 0;
    workspace.open_heap.push_back({start_state_idx, 0, estimated_cost});
  }
  std::ranges::make_heap(workspace.open_heap, cmp_queue_node);
  GridMap<RoutingEdge>& routing_h_edge_map = RTDM.getDatabase().get_planar_routing_h_edge_map();
  GridMap<RoutingEdge>& routing_v_edge_map = RTDM.getDatabase().get_planar_routing_v_edge_map();
  int32_t curr_net_idx = pr_model.get_curr_pr_task()->get_net_idx();
  double overflow_unit = pr_model.get_pr_com_param().get_overflow_unit();
  double corner_weight = pr_model.get_pr_com_param().get_corner_weight();
  int32_t workspace_ll_x = workspace.workspace_rect.get_ll_x();
  int32_t workspace_ll_y = workspace.workspace_rect.get_ll_y();
  constexpr std::array<std::pair<int32_t, int32_t>, 4> step_list = {{{-1, 0}, {1, 0}, {0, -1}, {0, 1}}};
  int32_t end_state_idx = -1;
  while (!workspace.open_heap.empty()) {
    std::ranges::pop_heap(workspace.open_heap, cmp_queue_node);
    PRAStarQueueNode queue_node = workspace.open_heap.back();
    workspace.open_heap.pop_back();

    PRAStarState& curr_state = getAStarState(workspace, queue_node.state_idx);
    if (curr_state.closed || queue_node.known_cost != curr_state.known_cost) {
      continue;
    }
    int32_t curr_cell_idx = queue_node.state_idx / 2;
    int32_t other_state_idx = queue_node.state_idx ^ 1;
    PRAStarState& other_state = getAStarState(workspace, other_state_idx);
    double dominated_cost = other_state.known_cost + corner_weight;
    if (curr_state.known_cost > dominated_cost || (curr_state.known_cost == dominated_cost && (corner_weight > 0 || queue_node.state_idx > other_state_idx))) {
      curr_state.closed = true;
      continue;
    }
    curr_state.closed = true;
    if (curr_cell_idx == end_cell_idx) {
      end_state_idx = queue_node.state_idx;
      break;
    }

    int32_t curr_local_x = curr_cell_idx / workspace.y_size;
    int32_t curr_local_y = curr_cell_idx % workspace.y_size;
    int32_t curr_x = workspace_ll_x + curr_local_x;
    int32_t curr_y = workspace_ll_y + curr_local_y;
    bool curr_is_horizontal = queue_node.state_idx % 2 == 1;
    double turn_known_cost = curr_state.known_cost + corner_weight;
    for (auto [step_x, step_y] : step_list) {
      bool is_horizontal = step_x != 0;
      if (curr_is_horizontal != is_horizontal && other_state.known_cost < turn_known_cost) {
        continue;
      }
      int32_t neighbor_local_x = curr_local_x + step_x;
      int32_t neighbor_local_y = curr_local_y + step_y;
      if (neighbor_local_x < 0 || workspace.x_size <= neighbor_local_x || neighbor_local_y < 0 || workspace.y_size <= neighbor_local_y) {
        continue;
      }

      int32_t neighbor_cell_idx = curr_cell_idx + (step_x * workspace.y_size) + step_y;
      int32_t neighbor_state_idx = (neighbor_cell_idx * 2) + (is_horizontal ? 1 : 0);
      PRAStarState& neighbor_state = getAStarState(workspace, neighbor_state_idx);
      if (neighbor_state.closed) {
        continue;
      }

      int32_t neighbor_x = curr_x + step_x;
      int32_t neighbor_y = curr_y + step_y;
      RoutingEdge& routing_edge
          = is_horizontal ? routing_h_edge_map[std::min(curr_x, neighbor_x)][curr_y] : routing_v_edge_map[curr_x][std::min(curr_y, neighbor_y)];
      const PREdgeCost& edge_cost
          = is_horizontal ? _routing_h_edge_cost_map[std::min(curr_x, neighbor_x)][curr_y] : _routing_v_edge_cost_map[curr_x][std::min(curr_y, neighbor_y)];
      bool is_owned = routing_edge_set.contains(&routing_edge);
      bool is_ignored = routing_edge.get_ignore_net_set().contains(curr_net_idx);
      if (!is_owned && routing_edge.get_supply() == 0 && !is_ignored) {
        continue;
      }
      double step_cost = is_owned ? 0 : 1.0;
      if (!is_owned && !is_ignored) {
        step_cost += edge_cost.getTotalCost(overflow_unit, routing_edge.get_congestion_cost());
      }
      if (curr_is_horizontal != is_horizontal) {
        step_cost += corner_weight;
      }
      double next_known_cost = curr_state.known_cost + step_cost;
      if (next_known_cost < neighbor_state.known_cost) {
        int32_t other_neighbor_state_idx = neighbor_state_idx ^ 1;
        PRAStarState& other_neighbor_state = getAStarState(workspace, other_neighbor_state_idx);
        double dominated_cost = other_neighbor_state.known_cost + corner_weight;
        if (next_known_cost > dominated_cost || (next_known_cost == dominated_cost && (corner_weight > 0 || neighbor_state_idx > other_neighbor_state_idx))) {
          continue;
        }
        neighbor_state.parent_state_idx = queue_node.state_idx;
        neighbor_state.known_cost = next_known_cost;
        PlanarCoord neighbor_coord(neighbor_x, neighbor_y);
        double next_estimated_cost = getAStarEstimatedCost(workspace, neighbor_coord, end_coord, has_owned_edge);
        workspace.open_heap.push_back({.state_idx = neighbor_state_idx, .known_cost = next_known_cost, .total_cost = next_known_cost + next_estimated_cost});
        std::ranges::push_heap(workspace.open_heap, cmp_queue_node);
      }
    }
  }

  if (end_state_idx == -1) {
    return false;
  }

  std::vector<PlanarCoord> coord_list;
  int32_t curr_state_idx = end_state_idx;
  while (true) {
    coord_list.push_back(getAStarStateCoord(workspace, curr_state_idx));
    if (curr_state_idx / 2 == start_cell_idx) {
      break;
    }
    curr_state_idx = getAStarState(workspace, curr_state_idx).parent_state_idx;
    if (curr_state_idx == -1) {
      return false;
    }
  }
  std::ranges::reverse(coord_list);
  routing_segment_list = getRoutingSegmentListByCoordList(coord_list);
  return !routing_segment_list.empty();
}

PlanarRect PlanarRouter::getAStarBaseRect(const Segment<PlanarCoord>& planar_topo)
{
  PlanarCoord first_coord = planar_topo.get_first();
  PlanarCoord second_coord = planar_topo.get_second();

  PlanarRect topo_rect(std::min(first_coord.get_x(), second_coord.get_x()), std::min(first_coord.get_y(), second_coord.get_y()),
                       std::max(first_coord.get_x(), second_coord.get_x()), std::max(first_coord.get_y(), second_coord.get_y()));
  PlanarRect search_rect = topo_rect;
  for (const PlanarRect& body_grid_rect : _macro_grid_rect_list) {
    if (!RTUTIL.isClosedOverlap(topo_rect, body_grid_rect) && !RTUTIL.isInside(body_grid_rect, first_coord) && !RTUTIL.isInside(body_grid_rect, second_coord)) {
      continue;
    }
    search_rect.set_ll_x(std::min(search_rect.get_ll_x(), body_grid_rect.get_ll_x()));
    search_rect.set_ll_y(std::min(search_rect.get_ll_y(), body_grid_rect.get_ll_y()));
    search_rect.set_ur_x(std::max(search_rect.get_ur_x(), body_grid_rect.get_ur_x()));
    search_rect.set_ur_y(std::max(search_rect.get_ur_y(), body_grid_rect.get_ur_y()));
  }
  return search_rect;
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

void PlanarRouter::addPRCandidateListByUPattern(std::vector<PRCandidate>& pr_candidate_list, PRModel& pr_model, Segment<PlanarCoord>& planar_topo)
{
  Die& die = RTDM.getDatabase().get_die();
  int32_t expand_step_num = pr_model.get_pr_com_param().get_expand_step_num();

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

void PlanarRouter::addPRCandidateListByOuter3Bends(std::vector<PRCandidate>& pr_candidate_list, PRModel& pr_model, Segment<PlanarCoord>& planar_topo)
{
  Die& die = RTDM.getDatabase().get_die();
  int32_t expand_step_num = pr_model.get_pr_com_param().get_expand_step_num();

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

void PlanarRouter::updatePRCandidate(PRModel& pr_model, PRCandidate& pr_candidate)
{
  double overflow_unit = pr_model.get_pr_com_param().get_overflow_unit();
  int32_t curr_net_idx = pr_model.get_curr_pr_task()->get_net_idx();

  const std::unordered_set<RoutingEdge*>& routing_edge_set = pr_model.get_curr_pr_task()->get_routing_edge_set();

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
  GridMap<RoutingEdge>& routing_h_edge_map = RTDM.getDatabase().get_planar_routing_h_edge_map();
  GridMap<RoutingEdge>& routing_v_edge_map = RTDM.getDatabase().get_planar_routing_v_edge_map();
  GridMap<PREdgeCost>& routing_h_edge_cost_map = _routing_h_edge_cost_map;
  GridMap<PREdgeCost>& routing_v_edge_cost_map = _routing_v_edge_cost_map;
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
      RoutingEdge& routing_edge = is_horizontal ? routing_h_edge_map[idx][first_y] : routing_v_edge_map[first_x][idx];
      const PREdgeCost& edge_cost = is_horizontal ? routing_h_edge_cost_map[idx][first_y] : routing_v_edge_cost_map[first_x][idx];
      bool is_owned = routing_edge_set.contains(&routing_edge);
      candidate_cost.total_wire_length += !is_owned;
      if (is_owned || routing_edge.get_ignore_net_set().contains(curr_net_idx)) {
        continue;
      }
      candidate_cost.is_path_blocked |= routing_edge.get_supply() == 0;
      candidate_cost.is_overflow |= edge_cost.is_overflow;
      candidate_cost.total_cost += edge_cost.getTotalCost(overflow_unit, routing_edge.get_congestion_cost());
      candidate_cost.saturation_edge_num += edge_cost.is_saturated;
      candidate_cost.hotspot_edge_num += edge_cost.is_hotspot;
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

void PlanarRouter::uploadNetList(PRModel& pr_model, const std::vector<PRNet*>& pr_net_list)
{
  for (PRNet* pr_net : pr_net_list) {
    for (Segment<PlanarCoord>& routing_segment : pr_net->get_routing_segment_list()) {
      pr_model.get_net_global_result_map()[pr_net->get_net_idx()].emplace_back(LayerCoord(routing_segment.get_first(), 0),
                                                                               LayerCoord(routing_segment.get_second(), 0));
    }
  }
}

// exhibit

void PlanarRouter::updateSummary(PRModel& pr_model)
{
  int32_t micron_dbu = RTDM.getDatabase().get_micron_dbu();
  ScaleAxis& gcell_axis = RTDM.getDatabase().get_gcell_axis();
  GridMap<PlanarRect>& gcell_map = RTDM.getDatabase().get_gcell_map();
  Summary& summary = RTDM.getDatabase().get_summary();
  int32_t enable_timing = RTDM.getConfig().enable_timing;

  double& total_demand = summary.pr_summary.total_demand;
  double& total_overflow = summary.pr_summary.total_overflow;
  double& total_wire_length = summary.pr_summary.total_wire_length;
  std::map<std::string, std::map<std::string, double>>& clock_timing_map = summary.pr_summary.clock_timing_map;

  std::vector<PRNet>& pr_net_list = pr_model.get_pr_net_list();

  total_demand = 0;
  total_overflow = 0;
  total_wire_length = 0;
  clock_timing_map.clear();

  for (GridMap<RoutingEdge>* routing_edge_map : {&RTDM.getDatabase().get_planar_routing_h_edge_map(), &RTDM.getDatabase().get_planar_routing_v_edge_map()}) {
    for (int32_t x = 0; x < routing_edge_map->get_x_size(); x++) {
      for (int32_t y = 0; y < routing_edge_map->get_y_size(); y++) {
        RoutingEdge& routing_edge = (*routing_edge_map)[x][y];
        total_demand += routing_edge.get_demand();
        total_overflow += routing_edge.get_overflow();
      }
    }
  }
  for (auto& [net_idx, segment_set] : pr_model.get_net_global_result_map()) {
    for (Segment<LayerCoord>& segment_value : segment_set) {
      Segment<LayerCoord>* segment = &segment_value;
      LayerCoord& first_coord = segment->get_first();
      int32_t first_layer_idx = first_coord.get_layer_idx();
      LayerCoord& second_coord = segment->get_second();
      int32_t second_layer_idx = second_coord.get_layer_idx();

      if (first_layer_idx == second_layer_idx) {
        PlanarRect& first_gcell = gcell_map[first_coord.get_x()][first_coord.get_y()];
        PlanarRect& second_gcell = gcell_map[second_coord.get_x()][second_coord.get_y()];
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
    for (auto& [net_idx, segment_set] : pr_model.get_net_global_result_map()) {
      for (Segment<LayerCoord>& segment_value : segment_set) {
        Segment<LayerCoord>* segment = &segment_value;
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

  for (auto& [net_idx, segment_set] : pr_model.get_net_global_result_map()) {
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
    for (Segment<LayerCoord>& segment_value : segment_set) {
      Segment<LayerCoord>* segment = &segment_value;
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

  for (std::pair<std::string, GridMap<RoutingEdge>*> edge_map_pair : {std::make_pair("h_net_map.csv", &RTDM.getDatabase().get_planar_routing_h_edge_map()),
                                                                      std::make_pair("v_net_map.csv", &RTDM.getDatabase().get_planar_routing_v_edge_map())}) {
    std::ofstream* net_csv_file = RTUTIL.getOutputFileStream(RTUTIL.getString(pr_temp_directory_path, edge_map_pair.first));
    GridMap<RoutingEdge>& routing_edge_map = *edge_map_pair.second;
    for (int32_t y = routing_edge_map.get_y_size() - 1; y >= 0; y--) {
      for (int32_t x = 0; x < routing_edge_map.get_x_size(); x++) {
        RTUTIL.pushStream(net_csv_file, routing_edge_map[x][y].get_demand(), ",");
      }
      RTUTIL.pushStream(net_csv_file, "\n");
    }
    RTUTIL.closeFileStream(net_csv_file);
  }
  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void PlanarRouter::outputUsageCSV(PRModel& pr_model)
{
  std::string& pr_temp_directory_path = RTDM.getConfig().pr_temp_directory_path;
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  for (std::pair<std::string, GridMap<RoutingEdge>*> edge_map_pair : {std::make_pair("h_usage_map.csv", &RTDM.getDatabase().get_planar_routing_h_edge_map()),
                                                                      std::make_pair("v_usage_map.csv", &RTDM.getDatabase().get_planar_routing_v_edge_map())}) {
    std::ofstream csv_file(pr_temp_directory_path + edge_map_pair.first);
    if (!csv_file.is_open()) {
      RTLOG.error(Loc::current(), "Failed to open file '", pr_temp_directory_path + edge_map_pair.first, "'!");
      continue;
    }
    GridMap<RoutingEdge>& routing_edge_map = *edge_map_pair.second;
    for (int32_t y = routing_edge_map.get_y_size() - 1; y >= 0; y--) {
      for (int32_t x = 0; x < routing_edge_map.get_x_size(); x++) {
        csv_file << routing_edge_map[x][y].get_demand() << ",";
      }
      csv_file << "\n";
    }
    csv_file.close();
  }
  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void PlanarRouter::outputCongestionCostCSV(PRModel& pr_model)
{
  std::string& pr_temp_directory_path = RTDM.getConfig().pr_temp_directory_path;
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  for (std::pair<std::string, GridMap<RoutingEdge>*> edge_map_pair :
       {std::make_pair("h_congestion_cost_map.csv", &RTDM.getDatabase().get_planar_routing_h_edge_map()),
        std::make_pair("v_congestion_cost_map.csv", &RTDM.getDatabase().get_planar_routing_v_edge_map())}) {
    std::ofstream csv_file(pr_temp_directory_path + edge_map_pair.first);
    if (!csv_file.is_open()) {
      RTLOG.error(Loc::current(), "Failed to open file '", pr_temp_directory_path + edge_map_pair.first, "'!");
      continue;
    }
    GridMap<RoutingEdge>& routing_edge_map = *edge_map_pair.second;
    for (int32_t y = routing_edge_map.get_y_size() - 1; y >= 0; y--) {
      for (int32_t x = 0; x < routing_edge_map.get_x_size(); x++) {
        csv_file << routing_edge_map[x][y].get_congestion_cost() << ",";
      }
      csv_file << "\n";
    }
    csv_file.close();
  }
  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

// debug

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

  // edge_axis
  {
    for (std::pair<GridMap<RoutingEdge>*, bool> edge_map_pair : {std::make_pair(&RTDM.getDatabase().get_planar_routing_h_edge_map(), true),
                                                                 std::make_pair(&RTDM.getDatabase().get_planar_routing_v_edge_map(), false)}) {
      GPStruct edge_axis_struct(edge_map_pair.second ? "h_edge_axis" : "v_edge_axis");
      GridMap<RoutingEdge>& routing_edge_map = *edge_map_pair.first;
      for (int32_t x = 0; x < routing_edge_map.get_x_size(); x++) {
        for (int32_t y = 0; y < routing_edge_map.get_y_size(); y++) {
          PlanarCoord first_grid_coord(x, y);
          PlanarCoord second_grid_coord = edge_map_pair.second ? PlanarCoord(x + 1, y) : PlanarCoord(x, y + 1);
          PlanarRect first_real_rect = RTUTIL.getRealRectByGCell(first_grid_coord, gcell_axis);
          PlanarRect second_real_rect = RTUTIL.getRealRectByGCell(second_grid_coord, gcell_axis);
          PlanarCoord first_coord = first_real_rect.getMidPoint();
          PlanarCoord second_coord = second_real_rect.getMidPoint();
          PlanarRect edge_rect = edge_map_pair.second
                                     ? PlanarRect(first_coord.get_x(), first_real_rect.get_ll_y(), second_coord.get_x(), first_real_rect.get_ur_y())
                                     : PlanarRect(first_real_rect.get_ll_x(), first_coord.get_y(), first_real_rect.get_ur_x(), second_coord.get_y());

          GPBoundary gp_boundary;
          gp_boundary.set_layer_idx(RTGP.getGDSIdxByRouting(0));
          gp_boundary.set_data_type(static_cast<int32_t>(edge_map_pair.second ? GPDataType::kHEdgeAxis : GPDataType::kVEdgeAxis));
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
    for (std::pair<GridMap<RoutingEdge>*, bool> edge_map_pair : {std::make_pair(&RTDM.getDatabase().get_planar_routing_h_edge_map(), true),
                                                                 std::make_pair(&RTDM.getDatabase().get_planar_routing_v_edge_map(), false)}) {
      GPStruct routing_edge_struct(edge_map_pair.second ? "h_edge_info" : "v_edge_info");
      GridMap<RoutingEdge>& routing_edge_map = *edge_map_pair.first;
      for (int32_t x = 0; x < routing_edge_map.get_x_size(); x++) {
        for (int32_t y = 0; y < routing_edge_map.get_y_size(); y++) {
          RoutingEdge& routing_edge = routing_edge_map[x][y];
          PlanarCoord first_grid_coord(x, y);
          PlanarCoord second_grid_coord = edge_map_pair.second ? PlanarCoord(x + 1, y) : PlanarCoord(x, y + 1);
          PlanarRect first_real_rect = RTUTIL.getRealRectByGCell(first_grid_coord, gcell_axis);
          PlanarRect second_real_rect = RTUTIL.getRealRectByGCell(second_grid_coord, gcell_axis);
          PlanarCoord first_coord = first_real_rect.getMidPoint();
          PlanarCoord second_coord = second_real_rect.getMidPoint();
          PlanarRect edge_rect = edge_map_pair.second
                                     ? PlanarRect(first_coord.get_x(), first_real_rect.get_ll_y(), second_coord.get_x(), first_real_rect.get_ur_y())
                                     : PlanarRect(first_real_rect.get_ll_x(), first_coord.get_y(), first_real_rect.get_ur_x(), second_coord.get_y());

          int32_t info_data_type = static_cast<int32_t>(edge_map_pair.second ? GPDataType::kHEdgeInfo : GPDataType::kVEdgeInfo);
          GPBoundary gp_boundary;
          gp_boundary.set_layer_idx(RTGP.getGDSIdxByRouting(0));
          gp_boundary.set_data_type(info_data_type);
          gp_boundary.set_rect(edge_rect);
          routing_edge_struct.push(gp_boundary);

          std::string ignore_net_message;
          for (int32_t net_idx : routing_edge.get_ignore_net_set()) {
            ignore_net_message += RTUTIL.getString(ignore_net_message.empty() ? "" : ",", net_idx);
          }
          std::string demand_net_message;
          for (int32_t net_idx : routing_edge.get_demand_net_idx_list()) {
            demand_net_message += RTUTIL.getString(demand_net_message.empty() ? "" : ",", net_idx);
          }
          std::vector<std::string> message_list;
          message_list.push_back(RTUTIL.getString(edge_map_pair.second ? "H" : "V", " (", first_grid_coord.get_x(), ",", first_grid_coord.get_y(), ")-(",
                                                  second_grid_coord.get_x(), ",", second_grid_coord.get_y(), ")"));
          message_list.push_back(RTUTIL.getString("demand: ", routing_edge.get_demand()));
          message_list.push_back(RTUTIL.getString("demand_net: [", demand_net_message, "]"));
          message_list.push_back(RTUTIL.getString("supply: ", routing_edge.get_supply()));
          message_list.push_back(RTUTIL.getString("ignore_net: [", ignore_net_message, "]"));
          message_list.push_back(RTUTIL.getString("congestion_cost: ", routing_edge.get_congestion_cost()));

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
  for (std::pair<GridMap<RoutingEdge>*, bool> edge_map_pair : {std::make_pair(&RTDM.getDatabase().get_planar_routing_h_edge_map(), true),
                                                               std::make_pair(&RTDM.getDatabase().get_planar_routing_v_edge_map(), false)}) {
    GridMap<RoutingEdge>& routing_edge_map = *edge_map_pair.first;
    for (int32_t x = 0; x < routing_edge_map.get_x_size(); x++) {
      for (int32_t y = 0; y < routing_edge_map.get_y_size(); y++) {
        RoutingEdge& routing_edge = routing_edge_map[x][y];
        if (routing_edge.get_overflow() <= 0) {
          continue;
        }
        PlanarCoord first_coord(x, y);
        PlanarCoord second_coord = edge_map_pair.second ? PlanarCoord(x + 1, y) : PlanarCoord(x, y + 1);
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

  std::string gds_file_path = RTUTIL.getString(pr_temp_directory_path, flag, "_pr_model.gds");
  RTGP.plot(gp_gds, gds_file_path);
}

}  // namespace irt
