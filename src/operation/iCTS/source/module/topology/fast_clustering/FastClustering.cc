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
 * @file FastClustering.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief Fast spatial clustering facade for topology clustering.
 */

#include "FastClustering.hh"

#include <algorithm>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "Clustering.hh"
#include "LogTable.hh"
#include "Logger.hh"
#include "TopologyConfig.hh"
#include "cluster_draft/FastClusteringDraft.hh"

namespace icts {

namespace detail = fast_clustering;

namespace {

using detail::FormatRatio;

auto CalcAverageFanout(std::size_t load_count, std::size_t cluster_count) -> double
{
  if (cluster_count == 0U) {
    return 0.0;
  }
  return static_cast<double>(load_count) / static_cast<double>(cluster_count);
}

}  // namespace

auto FastClustering::buildElectricalBaseConfig(std::size_t max_fanout, double max_cap) -> ClusterConfig
{
  ClusterConfig config;
  config.max_fanout = max_fanout;
  config.max_diameter = std::numeric_limits<int>::max();
  config.max_cap = max_cap;
  return config;
}

auto FastClustering::runDefault(const std::vector<Pin*>& loads, const ClusterConfig& base_config) -> ClusterOutput
{
  return run(loads, base_config);
}

auto FastClustering::run(const std::vector<Pin*>& loads, const ClusterConfig& config) -> ClusterOutput
{
  ClusterOutput result;
  if (loads.empty()) {
    return result;
  }

  auto entries = detail::CollectEntries(loads);
  if (entries.empty()) {
    CTSLOG.warn(Loc::current(), "Fast clustering skipped: no valid load pins.");
    return result;
  }

  auto drafts = detail::BuildSpatialRecursiveClusters(entries, config);

  detail::PolishSmallClusters(drafts, entries, config);

  auto finalized = detail::FinalizeClusters(drafts, entries, config);
  if (!finalized.has_value() || detail::CountAssignedLoads(*finalized) != entries.size()) {
    CTSLOG.warn(Loc::current(), "Fast clustering failed to produce a legal complete partition.");
    return result;
  }

  const auto assigned_load_count = detail::CountAssignedLoads(*finalized);
  std::size_t min_fanout = finalized->clusters.empty() ? 0U : finalized->clusters.front().size();
  std::size_t max_fanout = 0U;
  for (const auto& cluster : finalized->clusters) {
    min_fanout = std::min(min_fanout, cluster.size());
    max_fanout = std::max(max_fanout, cluster.size());
  }

  std::size_t exact_cluster_count = 0U;
  std::size_t route_failure_count = 0U;
  int max_diameter_dbu = 0;
  double min_total_cap_pf = finalized->electrical_summaries.empty() ? 0.0 : finalized->electrical_summaries.front().total_cap_pf;
  double max_total_cap_pf = 0.0;
  double total_wirelength_dbu = 0.0;
  for (const auto& summary : finalized->electrical_summaries) {
    exact_cluster_count += summary.exact ? 1U : 0U;
    route_failure_count += summary.exact && !summary.route_success ? 1U : 0U;
    max_diameter_dbu = std::max(max_diameter_dbu, summary.diameter_dbu);
    min_total_cap_pf = std::min(min_total_cap_pf, summary.total_cap_pf);
    max_total_cap_pf = std::max(max_total_cap_pf, summary.total_cap_pf);
    total_wirelength_dbu += summary.wirelength_dbu;
  }
  EmitLogTable(Loc::current(), "CTS Clustering Summary", {"Metric", "Value"},
               {{"Strategy", "recursive_spatial_bisect"},
                {"Loads", ToLogTableCell(entries.size())},
                {"Clusters", ToLogTableCell(finalized->clusters.size())},
                {"Fanout Minimum", ToLogTableCell(min_fanout)},
                {"Fanout Average", FormatRatio(CalcAverageFanout(assigned_load_count, finalized->clusters.size()))},
                {"Fanout Maximum", ToLogTableCell(max_fanout)},
                {"Constraint Maximum Fanout", ToLogTableCell(config.max_fanout)},
                {"Constraint Maximum Diameter (DBU)", ToLogTableCell(config.max_diameter)},
                {"Constraint Maximum Capacitance (pF)", ToLogTableCell(config.max_cap)},
                {"Exact Clusters", ToLogTableCell(exact_cluster_count)},
                {"Exact Route Failures", ToLogTableCell(route_failure_count)},
                {"Maximum Diameter (DBU)", ToLogTableCell(max_diameter_dbu)},
                {"Total Capacitance Minimum (pF)", ToLogTableCell(min_total_cap_pf)},
                {"Total Capacitance Maximum (pF)", ToLogTableCell(max_total_cap_pf)},
                {"Routed Wirelength (DBU)", ToLogTableCell(total_wirelength_dbu)}});
  return *finalized;
}

}  // namespace icts
