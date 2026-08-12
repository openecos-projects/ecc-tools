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
 * @file TopologyDistance.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-28
 * @brief Calculates clustered-sink topology distances.
 */

#include "synthesis/trace/distance/TopologyDistance.hh"

#include <algorithm>
#include <cstdlib>
#include <optional>
#include <vector>

#include "Logger.hh"
#include "Net.hh"
#include "Pin.hh"
#include "Point.hh"
#include "synthesis/topology/buffer/BufferInsertion.hh"

namespace icts::topology {
namespace {

auto calcManhattanDistance(const Point<int>& lhs, const Point<int>& rhs) -> int
{
  const int delta_x = std::abs(lhs.get_x() - rhs.get_x());
  const int delta_y = std::abs(lhs.get_y() - rhs.get_y());
  return delta_x + delta_y;
}

auto resolveDbuPerUm(std::int32_t dbu_per_um) -> std::optional<double>
{
  if (dbu_per_um <= 0) {
    CTSLOG.warn(Loc::current(), "Topology: cannot calculate cluster distance without DBU-per-micron.");
    return std::nullopt;
  }
  return static_cast<double>(dbu_per_um);
}

auto dbuToUm(double value_dbu, double dbu_per_um) -> double
{
  return value_dbu / dbu_per_um;
}

}  // namespace

auto CalculateClusterLeafDistance(const ClusterLeafDistanceInput& input) -> std::optional<Topology::ClusterLeafDistanceSummary>
{
  if (input.build == nullptr) {
    CTSLOG.error(Loc::current(), "Topology distance calculation requires topology build.");
  }
  const auto& build = *input.build;
  if (build.output.cluster_buffers.empty()) {
    return std::nullopt;
  }

  const auto dbu_per_um = resolveDbuPerUm(input.dbu_per_um);
  if (!dbu_per_um.has_value()) {
    return std::nullopt;
  }

  std::vector<double> distances;
  distances.reserve(build.output.cluster_buffers.size());

  double total_distance = 0.0;
  for (const auto& cluster_buffer : build.output.cluster_buffers) {
    if (!HasValidLocation(cluster_buffer.location) || cluster_buffer.input_pin == nullptr) {
      continue;
    }

    const auto* leaf_net = cluster_buffer.input_pin->get_net();
    if (leaf_net == nullptr || leaf_net->get_driver() == nullptr) {
      continue;
    }
    const auto leaf_location = FindRenderableLocation(leaf_net->get_driver());
    if (!HasValidLocation(leaf_location)) {
      continue;
    }

    const int distance_dbu = calcManhattanDistance(cluster_buffer.location, leaf_location);
    const double distance_um = dbuToUm(distance_dbu, *dbu_per_um);
    distances.push_back(distance_um);
    total_distance += distance_um;
  }

  if (distances.empty()) {
    return std::nullopt;
  }

  std::ranges::sort(distances);
  const std::size_t median_index = distances.size() / 2U;
  const double median_distance = (distances.size() % 2U) == 0U
                                     ? (static_cast<double>(distances.at(median_index - 1U)) + static_cast<double>(distances.at(median_index))) / 2.0
                                     : static_cast<double>(distances.at(median_index));
  Topology::ClusterLeafDistanceSummary summary{
      .count = distances.size(),
      .min_distance_um = distances.front(),
      .max_distance_um = distances.back(),
      .mean_distance_um = total_distance / static_cast<double>(distances.size()),
      .median_distance_um = median_distance,
  };
  return summary;
}

}  // namespace icts::topology
