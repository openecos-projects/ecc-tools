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
 * @file FastClusteringPartition.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief Initial spatial partitioning for fast topology clustering.
 */

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numeric>
#include <utility>
#include <vector>

#include "Pin.hh"
#include "Point.hh"
#include "cluster_draft/FastClusteringDraft.hh"

namespace icts {
struct ClusterConfig;
}  // namespace icts

namespace icts::fast_clustering {

auto CollectEntries(const std::vector<Pin*>& loads) -> std::vector<LoadEntry>
{
  std::vector<LoadEntry> entries;
  entries.reserve(loads.size());
  for (std::size_t index = 0; index < loads.size(); ++index) {
    auto* pin = loads.at(index);
    if (pin == nullptr) {
      continue;
    }
    entries.push_back(LoadEntry{.pin = pin, .location = pin->get_location(), .original_index = index});
  }
  return entries;
}

namespace {

struct RecursiveSplitPlan
{
  std::vector<std::size_t> ordered_entry_ids;
  std::size_t split_size = 0U;
  std::size_t split_distance = std::numeric_limits<std::size_t>::max();
  std::size_t child_count = 0U;
  double score = std::numeric_limits<double>::infinity();
  double routing_cap_balance_penalty = std::numeric_limits<double>::infinity();
  double routing_cap_spread = std::numeric_limits<double>::infinity();
  double utilization_penalty = std::numeric_limits<double>::infinity();
  int max_child_diameter = std::numeric_limits<int>::max();
  int total_child_diameter = std::numeric_limits<int>::max();
};

auto ResolveLongestAxis(const Bounds& bounds) -> bool
{
  return (bounds.max_x - bounds.min_x) >= (bounds.max_y - bounds.min_y);
}

auto SortEntryIdsByAxis(std::vector<std::size_t>& entry_ids, const std::vector<LoadEntry>& entries, bool split_by_x) -> void
{
  std::ranges::sort(entry_ids, [&entries, split_by_x](std::size_t lhs, std::size_t rhs) -> bool {
    const auto lhs_location = entries.at(lhs).location;
    const auto rhs_location = entries.at(rhs).location;
    const auto lhs_primary = split_by_x ? lhs_location.get_x() : lhs_location.get_y();
    const auto rhs_primary = split_by_x ? rhs_location.get_x() : rhs_location.get_y();
    if (lhs_primary != rhs_primary) {
      return lhs_primary < rhs_primary;
    }
    const auto lhs_secondary = split_by_x ? lhs_location.get_y() : lhs_location.get_x();
    const auto rhs_secondary = split_by_x ? rhs_location.get_y() : rhs_location.get_x();
    if (lhs_secondary != rhs_secondary) {
      return lhs_secondary < rhs_secondary;
    }
    return entries.at(lhs).original_index < entries.at(rhs).original_index;
  });
}

auto ResolveTargetClusterCount(std::size_t entry_count, std::size_t fanout_limit) -> std::size_t
{
  const auto safe_fanout = std::max<std::size_t>(1U, fanout_limit);
  return (entry_count + safe_fanout - 1U) / safe_fanout;
}

auto ResolveRecursiveChildClusterCount(std::size_t entry_count, std::size_t fanout_limit, const Bounds& bounds, const ClusterConfig& config) -> std::size_t
{
  auto target_cluster_count = ResolveTargetClusterCount(entry_count, fanout_limit);
  if (target_cluster_count <= 1U && !IsDiameterLegal(bounds, config)) {
    target_cluster_count = 2U;
  }
  return target_cluster_count;
}

auto CalcSizeDistance(std::size_t lhs, std::size_t rhs) -> std::size_t
{
  return lhs > rhs ? lhs - rhs : rhs - lhs;
}

auto ResolveSplitCandidateWindow(std::size_t fanout_limit) -> std::size_t
{
  const auto utilization_scaled_window = std::max<std::size_t>(1U, fanout_limit / 4U);
  return std::min(kSplitCandidateWindow, utilization_scaled_window);
}

auto CalcSplitUtilizationPenalty(std::size_t entry_count, std::size_t split_size, std::size_t ideal_split_size, std::size_t target_cluster_count,
                                 std::size_t lhs_child_count, std::size_t rhs_child_count, std::size_t fanout_limit) -> double
{
  const auto safe_fanout = static_cast<double>(std::max<std::size_t>(1U, fanout_limit));
  const auto split_deviation = static_cast<double>(CalcSizeDistance(split_size, ideal_split_size)) / safe_fanout;
  const auto target_leaf_size = static_cast<double>(entry_count) / static_cast<double>(std::max<std::size_t>(1U, target_cluster_count));
  const auto lhs_leaf_size = static_cast<double>(split_size) / static_cast<double>(std::max<std::size_t>(1U, lhs_child_count));
  const auto rhs_size = entry_count - split_size;
  const auto rhs_leaf_size = static_cast<double>(rhs_size) / static_cast<double>(std::max<std::size_t>(1U, rhs_child_count));
  const auto child_balance = (std::abs(lhs_leaf_size - target_leaf_size) + std::abs(rhs_leaf_size - target_leaf_size)) / safe_fanout;
  return split_deviation + child_balance;
}

auto BuildRecursiveSplitPlan(std::vector<std::size_t> entry_ids, const std::vector<LoadEntry>& entries, std::size_t fanout_limit, const Bounds& bounds,
                             const ClusterConfig& config, bool split_by_x) -> RecursiveSplitPlan
{
  SortEntryIdsByAxis(entry_ids, entries, split_by_x);
  const auto entry_count = entry_ids.size();
  auto target_cluster_count = ResolveRecursiveChildClusterCount(entry_count, fanout_limit, bounds, config);

  const auto left_cluster_count = std::max<std::size_t>(1U, target_cluster_count / 2U);
  const auto ideal_split_size
      = std::clamp<std::size_t>((entry_count * left_cluster_count + target_cluster_count - 1U) / target_cluster_count, 1U, entry_count - 1U);
  const auto split_candidate_window = ResolveSplitCandidateWindow(fanout_limit);
  const auto split_begin = ideal_split_size > split_candidate_window ? ideal_split_size - split_candidate_window : 1U;
  const auto split_end = std::min(entry_count - 1U, ideal_split_size + split_candidate_window);

  RecursiveSplitPlan best_plan{
      .ordered_entry_ids = entry_ids,
      .split_size = ideal_split_size,
  };
  for (auto split_size = split_begin; split_size <= split_end; ++split_size) {
    std::vector<std::size_t> lhs_ids(entry_ids.begin(), entry_ids.begin() + static_cast<std::ptrdiff_t>(split_size));
    std::vector<std::size_t> rhs_ids(entry_ids.begin() + static_cast<std::ptrdiff_t>(split_size), entry_ids.end());
    const auto lhs = BuildDraft(std::move(lhs_ids), entries, config);
    const auto rhs = BuildDraft(std::move(rhs_ids), entries, config);
    const auto lhs_child_count = ResolveRecursiveChildClusterCount(lhs.entry_ids.size(), fanout_limit, lhs.bounds, config);
    const auto rhs_child_count = ResolveRecursiveChildClusterCount(rhs.entry_ids.size(), fanout_limit, rhs.bounds, config);
    const auto child_count = std::max<std::size_t>(1U, lhs_child_count + rhs_child_count);
    const auto target_routing_cap_proxy = (lhs.routing_cap_proxy + rhs.routing_cap_proxy) / static_cast<double>(child_count);
    const auto lhs_avg_proxy = lhs.routing_cap_proxy / static_cast<double>(std::max<std::size_t>(1U, lhs_child_count));
    const auto rhs_avg_proxy = rhs.routing_cap_proxy / static_cast<double>(std::max<std::size_t>(1U, rhs_child_count));
    const auto geometry_score = ClusterScoreProxy(lhs, config) + ClusterScoreProxy(rhs, config);
    const auto utilization_penalty
        = CalcSplitUtilizationPenalty(entry_count, split_size, ideal_split_size, target_cluster_count, lhs_child_count, rhs_child_count, fanout_limit);
    const auto routing_cap_balance_penalty = static_cast<double>(lhs_child_count) * CalcRoutingCapVariancePenalty(lhs_avg_proxy, target_routing_cap_proxy)
                                             + static_cast<double>(rhs_child_count) * CalcRoutingCapVariancePenalty(rhs_avg_proxy, target_routing_cap_proxy);
    const auto score = geometry_score + kSplitRoutingCapBalanceWeight * routing_cap_balance_penalty
                       + kSplitUtilizationBalanceWeight * std::max(1.0, geometry_score) * utilization_penalty;
    const auto split_distance = CalcSizeDistance(split_size, ideal_split_size);
    if (score + kScoreEpsilon < best_plan.score || (std::abs(score - best_plan.score) <= kScoreEpsilon && split_distance < best_plan.split_distance)) {
      const auto lhs_diameter = CalcDiameter(lhs.bounds);
      const auto rhs_diameter = CalcDiameter(rhs.bounds);
      best_plan.split_size = split_size;
      best_plan.split_distance = split_distance;
      best_plan.child_count = child_count;
      best_plan.score = score;
      best_plan.routing_cap_balance_penalty = routing_cap_balance_penalty;
      best_plan.routing_cap_spread = std::abs(lhs_avg_proxy - rhs_avg_proxy);
      best_plan.utilization_penalty = utilization_penalty;
      best_plan.max_child_diameter = std::max(lhs_diameter, rhs_diameter);
      best_plan.total_child_diameter = lhs_diameter + rhs_diameter;
    }
  }
  return best_plan;
}

auto IsParetoBetterSplitAxis(const RecursiveSplitPlan& candidate, const RecursiveSplitPlan& baseline) -> bool
{
  return candidate.score + kScoreEpsilon < baseline.score && candidate.child_count == baseline.child_count
         && candidate.split_distance <= baseline.split_distance && candidate.routing_cap_balance_penalty <= baseline.routing_cap_balance_penalty + kScoreEpsilon
         && candidate.routing_cap_spread <= baseline.routing_cap_spread + kScoreEpsilon
         && candidate.utilization_penalty <= baseline.utilization_penalty + kScoreEpsilon && candidate.max_child_diameter <= baseline.max_child_diameter
         && candidate.total_child_diameter <= baseline.total_child_diameter;
}

auto ResolveRecursiveSplitPlan(const std::vector<std::size_t>& entry_ids, const std::vector<LoadEntry>& entries, std::size_t fanout_limit, const Bounds& bounds,
                               const ClusterConfig& config) -> RecursiveSplitPlan
{
  const auto split_by_longest_axis = ResolveLongestAxis(bounds);
  auto longest_axis_plan = BuildRecursiveSplitPlan(entry_ids, entries, fanout_limit, bounds, config, split_by_longest_axis);
  auto alternate_axis_plan = BuildRecursiveSplitPlan(entry_ids, entries, fanout_limit, bounds, config, !split_by_longest_axis);
  if (IsParetoBetterSplitAxis(alternate_axis_plan, longest_axis_plan)) {
    return alternate_axis_plan;
  }
  return longest_axis_plan;
}

auto BuildSpatialRecursiveClusters(std::vector<std::size_t> entry_ids, const std::vector<LoadEntry>& entries, const ClusterConfig& config,
                                   std::size_t fanout_limit, std::vector<ClusterDraft>& clusters) -> void
{
  std::vector<std::vector<std::size_t>> pending;
  pending.push_back(std::move(entry_ids));
  while (!pending.empty()) {
    auto current_entry_ids = std::move(pending.back());
    pending.pop_back();
    if (current_entry_ids.empty()) {
      continue;
    }

    const auto bounds = CalcClusterBounds(current_entry_ids, entries);
    if ((current_entry_ids.size() <= fanout_limit && IsDiameterLegal(bounds, config)) || current_entry_ids.size() == 1U) {
      clusters.push_back(BuildDraft(std::move(current_entry_ids), entries, config));
      continue;
    }

    auto split_plan = ResolveRecursiveSplitPlan(current_entry_ids, entries, fanout_limit, bounds, config);
    current_entry_ids = std::move(split_plan.ordered_entry_ids);
    const auto split_size = split_plan.split_size;
    std::vector<std::size_t> lhs(current_entry_ids.begin(), current_entry_ids.begin() + static_cast<std::ptrdiff_t>(split_size));
    std::vector<std::size_t> rhs(current_entry_ids.begin() + static_cast<std::ptrdiff_t>(split_size), current_entry_ids.end());
    pending.push_back(std::move(rhs));
    pending.push_back(std::move(lhs));
  }
}

}  // namespace

auto BuildSpatialRecursiveClusters(const std::vector<LoadEntry>& entries, const ClusterConfig& config) -> std::vector<ClusterDraft>
{
  const auto fanout_limit = ResolvePackingFanoutLimit(config, entries.size());
  std::vector<std::size_t> entry_ids(entries.size());
  std::iota(entry_ids.begin(), entry_ids.end(), 0U);

  std::vector<ClusterDraft> clusters;
  clusters.reserve(ResolveTargetClusterCount(entries.size(), fanout_limit));
  BuildSpatialRecursiveClusters(std::move(entry_ids), entries, config, fanout_limit, clusters);
  return clusters;
}

}  // namespace icts::fast_clustering
