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
#include "GlobalRouter.hpp"

#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "DataManager.hpp"
#include "GDSPlotter.hpp"
#include "GRCost.hpp"
#include "Logger.hpp"
#include "Monitor.hpp"
#include "Net.hpp"
#include "RoutingLayer.hpp"
#include "Segment.hpp"
#include "Utility.hpp"

namespace irt {

namespace {

void logRouteProgress(const char* label, size_t done_num, size_t total_num, size_t& next_percent)
{
  size_t percent = (done_num * 100) / total_num;
  if (percent >= next_percent || done_num == total_num) {
    RTLOG.info(Loc::current(), label, " ", done_num, "/", total_num, "(", percent, "%)");
    next_percent += 10;
  }
}

}  // namespace

GlobalRouter* GlobalRouter::gr_instance_ = nullptr;

void GlobalRouter::initInst()
{
  if (gr_instance_ == nullptr) {
    gr_instance_ = new GlobalRouter();
  }
}

GlobalRouter& GlobalRouter::getInst()
{
  if (gr_instance_ == nullptr) {
    RTLOG.error(Loc::current(), "The instance not initialized!");
  }
  return *gr_instance_;
}

void GlobalRouter::destroyInst()
{
  if (gr_instance_ != nullptr) {
    delete gr_instance_;
    gr_instance_ = nullptr;
  }
}

void GlobalRouter::route()
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  RTDM.getDatabase().get_net_global_result_map().clear();
  initGridGraph();

  analyzeSupply();
  initNetList();

  patternRoute();
  detourRoute();
  mazeRouteFailedNets();
  mazeRerouteOverflowNets();
  fallbackRoute();
  // debugPlot("after_route");

  uploadGRResult();
  RTDM.rebuildGlobalResultRTree();
  printSummary();

  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void GlobalRouter::initGridGraph()
{
  Database& database = RTDM.getDatabase();

  GRGridShape shape;
  shape.x_size = database.get_gcell_map().get_x_size();
  shape.y_size = database.get_gcell_map().get_y_size();
  shape.layer_num = static_cast<int32_t>(database.get_routing_layer_list().size());
  shape.bottom_layer_idx = RTDM.getConfig().bottom_routing_layer_idx;
  shape.top_layer_idx = RTDM.getConfig().top_routing_layer_idx;
  shape.is_horizontal_layer.assign(shape.layer_num, false);
  for (int32_t layer_idx = 0; layer_idx < shape.layer_num; layer_idx++) {
    shape.is_horizontal_layer[layer_idx] = database.get_routing_layer_list()[layer_idx].isPreferH();
  }
  grid_graph_.init(shape);
}

void GlobalRouter::analyzeSupply()
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");

