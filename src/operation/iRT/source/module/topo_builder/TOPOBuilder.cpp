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

#include "Logger.hpp"
#include "Monitor.hpp"
#include "RTHeader.hpp"
#include "flute3/flute.h"

namespace irt {

namespace {

using PlanarTopo = std::vector<Segment<PlanarCoord>>;
using NeighborList = std::vector<std::vector<int32_t>>;

constexpr double kCostEpsilon = 1e-9;
constexpr int32_t kMaxSteinerShiftNum = 32;
constexpr int32_t kSteinerShiftExactSpan = 16;
constexpr int32_t kSteinerShiftCoarseSampleNum = 8;
constexpr int32_t kSteinerShiftKeepBinNum = 2;
constexpr int32_t kSteinerShiftFineSampleNum = 8;
constexpr int32_t kSteinerShiftLocalRadius = 4;

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

double getBendCost(const TBTask& task, const PlanarCoord& first, const PlanarCoord& bend, const PlanarCoord& second)
{
  double cost = getSegmentCost(task, first, bend);
  if (!std::isfinite(cost)) {
    return std::numeric_limits<double>::infinity();
  }
  double second_cost = getSegmentCost(task, bend, second);
  return std::isfinite(second_cost) ? cost + second_cost : std::numeric_limits<double>::infinity();
}

double getPatternCost(const TBTask& task, const PlanarCoord& first, const PlanarCoord& second)
{
  if (first == second) {
    return 0;
  }
  if (first.get_x() == second.get_x() || first.get_y() == second.get_y()) {
    return getSegmentCost(task, first, second);
  }
  PlanarCoord x_bend(second.get_x(), first.get_y());
  PlanarCoord y_bend(first.get_x(), second.get_y());
  return std::min(getBendCost(task, first, x_bend, second), getBendCost(task, first, y_bend, second));
}

bool isInsideSearchRegion(const TBTask& task, const PlanarCoord& coord)
{
  if (!task.has_planar_search_region()) {
    return true;
  }
  const PlanarRect& region = task.get_planar_search_region();
  return region.get_ll_x() <= coord.get_x() && coord.get_x() <= region.get_ur_x() && region.get_ll_y() <= coord.get_y() && coord.get_y() <= region.get_ur_y();
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
  if (!std::isfinite(cost)) {
    return cost;
  }
  for (int32_t neighbor_idx : neighbor_list[first_idx]) {
    if (neighbor_idx != second_idx) {
      double pattern_cost = getPatternCost(task, first_coord, getBranchCoord(tree, neighbor_idx));
      if (!std::isfinite(pattern_cost)) {
        return pattern_cost;
      }
      cost += pattern_cost;
    }
  }
  for (int32_t neighbor_idx : neighbor_list[second_idx]) {
    if (neighbor_idx != first_idx) {
      double pattern_cost = getPatternCost(task, second_coord, getBranchCoord(tree, neighbor_idx));
      if (!std::isfinite(pattern_cost)) {
        return pattern_cost;
      }
      cost += pattern_cost;
    }
  }
  return cost;
}

bool isStrictlyBetterCost(double current_cost, double candidate_cost)
{
  return std::isfinite(candidate_cost) && (!std::isfinite(current_cost) || candidate_cost + kCostEpsilon < current_cost);
}

std::pair<int32_t, int32_t> getBranchShiftRange(const Flute::Tree& tree, const NeighborList& neighbor_list, int32_t branch_idx, bool is_horizontal)
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

bool isShiftEdgeEligible(const TBTask& task, const Flute::Tree& tree, const NeighborList& neighbor_list, int32_t first_idx, int32_t second_idx,
                         const PlanarCoord& first_coord, const PlanarCoord& second_coord)
{
  if (!task.has_shift_edge_filter()) {
    return true;
  }
  if (task.should_shift_edge(first_coord, second_coord)) {
    return true;
  }
  for (int32_t neighbor_idx : neighbor_list[first_idx]) {
    if (neighbor_idx != second_idx && task.should_shift_edge(first_coord, getBranchCoord(tree, neighbor_idx))) {
      return true;
    }
  }
  for (int32_t neighbor_idx : neighbor_list[second_idx]) {
    if (neighbor_idx != first_idx && task.should_shift_edge(second_coord, getBranchCoord(tree, neighbor_idx))) {
      return true;
    }
  }
  return false;
}

bool shiftBestSteinerEdge(const TBTask& task, Flute::Tree& tree, const NeighborList& neighbor_list)
{
  struct ShiftResult
  {
    bool valid = false;
    int32_t value = 0;
    double cost = std::numeric_limits<double>::infinity();
    PlanarCoord first_coord;
    PlanarCoord second_coord;
  };

  auto make_samples = [](int32_t lower, int32_t upper, int32_t sample_num) {
    std::vector<int32_t> samples;
    int64_t span = static_cast<int64_t>(upper) - lower + 1;
    if (span < sample_num) {
      sample_num = static_cast<int32_t>(span);
    }
    samples.reserve(sample_num);
    for (int32_t i = 0; i < sample_num; i++) {
      int64_t offset = sample_num == 1 ? 0 : (span - 1) * i / (sample_num - 1);
      samples.push_back(static_cast<int32_t>(static_cast<int64_t>(lower) + offset));
    }
    return samples;
  };

  auto is_better_result = [](const ShiftResult& first, const ShiftResult& second) {
    if (first.valid != second.valid) {
      return first.valid;
    }
    if (!first.valid) {
      return false;
    }
    if (std::abs(first.cost - second.cost) > kCostEpsilon) {
      return first.cost < second.cost;
    }
    return first.value < second.value;
  };

  TBSteinerShift best_shift;

  std::set<std::pair<int32_t, int32_t>> visited_edge_set;
  for (int32_t first_idx = 0; first_idx < getBranchNum(tree); first_idx++) {
    int32_t second_idx = tree.branch[first_idx].n;
    if (second_idx < 0 || getBranchNum(tree) <= second_idx || second_idx == first_idx) {
      continue;
    }
    int32_t edge_first_idx = std::min(first_idx, second_idx);
    int32_t edge_second_idx = std::max(first_idx, second_idx);
    if (!visited_edge_set.emplace(edge_first_idx, edge_second_idx).second
        || (first_idx < tree.deg && second_idx < tree.deg)) {
      continue;
    }
    bool first_is_steiner = first_idx >= tree.deg;
    bool second_is_steiner = second_idx >= tree.deg;
    int32_t movable_idx = first_is_steiner ? first_idx : second_idx;
    PlanarCoord first_coord = getBranchCoord(tree, first_idx);
    PlanarCoord second_coord = getBranchCoord(tree, second_idx);
    bool is_horizontal = first_coord.get_y() == second_coord.get_y() && first_coord.get_x() != second_coord.get_x();
    bool is_vertical = first_coord.get_x() == second_coord.get_x() && first_coord.get_y() != second_coord.get_y();
    if (!is_horizontal && !is_vertical) {
      continue;
    }

    auto [lower, upper] = getBranchShiftRange(tree, neighbor_list, movable_idx, is_horizontal);
    if (first_is_steiner && second_is_steiner) {
      auto [second_lower, second_upper] = getBranchShiftRange(tree, neighbor_list, second_idx, is_horizontal);
      lower = std::max(lower, second_lower);
      upper = std::min(upper, second_upper);
    }
    if (task.has_planar_search_region()) {
      const PlanarRect& region = task.get_planar_search_region();
      if (is_horizontal) {
        lower = std::max(lower, region.get_ll_y());
        upper = std::min(upper, region.get_ur_y());
      } else {
        lower = std::max(lower, region.get_ll_x());
        upper = std::min(upper, region.get_ur_x());
      }
    }
    if (lower > upper || !isShiftEdgeEligible(task, tree, neighbor_list, first_idx, second_idx, first_coord, second_coord)) {
      continue;
    }
    PlanarCoord current_movable_coord = first_is_steiner ? first_coord : second_coord;
    if (!isInsideSearchRegion(task, current_movable_coord)) {
      continue;
    }

    double current_cost = getIncidentEdgeCost(task, tree, neighbor_list, first_idx, second_idx, first_coord, second_coord);

    auto get_result = [&](int32_t value) {
      ShiftResult result;
      PlanarCoord candidate_first = first_coord;
      PlanarCoord candidate_second = second_coord;
      if (first_is_steiner) {
        setShiftCoord(candidate_first, is_horizontal, value);
      }
      if (second_is_steiner) {
        setShiftCoord(candidate_second, is_horizontal, value);
      }
      PlanarCoord movable_coord = first_is_steiner ? candidate_first : candidate_second;
      if (movable_coord == current_movable_coord) {
        result.valid = true;
        result.value = value;
        result.cost = current_cost;
        result.first_coord = first_coord;
        result.second_coord = second_coord;
        return result;
      }
      result.valid = true;
      result.value = value;
      result.cost = getIncidentEdgeCost(task, tree, neighbor_list, first_idx, second_idx, candidate_first, candidate_second);
      result.first_coord = candidate_first;
      result.second_coord = candidate_second;
      return result;
    };

    auto consider_result = [&](const ShiftResult& result) {
      if (!result.valid || !isStrictlyBetterCost(current_cost, result.cost)) {
        return;
      }
      double gain = std::isfinite(current_cost) ? current_cost - result.cost : std::numeric_limits<double>::infinity();
      if (!best_shift.isValid() || gain > best_shift.gain + kCostEpsilon) {
        best_shift = {first_idx, second_idx, result.first_coord, result.second_coord, gain};
      }
    };

    int64_t span = static_cast<int64_t>(upper) - lower + 1;
    if (span <= kSteinerShiftExactSpan) {
      for (int32_t value = lower; value <= upper; value++) {
        consider_result(get_result(value));
      }
      continue;
    }

    struct ShiftBin
    {
      int32_t lower;
      int32_t upper;
      ShiftResult coarse;
    };
    std::vector<int32_t> coarse_samples = make_samples(lower, upper, kSteinerShiftCoarseSampleNum);
    std::vector<ShiftBin> bins;
    bins.reserve(coarse_samples.size());
    for (size_t i = 0; i < coarse_samples.size(); i++) {
      int32_t bin_lower = i == 0 ? lower : static_cast<int32_t>(static_cast<int64_t>(coarse_samples[i - 1])
                                                                + (static_cast<int64_t>(coarse_samples[i]) - coarse_samples[i - 1]) / 2 + 1);
      int32_t bin_upper = i + 1 == coarse_samples.size() ? upper : static_cast<int32_t>(static_cast<int64_t>(coarse_samples[i])
                                                                       + (static_cast<int64_t>(coarse_samples[i + 1]) - coarse_samples[i]) / 2);
      ShiftResult coarse = get_result(coarse_samples[i]);
      consider_result(coarse);
      bins.push_back({bin_lower, bin_upper, coarse});
    }
    std::ranges::sort(bins, [&](const ShiftBin& first, const ShiftBin& second) {
      if (is_better_result(first.coarse, second.coarse)) {
        return true;
      }
      if (is_better_result(second.coarse, first.coarse)) {
        return false;
      }
      return first.lower < second.lower;
    });

    int32_t bin_num = std::min<int32_t>(kSteinerShiftKeepBinNum, static_cast<int32_t>(bins.size()));
    for (int32_t bin_idx = 0; bin_idx < bin_num; bin_idx++) {
      ShiftBin& bin = bins[bin_idx];
      ShiftResult bin_best = bin.coarse;
      for (int32_t value : make_samples(bin.lower, bin.upper, kSteinerShiftFineSampleNum)) {
        ShiftResult result = get_result(value);
        consider_result(result);
        if (is_better_result(result, bin_best)) {
          bin_best = result;
        }
      }
      if (!bin_best.valid) {
        continue;
      }
      int32_t local_lower = static_cast<int32_t>(std::max<int64_t>(bin.lower, static_cast<int64_t>(bin_best.value) - kSteinerShiftLocalRadius));
      int32_t local_upper = static_cast<int32_t>(std::min<int64_t>(bin.upper, static_cast<int64_t>(bin_best.value) + kSteinerShiftLocalRadius));
      for (int32_t value = local_lower; value <= local_upper; value++) {
        consider_result(get_result(value));
      }
    }
  }
  if (best_shift.isValid()) {
    setBranchCoord(tree, best_shift.first_idx, best_shift.first_coord);
    setBranchCoord(tree, best_shift.second_idx, best_shift.second_coord);
  }
  return best_shift.isValid();
}

void shiftSteinerEdgesByCost(const TBTask& task, Flute::Tree& tree, TBRefineStat& stat)
{
  if (!task.has_segment_cost_query() || !task.is_cost_refine_enabled()) {
    return;
  }
  int32_t max_shift_num = kMaxSteinerShiftNum;
  NeighborList neighbor_list = getNeighborList(tree);
  while (stat.shifted_edge_num < max_shift_num && shiftBestSteinerEdge(task, tree, neighbor_list)) {
    stat.shifted_edge_num++;
  }
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
    double pattern_cost = getPatternCost(task, segment.get_first(), segment.get_second());
    if (!std::isfinite(pattern_cost)) {
      return pattern_cost;
    }
    cost += pattern_cost;
  }
  return cost;
}

TBTopoCandidate finalizeCandidate(const TBTask& task, Flute::Tree& tree)
{
  TBTopoCandidate candidate;
  shiftSteinerEdgesByCost(task, tree, candidate.refine_stat);
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

TBTopoCandidate buildTerminalMSTCandidate(const TBTask& task)
{
  struct Link
  {
    int32_t parent = -1;
    double cost = std::numeric_limits<double>::infinity();
    int64_t wire_length = std::numeric_limits<int64_t>::max();
  };
  auto is_better = [](const Link& first, const Link& second) {
    bool first_finite = std::isfinite(first.cost);
    bool second_finite = std::isfinite(second.cost);
    if (first_finite != second_finite) {
      return first_finite;
    }
    if (first_finite && std::abs(first.cost - second.cost) > kCostEpsilon) {
      return first.cost < second.cost;
    }
    if (first.wire_length != second.wire_length) {
      return first.wire_length < second.wire_length;
    }
    return first.parent < second.parent;
  };

  const std::vector<PlanarCoord>& terminal_list = task.get_planar_coord_list();
  std::vector<bool> visited(terminal_list.size(), false);
  std::vector<Link> link_list(terminal_list.size());
  TBTopoCandidate candidate;
  visited.front() = true;
  auto update_link = [&](int32_t parent_idx, int32_t child_idx) {
    const PlanarCoord& parent = terminal_list[parent_idx];
    const PlanarCoord& child = terminal_list[child_idx];
    Link link{parent_idx, getPatternCost(task, parent, child),
              std::abs(static_cast<int64_t>(parent.get_x()) - child.get_x()) + std::abs(static_cast<int64_t>(parent.get_y()) - child.get_y())};
    if (link_list[child_idx].parent < 0 || is_better(link, link_list[child_idx])) {
      link_list[child_idx] = link;
    }
  };
  for (int32_t child_idx = 1; child_idx < static_cast<int32_t>(terminal_list.size()); child_idx++) {
    update_link(0, child_idx);
  }
  while (candidate.topo_list.size() + 1 < terminal_list.size()) {
    int32_t best_idx = -1;
    for (int32_t idx = 0; idx < static_cast<int32_t>(terminal_list.size()); idx++) {
      if (!visited[idx] && (best_idx < 0 || is_better(link_list[idx], link_list[best_idx]))) {
        best_idx = idx;
      }
    }
    if (best_idx < 0) {
      break;
    }
    visited[best_idx] = true;
    candidate.topo_list.emplace_back(terminal_list[link_list[best_idx].parent], terminal_list[best_idx]);
    for (int32_t idx = 0; idx < static_cast<int32_t>(terminal_list.size()); idx++) {
      if (!visited[idx]) {
        update_link(best_idx, idx);
      }
    }
  }
  candidate.cost = getTopoCost(task, candidate.topo_list);
  return candidate;
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
  if (coord_list.size() == 2) {
    if (coord_list.front() == coord_list.back()) {
      return {};
    }
    return {Segment<PlanarCoord>(coord_list.front(), coord_list.back())};
  }

  TBTopoCandidate selected_candidate = buildBaselineCandidate(task);
  bool used_terminal_mst = task.is_congestion_driven() && task.has_segment_cost_query() && !std::isfinite(selected_candidate.cost);
  if (used_terminal_mst) {
    selected_candidate = buildTerminalMSTCandidate(task);
  }

  stat = selected_candidate.refine_stat;
  stat.used_terminal_mst = used_terminal_mst;
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
