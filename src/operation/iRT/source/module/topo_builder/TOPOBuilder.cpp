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

#include "TOPOBuilder.hpp"

#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <utility>

#include "Logger.hpp"
#include "Monitor.hpp"
#include "flute3/flute.h"

namespace irt {

namespace {

using PlanarTopo = std::vector<Segment<PlanarCoord>>;
using NeighborList = std::vector<std::vector<int32_t>>;

constexpr double kCostEpsilon = 1e-9;
constexpr int32_t kRepairExtraRadius = 2;
constexpr double kMaxWarpStretch = 8.0;
constexpr int64_t kWarpScale = 100;
constexpr int32_t kMaxAxisSampleNum = 64;

enum class TBAxis
{
  kX,
  kY
};

struct TBGapCostStat
{
  long double finite_cost_sum = 0;
  int64_t edge_num = 0;
  int64_t inf_edge_num = 0;
};

struct TBAxisCostStat
{
  std::vector<TBGapCostStat> gap_stat_list;
  double min_positive_cost = std::numeric_limits<double>::infinity();
};

struct TBTopoCandidate
{
  PlanarTopo topo_list;
  TBRefineStat refine_stat;
  double cost = std::numeric_limits<double>::infinity();
};

struct TBSteinerShift
{
  int32_t first_idx = -1;
  int32_t second_idx = -1;
  PlanarCoord first_coord;
  PlanarCoord second_coord;
  double gain = -1;