  for (int32_t layer_idx = grid_graph_.getBottomLayerIdx(); layer_idx <= grid_graph_.getTopLayerIdx(); layer_idx++) {
    RoutingLayer& routing_layer = RTDM.getDatabase().get_routing_layer_list()[layer_idx];
    bool is_horizontal = routing_layer.isPreferH();
    int32_t x_size = is_horizontal ? std::max(0, grid_graph_.getXSize() - 1) : grid_graph_.getXSize();
    int32_t y_size = is_horizontal ? grid_graph_.getYSize() : std::max(0, grid_graph_.getYSize() - 1);

#pragma omp parallel for collapse(2)
    for (int32_t x = 0; x < x_size; x++) {
      for (int32_t y = 0; y < y_size; y++) {
        LayerCoord first_coord(x, y, layer_idx);
        LayerCoord second_coord = is_horizontal ? LayerCoord(x + 1, y, layer_idx) : LayerCoord(x, y + 1, layer_idx);
        EXTLayerRect search_rect = getSearchRect(first_coord, second_coord);
        std::vector<LayerRect> wire_list = getCrossingWireList(search_rect);
        std::vector<PlanarRect> enlarged_obs_rect_list;
        for (auto& [net_idx, fixed_rect_set] : RTDM.getNetFixedRectMap(true, search_rect)) {
          for (EXTLayerRect* fixed_rect : fixed_rect_set) {
            PlanarRect fixed_shape = fixed_rect->get_real_rect();
            enlarged_obs_rect_list.push_back(RTUTIL.getEnlargedRect(fixed_shape, routing_layer.getPRLSpacing(fixed_shape)));
          }
        }
        int32_t blocked_wire_num = 0;
        for (const LayerRect& wire : wire_list) {
          for (const PlanarRect& enlarged_obs_rect : enlarged_obs_rect_list) {
            if (RTUTIL.isOpenOverlap(enlarged_obs_rect, wire)) {
              blocked_wire_num++;
              break;
            }
          }
        }

        int32_t supply_num = static_cast<int32_t>(wire_list.size());
        int32_t accessible_num = supply_num - blocked_wire_num;
        // Reserve one accessible track; it and all inaccessible tracks become fixed demand.
        int32_t usable_num = std::max(0, accessible_num - 1);
        grid_graph_.setSupply(GRWireEdgeId{x, y, layer_idx}, supply_num, usable_num);
      }
    }
  }

  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

EXTLayerRect GlobalRouter::getSearchRect(const LayerCoord& first_coord, const LayerCoord& second_coord)
{
  ScaleAxis& gcell_axis = RTDM.getDatabase().get_gcell_axis();

  PlanarRect first_real_rect = RTUTIL.getRealRectByGCell(first_coord, gcell_axis);
  PlanarRect second_real_rect = RTUTIL.getRealRectByGCell(second_coord, gcell_axis);
  PlanarCoord first_mid_coord = first_real_rect.getMidPoint();
  PlanarCoord second_mid_coord = second_real_rect.getMidPoint();

  PlanarRect search_real_rect;
  if (RTUTIL.isHorizontal(first_coord, second_coord)) {
    search_real_rect.set_ll(std::min(first_mid_coord.get_x(), second_mid_coord.get_x()), std::min(first_real_rect.get_ll_y(), second_real_rect.get_ll_y()));
    search_real_rect.set_ur(std::max(first_mid_coord.get_x(), second_mid_coord.get_x()), std::max(first_real_rect.get_ur_y(), second_real_rect.get_ur_y()));
  } else if (RTUTIL.isVertical(first_coord, second_coord)) {
    search_real_rect.set_ll(std::min(first_real_rect.get_ll_x(), second_real_rect.get_ll_x()), std::min(first_mid_coord.get_y(), second_mid_coord.get_y()));
    search_real_rect.set_ur(std::max(first_real_rect.get_ur_x(), second_real_rect.get_ur_x()), std::max(first_mid_coord.get_y(), second_mid_coord.get_y()));
  } else {
    RTLOG.error(Loc::current(), "The grid_pair is not horizontal or vertical!");
  }

  EXTLayerRect search_rect;
  search_rect.set_real_rect(search_real_rect);
  search_rect.set_grid_rect(RTUTIL.getClosedGCellGridRect(search_rect.get_real_rect(), gcell_axis));
  search_rect.set_layer_idx(first_coord.get_layer_idx());
  return search_rect;
}

std::vector<LayerRect> GlobalRouter::getCrossingWireList(const EXTLayerRect& search_rect)
{
  RoutingLayer& routing_layer = RTDM.getDatabase().get_routing_layer_list()[search_rect.get_layer_idx()];
  const PlanarRect& real_rect = search_rect.get_real_rect();
  int32_t layer_idx = search_rect.get_layer_idx();
  int32_t half_wire_width = routing_layer.get_min_width() / 2;

  std::vector<LayerRect> wire_list;
  if (routing_layer.isPreferH()) {
    for (int32_t y : RTUTIL.getScaleList(real_rect.get_ll_y(), real_rect.get_ur_y(), routing_layer.getYTrackGridList())) {
      wire_list.emplace_back(real_rect.get_ll_x(), y - half_wire_width, real_rect.get_ur_x(), y + half_wire_width, layer_idx);
    }
  } else {
    for (int32_t x : RTUTIL.getScaleList(real_rect.get_ll_x(), real_rect.get_ur_x(), routing_layer.getXTrackGridList())) {
      wire_list.emplace_back(x - half_wire_width, real_rect.get_ll_y(), x + half_wire_width, real_rect.get_ur_y(), layer_idx);
    }
  }
  return wire_list;
}

void GlobalRouter::initNetList()
{
  net_list_.clear();
  std::vector<Net>& net_list = RTDM.getDatabase().get_net_list();
  RegionRoute& region_route = RTDM.getDatabase().get_region_route();

  for (Net& net : net_list) {
    if (net.get_pin_list().size() < 2) {
      continue;
    }
    if (!region_route.isActiveNet(net.get_net_idx())) {
      continue;
    }
    GRNet gr_net;
    gr_net.init(net, grid_graph_);
    net_list_.push_back(std::move(gr_net));
  }

  std::sort(net_list_.begin(), net_list_.end(), CmpGRNet());

  int32_t local_net_num = 0;
  for (const GRNet& gr_net : net_list_) {
    std::set<PlanarCoord, CmpPlanarCoordByXASC> coord_set;
    for (const GRPin& gr_pin : gr_net.get_pin_list()) {
      coord_set.insert(gr_pin.grid_coord);
    }
    if (coord_set.size() == 1) {
      local_net_num++;
    }
  }
  RTLOG.info(Loc::current(), "Global router net num: ", net_list_.size(), ", local net num: ", local_net_num);
}

void GlobalRouter::patternRoute()
{
  pattern_failed_net_idx_list_.clear();
  unrouted_net_idx_list_.clear();
  if (net_list_.empty()) {
    return;
  }
  size_t next_percent = 10;
  for (size_t i = 0; i < net_list_.size(); i++) {
    GRNet& net = net_list_[i];
    GRTree old_tree = net.release_tree();
    net.ripupUsage(grid_graph_, params_, old_tree);

    GRTree new_tree;
    if (tree_builder_.buildPattern(net, grid_graph_, params_, new_tree)) {
      net.commitUsage(grid_graph_, params_, new_tree);
      net.set_tree(std::move(new_tree));
    } else {
      net.commitUsage(grid_graph_, params_, old_tree);
      net.set_tree(std::move(old_tree));
      pattern_failed_net_idx_list_.push_back(i);
    }

    logRouteProgress("Routed", i + 1, net_list_.size(), next_percent);
  }
  RTLOG.info(Loc::current(), "Pattern route failed net num: ", pattern_failed_net_idx_list_.size(), ", left to maze route.");
}

void GlobalRouter::detourRoute()
{
  std::vector<std::pair<double, size_t>> overflow_net_list;
  for (size_t net_idx : getOverflowNetIndexList()) {
    const GRNet& net = net_list_[net_idx];
    double overflow = treeOverflow(grid_graph_, net, net.get_tree());
    if (overflow > GRParams::kEpsilon) {
      overflow_net_list.emplace_back(overflow, net_idx);
    }
  }
  if (overflow_net_list.empty()) {
    RTLOG.info(Loc::current(), "Pattern detour: no overflow net");
    return;
  }
  tree_builder_.prepareDetour(grid_graph_);
  std::sort(overflow_net_list.begin(), overflow_net_list.end(),
            [](const std::pair<double, size_t>& first, const std::pair<double, size_t>& second) { return first.first > second.first; });
  double before_overflow = grid_graph_.getTotalOverflow();
  int32_t accepted_num = 0;
  int32_t rolled_back_num = 0;
  int32_t scaffold_num = 0;
  size_t next_percent = 10;
  for (size_t i = 0; i < overflow_net_list.size(); i++) {
    GRNet& net = net_list_[overflow_net_list[i].second];
    double old_overflow = treeOverflow(grid_graph_, net, net.get_tree());
    if (old_overflow <= GRParams::kEpsilon) {
      continue;
    }
    double old_cost = treeCost(grid_graph_, net, params_, net.get_tree());

    GRTree old_tree = net.release_tree();
    net.ripupUsage(grid_graph_, params_, old_tree);

    GRTree new_tree;
    bool is_built = tree_builder_.buildDetour(net, grid_graph_, params_, new_tree);
    if (is_built && tree_builder_.wasDetourShifted()) {
      scaffold_num++;
    }
    bool is_accepted = false;
    if (is_built) {
      net.commitUsage(grid_graph_, params_, new_tree);
      double new_overflow = treeOverflow(grid_graph_, net, new_tree);
      double new_cost = treeCost(grid_graph_, net, params_, new_tree);
      is_accepted = isBetter(old_overflow, old_cost, new_overflow, new_cost);
      if (is_accepted) {
        net.set_tree(std::move(new_tree));
        accepted_num++;
      } else {
        net.ripupUsage(grid_graph_, params_, new_tree);
      }
    }
    if (!is_accepted) {
      net.commitUsage(grid_graph_, params_, old_tree);
      net.set_tree(std::move(old_tree));
      rolled_back_num++;
    }

    logRouteProgress("Pattern detoured", i + 1, overflow_net_list.size(), next_percent);
  }
  double after_overflow = grid_graph_.getTotalOverflow();
  RTLOG.info(Loc::current(), "Pattern detour: nets ", overflow_net_list.size(), ", overflow ", before_overflow, " -> ", after_overflow, ", overflow_net ",
             getOverflowNetIndexList().size(), ", accepted ", accepted_num, ", rolled back ", rolled_back_num, ", scaffold ", scaffold_num);
}

std::vector<size_t> GlobalRouter::getOverflowNetIndexList() const
{
  std::vector<size_t> net_idx_list;
  for (size_t net_idx = 0; net_idx < net_list_.size(); net_idx++) {
    if (net_list_[net_idx].hasOverflow(grid_graph_)) {
      net_idx_list.push_back(net_idx);
    }
  }
  return net_idx_list;
}

void GlobalRouter::mazeRouteFailedNets()
{
  if (pattern_failed_net_idx_list_.empty()) {
    return;
  }
  std::vector<size_t> failed_net_idx_list = std::move(pattern_failed_net_idx_list_);
  pattern_failed_net_idx_list_.clear();
  RTLOG.info(Loc::current(), "Maze routing pattern-failed nets: ", failed_net_idx_list.size());
  size_t next_percent = 10;
  for (size_t i = 0; i < failed_net_idx_list.size(); i++) {
    if (!routeNetByMaze(net_list_[failed_net_idx_list[i]])) {
      unrouted_net_idx_list_.push_back(failed_net_idx_list[i]);
    }
    logRouteProgress("Maze routed pattern-failed nets", i + 1, failed_net_idx_list.size(), next_percent);
  }
  RTLOG.info(Loc::current(), "Unrouted net num: ", unrouted_net_idx_list_.size(), ", left to blocked fallback.");
}

void GlobalRouter::mazeRerouteOverflowNets()
{
  constexpr int32_t kMaxIteration = 6;
  for (int32_t iteration = 0; iteration < kMaxIteration; iteration++) {
    std::vector<size_t> net_idx_list = getOverflowNetIndexList();
    if (net_idx_list.empty()) {
      break;
    }
    double before_overflow = grid_graph_.getTotalOverflow();
    double before_cost = getTotalRoutingCost();
    std::vector<std::pair<size_t, GRTree>> snapshot_list;
    snapshot_list.reserve(net_idx_list.size());
    size_t next_percent = 10;
    for (size_t i = 0; i < net_idx_list.size(); i++) {
      size_t net_idx = net_idx_list[i];
      snapshot_list.emplace_back(net_idx, net_list_[net_idx].get_tree());
      routeNetByMaze(net_list_[net_idx]);
      logRouteProgress("Maze routed", i + 1, net_idx_list.size(), next_percent);
    }
    double after_overflow = grid_graph_.getTotalOverflow();
    double after_cost = getTotalRoutingCost();
    // Overflow is the primary reroute objective; cost only breaks ties when overflow is unchanged.
    bool is_accepted = isBetter(before_overflow, before_cost, after_overflow, after_cost);
    RTLOG.info(Loc::current(), "Maze reroute iteration ", iteration, ": nets ", net_idx_list.size(), ", overflow ", before_overflow, " -> ", after_overflow,
               ", cost ", before_cost, " -> ", after_cost, ", cost_weight ", params_.cost_weight, is_accepted ? " (accepted)" : " (rolled back)");
    if (!is_accepted) {
      for (auto& [net_idx, old_tree] : snapshot_list) {
        GRNet& net = net_list_[net_idx];
        net.ripupUsage(grid_graph_, params_, net.get_tree());
        net.commitUsage(grid_graph_, params_, old_tree);
        net.set_tree(std::move(old_tree));
      }
      break;
    }
    params_.cost_weight *= 2.0;
    grid_graph_.updateCongestionCost(params_);
  }
}

double GlobalRouter::getTotalRoutingCost() const
{
  double total_cost = 0.0;
  for (const GRNet& net : net_list_) {
    double net_cost = treeCost(grid_graph_, net, params_, net.get_tree());
    if (net_cost >= GRParams::kInf) {
      return GRParams::kInf;
    }
    total_cost += net_cost;
  }
  return total_cost;
}

bool GlobalRouter::routeNetByMaze(GRNet& net)
{
  GRTree old_tree = net.release_tree();
  net.ripupUsage(grid_graph_, params_, old_tree);

  std::vector<std::vector<PlanarCoord>> path_list;
  GRTree new_tree;
  bool is_built = maze_.buildPathList(net, grid_graph_, params_, path_list);
  if (is_built) {
    is_built = tree_builder_.buildFromPaths(net, grid_graph_, params_, path_list, new_tree);
  }
  if (!is_built) {
    net.commitUsage(grid_graph_, params_, old_tree);
    net.set_tree(std::move(old_tree));
    RTLOG.warn(Loc::current(), "Failed to maze-route net ", net.get_origin_net()->get_net_name());
    return false;
  }
  net.commitUsage(grid_graph_, params_, new_tree);
  net.set_tree(std::move(new_tree));
  return true;
}

void GlobalRouter::fallbackRoute()
{
  if (unrouted_net_idx_list_.empty()) {
    return;
  }
  RTLOG.info(Loc::current(), "Fallback routing ", unrouted_net_idx_list_.size(), " nets that have no maze path.");
  for (size_t net_idx : unrouted_net_idx_list_) {
    GRNet& net = net_list_[net_idx];
    GRTree old_tree = net.release_tree();
    net.ripupUsage(grid_graph_, params_, old_tree);
    GRTree new_tree;
    tree_builder_.buildDirect(net, grid_graph_, new_tree);
    net.commitUsage(grid_graph_, params_, new_tree);
    net.set_tree(std::move(new_tree));
    RTLOG.warn(Loc::current(), "Net ", net.get_origin_net()->get_net_name(), " has no maze path, connected by straight lines ignoring supply.");
  }
  unrouted_net_idx_list_.clear();
}

void GlobalRouter::uploadGRResult()
{
  for (GRNet& net : net_list_) {
    int32_t net_idx = net.get_origin_net()->get_net_idx();
    std::vector<Segment<LayerCoord>>& result_list = RTDM.getDatabase().get_net_global_result_map()[net_idx];
    result_list.reserve(result_list.size() + net.get_tree().wire_list.size() + net.get_tree().via_list.size());
    for (const GRWireSegment& wire_segment : net.get_tree().wire_list) {
      if (wire_segment.first == wire_segment.second) {
        continue;
      }
      result_list.emplace_back(wire_segment.first, wire_segment.second);
    }
    for (const GRViaStack& via_stack : net.get_tree().via_list) {
      if (via_stack.bottom_layer_idx >= via_stack.top_layer_idx) {
        continue;
      }
      result_list.emplace_back(LayerCoord(via_stack.coord, via_stack.bottom_layer_idx), LayerCoord(via_stack.coord, via_stack.top_layer_idx));
    }
  }
}

void GlobalRouter::printSummary() const
{
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();

  double total_supply = 0.0;
  double total_demand = 0.0;
  double total_overflow = 0.0;
  double max_overflow = 0.0;
  int32_t overflow_edge_num = 0;

  fort::char_table summary_table;
  summary_table.set_cell_text_align(fort::text_align::right);
  summary_table << fort::header << "routing"
                << "supply"
                << "demand"
                << "overflow"
                << "max_overflow"
                << "overflow_edge" << fort::endr;

  for (int32_t layer_idx = grid_graph_.getBottomLayerIdx(); layer_idx <= grid_graph_.getTopLayerIdx(); layer_idx++) {
    GRLayerUsage layer_usage = grid_graph_.getLayerUsage(layer_idx);
    summary_table << routing_layer_list[layer_idx].get_layer_name() << layer_usage.supply << layer_usage.demand << layer_usage.overflow
                  << layer_usage.max_overflow << layer_usage.overflow_edge_num << fort::endr;
    total_supply += layer_usage.supply;
    total_demand += layer_usage.demand;
    total_overflow += layer_usage.overflow;
    max_overflow = std::max(max_overflow, layer_usage.max_overflow);
    overflow_edge_num += layer_usage.overflow_edge_num;
  }
  summary_table << fort::header << "Total" << total_supply << total_demand << total_overflow << max_overflow << overflow_edge_num << fort::endr;

  RTUTIL.printTableList({summary_table});
}

bool GlobalRouter::isLayerInRange(int32_t layer_idx) const
{
  return layer_idx >= grid_graph_.getBottomLayerIdx() && layer_idx <= grid_graph_.getTopLayerIdx();
}

void GlobalRouter::debugPlot(const std::string& flag) const
{
  ScaleAxis& gcell_axis = RTDM.getDatabase().get_gcell_axis();
  Die& die = RTDM.getDatabase().get_die();
  std::vector<RoutingLayer>& routing_layer_list = RTDM.getDatabase().get_routing_layer_list();

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

  // track_axis
  {
    GPStruct track_axis_struct("track_axis_struct");
    for (RoutingLayer& routing_layer : routing_layer_list) {
      if (!isLayerInRange(routing_layer.get_layer_idx())) {
        continue;
      }
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
      if (!isLayerInRange(layer_idx)) {
        continue;
      }
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
      if (!isLayerInRange(access_point->get_layer_idx())) {
        continue;
      }
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

  // global_result
  for (auto& [net_idx, segment_set] : RTDM.getNetGlobalResultMap(die)) {
    GPStruct global_result_struct(RTUTIL.getString("global_result(net_", net_idx, ")"));
    for (Segment<LayerCoord>& segment_value : segment_set) {
      Segment<LayerCoord>* segment = &segment_value;
      for (NetShape& net_shape : RTDM.getNetGlobalShapeList(net_idx, *segment)) {
        if (!isLayerInRange(net_shape.get_layer_idx())) {
          continue;
        }
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

  // edge_info
  {
    GPStruct edge_info_struct("edge_info");
    for (int32_t layer_idx = grid_graph_.getBottomLayerIdx(); layer_idx <= grid_graph_.getTopLayerIdx(); layer_idx++) {
      const GridMap<GRWireEdge>& edge_map = grid_graph_.getEdgeMap(layer_idx);
      bool is_horizontal = grid_graph_.isHorizontalLayer(layer_idx);
      int32_t info_data_type = static_cast<int32_t>(is_horizontal ? GPDataType::kHEdgeInfo : GPDataType::kVEdgeInfo);
      for (int32_t x = 0; x < edge_map.get_x_size(); x++) {
        for (int32_t y = 0; y < edge_map.get_y_size(); y++) {
          const GRWireEdge& edge = edge_map[x][y];
          PlanarCoord first_grid_coord(x, y);
          PlanarCoord second_grid_coord = is_horizontal ? PlanarCoord(x + 1, y) : PlanarCoord(x, y + 1);
          PlanarRect first_gcell_rect = RTUTIL.getRealRectByGCell(first_grid_coord, gcell_axis);
          PlanarRect second_gcell_rect = RTUTIL.getRealRectByGCell(second_grid_coord, gcell_axis);
          PlanarCoord first_coord = first_gcell_rect.getMidPoint();
          PlanarCoord second_coord = second_gcell_rect.getMidPoint();
          PlanarRect edge_rect = is_horizontal
                                     ? PlanarRect(first_coord.get_x(), first_gcell_rect.get_ll_y(), second_coord.get_x(), first_gcell_rect.get_ur_y())
                                     : PlanarRect(first_gcell_rect.get_ll_x(), first_coord.get_y(), first_gcell_rect.get_ur_x(), second_coord.get_y());

          GPBoundary gp_boundary;
          gp_boundary.set_layer_idx(RTGP.getGDSIdxByRouting(layer_idx));
          gp_boundary.set_data_type(info_data_type);
          gp_boundary.set_rect(edge_rect);
          edge_info_struct.push(gp_boundary);

          std::vector<std::string> info_line_list;
          info_line_list.push_back(
              RTUTIL.getString(is_horizontal ? "H" : "V", " (", x, ",", y, ")-(", second_grid_coord.get_x(), ",", second_grid_coord.get_y(), ")"));
          info_line_list.push_back(RTUTIL.getString("overflow: ", edge.getOverflow(), ", supply: ", edge.getUsableSupply(), ", demand: ", edge.getDemand(),
                                                    ", congestion_cost: ", edge.congestion_cost));
          info_line_list.push_back(RTUTIL.getString("fixed_demand: ", edge.fixed_demand));
          int32_t line_spacing = std::max(1, edge_rect.getYSpan() / 8);
          int32_t text_y = edge_rect.get_ur_y();
          for (const std::string& info_line : info_line_list) {
            text_y -= line_spacing;
            GPText gp_text;
            gp_text.set_coord(edge_rect.get_ll_x(), text_y);
            gp_text.set_text_type(info_data_type);
            gp_text.set_message(info_line);
            gp_text.set_layer_idx(RTGP.getGDSIdxByRouting(layer_idx));
            gp_text.set_presentation(GPTextPresentation::kLeftMiddle);
            edge_info_struct.push(gp_text);
          }
        }
      }
    }
    gp_gds.addStruct(edge_info_struct);
  }

  // overflow
  {
    GPStruct overflow_struct("overflow");
    for (int32_t layer_idx = grid_graph_.getBottomLayerIdx(); layer_idx <= grid_graph_.getTopLayerIdx(); layer_idx++) {
      const GridMap<GRWireEdge>& edge_map = grid_graph_.getEdgeMap(layer_idx);
      bool is_horizontal = grid_graph_.isHorizontalLayer(layer_idx);
      for (int32_t x = 0; x < edge_map.get_x_size(); x++) {
        for (int32_t y = 0; y < edge_map.get_y_size(); y++) {
          const GRWireEdge& edge = edge_map[x][y];
          if (edge.getOverflow() <= GRParams::kEpsilon) {
            continue;
          }
          PlanarRect first_gcell_rect = RTUTIL.getRealRectByGCell(PlanarCoord(x, y), gcell_axis);
          PlanarCoord first_coord = first_gcell_rect.getMidPoint();
          PlanarCoord second_grid_coord = is_horizontal ? PlanarCoord(x + 1, y) : PlanarCoord(x, y + 1);
          PlanarCoord second_coord = RTUTIL.getRealRectByGCell(second_grid_coord, gcell_axis).getMidPoint();

          GPPath gp_path;
          gp_path.set_data_type(static_cast<int32_t>(GPDataType::kOverflow));
          gp_path.set_layer_idx(RTGP.getGDSIdxByRouting(layer_idx));
          gp_path.set_width(point_size);
          gp_path.set_segment(first_coord, second_coord);
          overflow_struct.push(gp_path);
        }
      }
    }
    gp_gds.addStruct(overflow_struct);
  }

  std::string gr_temp_directory_path = RTUTIL.getString(RTDM.getConfig().temp_directory_path, "global_router/");
  RTUTIL.createDir(gr_temp_directory_path);
  std::string gds_file_path = RTUTIL.getString(gr_temp_directory_path, flag, "_gr_model.gds");
  RTGP.plot(gp_gds, gds_file_path);
  RTLOG.info(Loc::current(), "GR debug GDS: ", gds_file_path);
}

}  // namespace irt