  bool isValid() const { return first_idx >= 0; }
};

int32_t getBranchNum(const Flute::Tree& tree)
{
  return std::max(0, 2 * tree.deg - 2);
}

PlanarCoord getBranchCoord(const Flute::Tree& tree, int32_t branch_idx)
{
  return PlanarCoord(tree.branch[branch_idx].x, tree.branch[branch_idx].y);
}

void setBranchCoord(Flute::Tree& tree, int32_t branch_idx, const PlanarCoord& coord)
{
  tree.branch[branch_idx].x = coord.get_x();
  tree.branch[branch_idx].y = coord.get_y();
}

double addCost(double first, double second)
{
  return std::isfinite(first) && std::isfinite(second) ? first + second : std::numeric_limits<double>::infinity();
}

double getSegmentCost(const TBTask& task, const PlanarCoord& first, const PlanarCoord& second)
{
  if (first == second) {
    return 0;
  }
  if (first.get_x() != second.get_x() && first.get_y() != second.get_y()) {
    return std::numeric_limits<double>::infinity();
  }
  if (!task.has_segment_cost_query()) {
    return std::abs(first.get_x() - second.get_x()) + std::abs(first.get_y() - second.get_y());
  }
  double cost = task.get_segment_cost(first, second);
  return std::isnan(cost) || cost < 0 ? std::numeric_limits<double>::infinity() : cost;
}

double getPatternCost(const TBTask& task, const PlanarCoord& first, const PlanarCoord& second)
{
  PlanarCoord x_bend(second.get_x(), first.get_y());
  PlanarCoord y_bend(first.get_x(), second.get_y());
  return std::min(addCost(getSegmentCost(task, first, x_bend), getSegmentCost(task, x_bend, second)),
                  addCost(getSegmentCost(task, first, y_bend), getSegmentCost(task, y_bend, second)));
}

bool isInsideSearchRegion(const TBTask& task, const PlanarCoord& coord)
{
  if (!task.has_planar_search_region()) {
    return true;
  }
  const PlanarRect& region = task.get_planar_search_region();
  return region.get_ll_x() <= coord.get_x() && coord.get_x() <= region.get_ur_x() && region.get_ll_y() <= coord.get_y()
         && coord.get_y() <= region.get_ur_y();
}

NeighborList getNeighborList(const Flute::Tree& tree)
{
  int32_t branch_num = getBranchNum(tree);
  NeighborList neighbor_list(branch_num);
  for (int32_t i = 0; i < branch_num; i++) {
    int32_t neighbor_idx = tree.branch[i].n;
    if (neighbor_idx < 0 || branch_num <= neighbor_idx || neighbor_idx == i) {
      continue;
    }
    neighbor_list[i].push_back(neighbor_idx);
    neighbor_list[neighbor_idx].push_back(i);
  }
  return neighbor_list;
}

double getIncidentEdgeCost(const TBTask& task, const Flute::Tree& tree, const NeighborList& neighbor_list, int32_t first_idx, int32_t second_idx,
                           const PlanarCoord& first_coord, const PlanarCoord& second_coord)
{
  double cost = getPatternCost(task, first_coord, second_coord);
  for (int32_t neighbor_idx : neighbor_list[first_idx]) {
    if (neighbor_idx != second_idx) {
      cost = addCost(cost, getPatternCost(task, first_coord, getBranchCoord(tree, neighbor_idx)));
    }
  }
  for (int32_t neighbor_idx : neighbor_list[second_idx]) {
    if (neighbor_idx != first_idx) {
      cost = addCost(cost, getPatternCost(task, second_coord, getBranchCoord(tree, neighbor_idx)));
    }
  }
  return cost;
}

bool isStrictlyBetterCost(double current_cost, double candidate_cost)
{
  return std::isfinite(candidate_cost) && (!std::isfinite(current_cost) || candidate_cost + kCostEpsilon < current_cost);
}

std::pair<int32_t, int32_t> getBranchShiftRange(const Flute::Tree& tree, const NeighborList& neighbor_list, int32_t branch_idx,
                                                bool is_horizontal)
{
  PlanarCoord branch_coord = getBranchCoord(tree, branch_idx);
  int32_t lower = is_horizontal ? branch_coord.get_y() : branch_coord.get_x();
  int32_t upper = lower;
  for (int32_t neighbor_idx : neighbor_list[branch_idx]) {
    PlanarCoord neighbor_coord = getBranchCoord(tree, neighbor_idx);
    int32_t value = is_horizontal ? neighbor_coord.get_y() : neighbor_coord.get_x();
    lower = std::min(lower, value);
    upper = std::max(upper, value);
  }
  return {lower, upper};
}

void setShiftCoord(PlanarCoord& coord, bool is_horizontal, int32_t value)
{
  if (is_horizontal) {
    coord.set_y(value);
  } else {
    coord.set_x(value);
  }
}

bool shiftBestSteinerEdge(const TBTask& task, Flute::Tree& tree)
{
  NeighborList neighbor_list = getNeighborList(tree);
  TBSteinerShift best_shift;

  for (int32_t first_idx = tree.deg; first_idx < getBranchNum(tree); first_idx++) {
    int32_t second_idx = tree.branch[first_idx].n;
    if (second_idx < tree.deg || second_idx == first_idx) {
      continue;
    }
    PlanarCoord first_coord = getBranchCoord(tree, first_idx);
    PlanarCoord second_coord = getBranchCoord(tree, second_idx);
    bool is_horizontal = first_coord.get_y() == second_coord.get_y() && first_coord.get_x() != second_coord.get_x();
    bool is_vertical = first_coord.get_x() == second_coord.get_x() && first_coord.get_y() != second_coord.get_y();
    if (!is_horizontal && !is_vertical) {
      continue;
    }

    auto [first_lower, first_upper] = getBranchShiftRange(tree, neighbor_list, first_idx, is_horizontal);
    auto [second_lower, second_upper] = getBranchShiftRange(tree, neighbor_list, second_idx, is_horizontal);
    int32_t lower = std::max(first_lower, second_lower);
    int32_t upper = std::min(first_upper, second_upper);
    double current_cost = getIncidentEdgeCost(task, tree, neighbor_list, first_idx, second_idx, first_coord, second_coord);
    for (int32_t value = lower; value <= upper; value++) {
      PlanarCoord candidate_first = first_coord;
      PlanarCoord candidate_second = second_coord;
      setShiftCoord(candidate_first, is_horizontal, value);
      setShiftCoord(candidate_second, is_horizontal, value);
      if (candidate_first == first_coord || !isInsideSearchRegion(task, candidate_first) || !isInsideSearchRegion(task, candidate_second)) {
        continue;
      }
      double candidate_cost = getIncidentEdgeCost(task, tree, neighbor_list, first_idx, second_idx, candidate_first, candidate_second);
      if (!isStrictlyBetterCost(current_cost, candidate_cost)) {
        continue;
      }
      double gain = std::isfinite(current_cost) ? current_cost - candidate_cost : std::numeric_limits<double>::infinity();
      if (!best_shift.isValid() || gain > best_shift.gain + kCostEpsilon) {
        best_shift = {first_idx, second_idx, candidate_first, candidate_second, gain};
      }
    }
  }
  if (best_shift.isValid()) {
    setBranchCoord(tree, best_shift.first_idx, best_shift.first_coord);
    setBranchCoord(tree, best_shift.second_idx, best_shift.second_coord);
  }
  return best_shift.isValid();
}

double getEscapeCost(const TBTask& task, const PlanarCoord& coord)
{
  double cost = std::numeric_limits<double>::infinity();
  for (const PlanarCoord& neighbor : {PlanarCoord(coord.get_x() - 1, coord.get_y()), PlanarCoord(coord.get_x() + 1, coord.get_y()),
                                      PlanarCoord(coord.get_x(), coord.get_y() - 1), PlanarCoord(coord.get_x(), coord.get_y() + 1)}) {
    if (isInsideSearchRegion(task, neighbor)) {
      cost = std::min(cost, getSegmentCost(task, coord, neighbor));
    }
  }
  return cost;
}

std::optional<PlanarCoord> findRepairCoord(const TBTask& task, const PlanarCoord& raw_coord)
{
  if (!task.has_planar_search_region() || task.get_planar_search_region().isIncorrect()) {
    return std::nullopt;
  }
  const PlanarRect& region = task.get_planar_search_region();
  int32_t max_radius = 0;
  for (const PlanarCoord& corner : {region.get_ll(), PlanarCoord(region.get_ll_x(), region.get_ur_y()),
                                    PlanarCoord(region.get_ur_x(), region.get_ll_y()), region.get_ur()}) {
    max_radius = std::max(max_radius, std::abs(raw_coord.get_x() - corner.get_x()) + std::abs(raw_coord.get_y() - corner.get_y()));
  }

  int32_t found_radius = -1;
  double best_cost = std::numeric_limits<double>::infinity();
  std::optional<PlanarCoord> best_coord;
  auto updateBest = [&](const PlanarCoord& candidate, int32_t radius) {
    if (!isInsideSearchRegion(task, candidate)) {
      return;
    }
    double escape_cost = getEscapeCost(task, candidate);
    if (!std::isfinite(escape_cost)) {
      return;
    }
    double candidate_cost = escape_cost + radius;
    if (!best_coord.has_value() || candidate_cost + kCostEpsilon < best_cost
        || (std::abs(candidate_cost - best_cost) <= kCostEpsilon && CmpPlanarCoordByXASC()(candidate, *best_coord))) {
      found_radius = found_radius == -1 ? radius : found_radius;
      best_cost = candidate_cost;
      best_coord = candidate;
    }
  };
  for (int32_t radius = 1; radius <= max_radius && (found_radius == -1 || radius <= found_radius + kRepairExtraRadius); radius++) {
    for (int32_t dx = -radius; dx <= radius; dx++) {
      updateBest(PlanarCoord(raw_coord.get_x() + dx, raw_coord.get_y() - radius), radius);
      updateBest(PlanarCoord(raw_coord.get_x() + dx, raw_coord.get_y() + radius), radius);
    }
    for (int32_t dy = -radius + 1; dy < radius; dy++) {
      updateBest(PlanarCoord(raw_coord.get_x() - radius, raw_coord.get_y() + dy), radius);
      updateBest(PlanarCoord(raw_coord.get_x() + radius, raw_coord.get_y() + dy), radius);
    }
  }
  return best_coord;
}

void repairIsolatedSteiner(const TBTask& task, Flute::Tree& tree, TBRefineStat& stat)
{
  if (!task.has_segment_cost_query()) {
    return;
  }
  std::map<PlanarCoord, std::vector<int32_t>, CmpPlanarCoordByXASC> coord_branch_map;
  for (int32_t branch_idx = tree.deg; branch_idx < getBranchNum(tree); branch_idx++) {
    coord_branch_map[getBranchCoord(tree, branch_idx)].push_back(branch_idx);
  }
  for (const auto& [raw_coord, branch_idx_list] : coord_branch_map) {
    if (std::isfinite(getEscapeCost(task, raw_coord))) {
      continue;
    }
    stat.isolated_steiner_num++;
    std::optional<PlanarCoord> repair_coord = findRepairCoord(task, raw_coord);
    if (!repair_coord.has_value()) {
      stat.failed_repair_num++;
      continue;
    }
    for (int32_t branch_idx : branch_idx_list) {
      setBranchCoord(tree, branch_idx, *repair_coord);
    }
    stat.repaired_steiner_num++;
  }
}

void refineFluteTree(const TBTask& task, Flute::Tree& tree, TBRefineStat& stat)
{
  if (!task.has_segment_cost_query()) {
    return;
  }
  int32_t max_shift_num = std::max(0, 2 * (tree.deg - 2));
  while (stat.shifted_edge_num < max_shift_num && shiftBestSteinerEdge(task, tree)) {
    stat.shifted_edge_num++;
  }
  repairIsolatedSteiner(task, tree, stat);
}

std::vector<int32_t> getUniqueAxisList(const std::vector<PlanarCoord>& coord_list, TBAxis axis)
{
  std::vector<int32_t> axis_list;
  axis_list.reserve(coord_list.size());
  for (const PlanarCoord& coord : coord_list) {
    axis_list.push_back(axis == TBAxis::kX ? coord.get_x() : coord.get_y());
  }
  std::ranges::sort(axis_list);
  axis_list.erase(std::ranges::unique(axis_list).begin(), axis_list.end());
  return axis_list;
}

std::vector<int32_t> getSampleCoordList(const std::vector<int32_t>& axis)
{
  int64_t span = static_cast<int64_t>(axis.back()) - axis.front();
  int32_t sample_num = static_cast<int32_t>(std::min<int64_t>(span + 1, kMaxAxisSampleNum));
  std::vector<int32_t> sample_list;
  sample_list.reserve(sample_num);
  for (int32_t sample_idx = 0; sample_idx < sample_num; sample_idx++) {
    int64_t offset = sample_num == 1 ? 0 : span * sample_idx / (sample_num - 1);
    sample_list.push_back(static_cast<int32_t>(axis.front() + offset));
  }
  return sample_list;
}

TBAxisCostStat getAxisCostStat(const TBTask& task, const std::vector<int32_t>& axis, const std::vector<int32_t>& orth_axis, TBAxis direction)
{
  TBAxisCostStat stat;
  if (axis.size() <= 1 || orth_axis.empty()) {
    return stat;
  }
  size_t gap_num = axis.size() - 1;
  stat.gap_stat_list.resize(gap_num);
  std::vector<int32_t> sample_coord_list = getSampleCoordList(orth_axis);
  bool is_horizontal = direction == TBAxis::kX;
  for (size_t gap_idx = 0; gap_idx < gap_num; gap_idx++) {
    TBGapCostStat& gap_stat = stat.gap_stat_list[gap_idx];
    for (int64_t axis_coord = axis[gap_idx]; axis_coord < axis[gap_idx + 1]; axis_coord++) {
      for (int32_t orth_coord : sample_coord_list) {
        PlanarCoord first = is_horizontal ? PlanarCoord(static_cast<int32_t>(axis_coord), orth_coord)
                                          : PlanarCoord(orth_coord, static_cast<int32_t>(axis_coord));
        PlanarCoord second = is_horizontal ? PlanarCoord(static_cast<int32_t>(axis_coord + 1), orth_coord)
                                           : PlanarCoord(orth_coord, static_cast<int32_t>(axis_coord + 1));
        double cost = getSegmentCost(task, first, second);
        gap_stat.edge_num++;
        if (!std::isfinite(cost)) {
          gap_stat.inf_edge_num++;
          continue;
        }
        gap_stat.finite_cost_sum += cost;
        if (cost > kCostEpsilon) {
          stat.min_positive_cost = std::min(stat.min_positive_cost, cost);
        }
      }
    }
  }
  return stat;
}

bool buildWarpedAxis(const std::vector<int32_t>& raw_axis, const TBAxisCostStat& cost_stat, double reference_cost,
                     std::vector<Flute::DTYPE>& warped_axis)
{
  if (raw_axis.empty()) {
    return false;
  }
  warped_axis.assign(raw_axis.size(), 0);
  constexpr int64_t max_warp_coord = std::numeric_limits<Flute::DTYPE>::max() / 4;
  for (size_t gap_idx = 0; gap_idx + 1 < raw_axis.size(); gap_idx++) {
    const TBGapCostStat& gap_stat = cost_stat.gap_stat_list[gap_idx];
    long double density = (gap_stat.finite_cost_sum + gap_stat.inf_edge_num * reference_cost * kMaxWarpStretch) / gap_stat.edge_num;
    long double stretch = std::clamp(density / reference_cost, static_cast<long double>(1), static_cast<long double>(kMaxWarpStretch));
    int64_t axis_delta = static_cast<int64_t>(raw_axis[gap_idx + 1]) - raw_axis[gap_idx];
    long double raw_delta = static_cast<long double>(axis_delta) * kWarpScale * stretch;
    if (!std::isfinite(raw_delta) || raw_delta > max_warp_coord - warped_axis[gap_idx]) {
      return false;
    }
    int64_t warped_delta = std::max<int64_t>(1, std::llround(raw_delta));
    warped_axis[gap_idx + 1] = static_cast<Flute::DTYPE>(warped_axis[gap_idx] + warped_delta);
  }
  return true;
}

int32_t getAxisIndex(const std::vector<int32_t>& axis, int32_t value)
{
  auto iter = std::ranges::lower_bound(axis, value);
  return iter != axis.end() && *iter == value ? static_cast<int32_t>(iter - axis.begin()) : -1;
}

bool restoreRawCoordinates(Flute::Tree& tree, const std::vector<int32_t>& raw_x_axis, const std::vector<int32_t>& raw_y_axis,
                           const std::vector<Flute::DTYPE>& warped_x_axis, const std::vector<Flute::DTYPE>& warped_y_axis)
{
  for (int32_t branch_idx = 0; branch_idx < getBranchNum(tree); branch_idx++) {
    int32_t x_idx = getAxisIndex(warped_x_axis, tree.branch[branch_idx].x);
    int32_t y_idx = getAxisIndex(warped_y_axis, tree.branch[branch_idx].y);
    if (x_idx < 0 || y_idx < 0 || tree.branch[branch_idx].n < 0 || getBranchNum(tree) <= tree.branch[branch_idx].n) {
      return false;
    }
    setBranchCoord(tree, branch_idx, PlanarCoord(raw_x_axis[x_idx], raw_y_axis[y_idx]));
  }
  return true;
}

PlanarTopo getTopoListByTree(const Flute::Tree& tree)
{
  PlanarTopo topo_list;
  topo_list.reserve(getBranchNum(tree));
  for (int32_t branch_idx = 0; branch_idx < getBranchNum(tree); branch_idx++) {
    PlanarCoord first = getBranchCoord(tree, branch_idx);
    PlanarCoord second = getBranchCoord(tree, tree.branch[branch_idx].n);
    if (first != second) {
      topo_list.emplace_back(first, second);
    }
  }
  return topo_list;
}

double getTopoCost(const TBTask& task, const PlanarTopo& topo_list)
{
  double cost = 0;
  for (const Segment<PlanarCoord>& segment : topo_list) {
    cost = addCost(cost, getPatternCost(task, segment.get_first(), segment.get_second()));
  }
  return cost;
}

TBTopoCandidate finalizeCandidate(const TBTask& task, Flute::Tree& tree)
{
  TBTopoCandidate candidate;
  refineFluteTree(task, tree, candidate.refine_stat);
  candidate.topo_list = getTopoListByTree(tree);
  candidate.cost = getTopoCost(task, candidate.topo_list);
  return candidate;
}

TBTopoCandidate buildBaselineCandidate(const TBTask& task)
{
  const std::vector<PlanarCoord>& coord_list = task.get_planar_coord_list();
  std::vector<Flute::DTYPE> x_list(coord_list.size());
  std::vector<Flute::DTYPE> y_list(coord_list.size());
  for (size_t coord_idx = 0; coord_idx < coord_list.size(); coord_idx++) {
    x_list[coord_idx] = coord_list[coord_idx].get_x();
    y_list[coord_idx] = coord_list[coord_idx].get_y();
  }
  Flute::Tree tree = Flute::flute(static_cast<int32_t>(coord_list.size()), x_list.data(), y_list.data(), FLUTE_ACCURACY);
  TBTopoCandidate candidate = finalizeCandidate(task, tree);
  Flute::free_tree(tree);
  return candidate;
}

std::optional<TBTopoCandidate> buildCongestionCandidate(const TBTask& task)
{
  const std::vector<PlanarCoord>& coord_list = task.get_planar_coord_list();
  std::vector<int32_t> raw_x_axis = getUniqueAxisList(coord_list, TBAxis::kX);
  std::vector<int32_t> raw_y_axis = getUniqueAxisList(coord_list, TBAxis::kY);
  constexpr int64_t max_warp_coord = std::numeric_limits<Flute::DTYPE>::max() / 4;
  auto is_axis_warpable = [](const std::vector<int32_t>& axis) {
    int64_t span = static_cast<int64_t>(axis.back()) - axis.front();
    return span <= max_warp_coord / kWarpScale;
  };
  if (!is_axis_warpable(raw_x_axis) || !is_axis_warpable(raw_y_axis)) {
    return std::nullopt;
  }
  TBAxisCostStat x_cost_stat = getAxisCostStat(task, raw_x_axis, raw_y_axis, TBAxis::kX);
  TBAxisCostStat y_cost_stat = getAxisCostStat(task, raw_y_axis, raw_x_axis, TBAxis::kY);
  double reference_cost = std::min(x_cost_stat.min_positive_cost, y_cost_stat.min_positive_cost);
  if (!std::isfinite(reference_cost)) {
    return std::nullopt;
  }

  std::vector<Flute::DTYPE> warped_x_axis;
  std::vector<Flute::DTYPE> warped_y_axis;
  if (!buildWarpedAxis(raw_x_axis, x_cost_stat, reference_cost, warped_x_axis)
      || !buildWarpedAxis(raw_y_axis, y_cost_stat, reference_cost, warped_y_axis)) {
    return std::nullopt;
  }

  std::vector<Flute::DTYPE> x_list(coord_list.size());
  std::vector<Flute::DTYPE> y_list(coord_list.size());
  for (size_t coord_idx = 0; coord_idx < coord_list.size(); coord_idx++) {
    x_list[coord_idx] = warped_x_axis[getAxisIndex(raw_x_axis, coord_list[coord_idx].get_x())];
    y_list[coord_idx] = warped_y_axis[getAxisIndex(raw_y_axis, coord_list[coord_idx].get_y())];
  }

  Flute::Tree tree = Flute::flute(static_cast<int32_t>(coord_list.size()), x_list.data(), y_list.data(), FLUTE_ACCURACY);
  bool is_mapped = restoreRawCoordinates(tree, raw_x_axis, raw_y_axis, warped_x_axis, warped_y_axis);
  TBTopoCandidate candidate;
  if (is_mapped) {
    candidate = finalizeCandidate(task, tree);
  }
  Flute::free_tree(tree);
  return is_mapped ? std::optional<TBTopoCandidate>(std::move(candidate)) : std::nullopt;
}

}  // namespace

// public

void TOPOBuilder::initInst()
{
  if (_tb_instance == nullptr) {
    _tb_instance = new TOPOBuilder();
  }
}

TOPOBuilder& TOPOBuilder::getInst()
{
  if (_tb_instance == nullptr) {
    RTLOG.error(Loc::current(), "The instance not initialized!");
  }
  return *_tb_instance;
}

void TOPOBuilder::destroyInst()
{
  if (_tb_instance != nullptr) {
    delete _tb_instance;
    _tb_instance = nullptr;
  }
}

void TOPOBuilder::init()
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");
  Flute::readLUT();
  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

std::vector<Segment<PlanarCoord>> TOPOBuilder::getPlanarTopoList(const TBTask& task)
{
  TBRefineStat stat;
  return getPlanarTopoList(task, stat);
}

std::vector<Segment<PlanarCoord>> TOPOBuilder::getPlanarTopoList(const TBTask& task, TBRefineStat& stat)
{
  stat = {};
  const std::vector<PlanarCoord>& coord_list = task.get_planar_coord_list();
  if (coord_list.size() <= 1) {
    return {};
  }

  TBTopoCandidate selected_candidate = buildBaselineCandidate(task);
  bool attempted_congestion_flute = task.is_congestion_driven() && coord_list.size() > 3 && task.has_segment_cost_query();
  bool used_congestion_flute = false;
  if (attempted_congestion_flute) {
    std::optional<TBTopoCandidate> congestion_candidate = buildCongestionCandidate(task);
    if (congestion_candidate.has_value() && isStrictlyBetterCost(selected_candidate.cost, congestion_candidate->cost)) {
      selected_candidate = std::move(*congestion_candidate);
      used_congestion_flute = true;
    }
  }

  stat = selected_candidate.refine_stat;
  stat.attempted_congestion_flute = attempted_congestion_flute;
  stat.used_congestion_flute = used_congestion_flute;
  return std::move(selected_candidate.topo_list);
}

void TOPOBuilder::destroy()
{
  Monitor monitor;
  RTLOG.info(Loc::current(), "Starting...");
  Flute::deleteLUT();
  RTLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

TOPOBuilder* TOPOBuilder::_tb_instance = nullptr;

}  // namespace irt
