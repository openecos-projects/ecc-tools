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
 * @file SinkBranch.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-28
 * @brief Implements downstream sink-tree Topology coordination.
 */

#include "synthesis/topology/sink/SinkBranch.hh"

#include <algorithm>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ClockRouteSegmentRC.hh"
#include "Logger.hh"
#include "Net.hh"
#include "Point.hh"
#include "config/Config.hh"
#include "io/Wrapper.hh"
#include "synthesis/htree/HTree.hh"
#include "synthesis/htree/characterization/library/CharacterizationLibrary.hh"
#include "synthesis/topology/buffer/BufferInsertion.hh"
#include "synthesis/topology/sink/SinkLoadClustering.hh"
#include "synthesis/trace/distance/TopologyDistance.hh"
#include "synthesis/trace/topology_build/TopologyBuildTrace.hh"

namespace icts::topology {

namespace {

auto ResolveRoutingLayer(const Config& config) -> int
{
  const auto& routing_layers = config.get_routing_layers();
  if (routing_layers.empty() || routing_layers.front() == 0U) {
    CTSLOG.error(Loc::current(), "Topology: routing layer must be configured for HTree.");
  }
  return static_cast<int>(routing_layers.front());
}

auto ResolveWireWidth(const Config& config) -> std::optional<double>
{
  const double wire_width_um = config.get_wire_width();
  return wire_width_um > 0.0 ? std::optional<double>(wire_width_um) : std::nullopt;
}

auto ApplyMinTopInputSlew(const Config& config, HTree::Config& htree_config) -> void
{
  htree_config.min_top_input_slew_ns = config.get_root_input_slew();
}

auto BuildSinkTreeLoadPreparationPolicy(const Config& config, Wrapper& wrapper, bool enable_sink_clustering) -> SinkTreeLoadPreparationPolicy
{
  SinkTreeLoadPreparationPolicy policy{
      .enable_sink_clustering = enable_sink_clustering,
      .max_fanout = config.get_max_fanout(),
      .max_cap_pf = config.has_max_cap() ? config.get_max_cap() : std::numeric_limits<double>::infinity(),
      .clock_route_segment_rc = {},
      .buffer_cell_masters = &config.get_buffer_types(),
  };
  if (enable_sink_clustering) {
    policy.clock_route_segment_rc = wrapper.queryConfiguredClockRouteSegmentRc(config);
  }
  return policy;
}

auto BuildSinkHtreeInput(const Topology::Input& input, Net& root_net) -> HTree::Input
{
  const auto& config = *input.config;
  auto& design = *input.design;
  auto& wrapper = *input.wrapper;
  auto& fast_sta = *input.fast_sta;
  return HTree::Input{
      .root_net = &root_net,
      .design = &design,
      .wrapper = &wrapper,
      .characterization_library = input.characterization_library,
      .characterization_input = CharacterizationLibrary::buildRuntimeInput(CharacterizationRuntimeInput{
          .config = &config,
          .wrapper = &wrapper,
          .fast_sta = &fast_sta,
      }),
      .characterization_config = CharacterizationLibrary::buildRuntimeConfig(config),
      .additional_characterization_lengths_um = input.additional_characterization_lengths_um,
      .fixed_topology_root_location = std::nullopt,
      .clock_period_ns = input.clock_period_ns,
      .clock_period_source = input.clock_period_source,
      .log_context = input.log_context,
      .object_name_prefix = input.object_name_prefix,
  };
}

auto BuildSinkHtreeConfig(const Config& config) -> HTree::Config
{
  HTree::Config htree_config{
      .force_branch_buffer = config.is_force_branch_buffer(),
      .depth_explore_window = 0U,
      .topology_tolerance = config.get_htree_topology_tolerance(),
      .max_fanout = config.get_max_fanout(),
      .has_max_cap = config.has_max_cap(),
      .max_cap_pf = config.has_max_cap() ? config.get_max_cap() : 0.0,
      .enable_root_driver_sizing = true,
      .allow_boundary_relaxation = false,
      .enable_analytical_solver = config.is_enable_analytical_htree(),
      .routing_layer = ResolveRoutingLayer(config),
      .wire_width_um = ResolveWireWidth(config),
  };
  ApplyMinTopInputSlew(config, htree_config);
  return htree_config;
}

}  // namespace

auto BuildSinkTree(const Topology::Input& input, const Topology::Config& config) -> Topology::Build
{
  if (input.config == nullptr) {
    CTSLOG.error(Loc::current(), "Topology sink tree build requires an explicit config.");
  }
  if (input.design == nullptr) {
    CTSLOG.error(Loc::current(), "Topology sink tree build requires an explicit design.");
  }
  if (input.wrapper == nullptr) {
    CTSLOG.error(Loc::current(), "Topology sink tree build requires an explicit wrapper.");
  }
  if (input.fast_sta == nullptr) {
    CTSLOG.error(Loc::current(), "Topology sink tree build requires an explicit FastSTA.");
  }
  if (input.root_net == nullptr) {
    CTSLOG.error(Loc::current(), "Topology sink tree build requires an explicit root net.");
  }
  const auto& flow_config = *input.config;
  auto& design = *input.design;
  auto& wrapper = *input.wrapper;
  auto& root_net = *input.root_net;

  Topology::Build result;
  auto* root_driver = root_net.get_driver();
  if (root_driver == nullptr) {
    CTSLOG.warn(Loc::current(), "Topology: root net \"", root_net.get_name(), "\" driver is null.");
    result.summary.failure_reason = "root net driver is null";
    return result;
  }

  const auto root_loads = CollectValidLoads(root_net);
  if (root_loads.empty()) {
    CTSLOG.warn(Loc::current(), "Topology: root net \"", root_net.get_name(), "\" has no loads.");
    result.summary.failure_reason = "root net load list is empty";
    return result;
  }
  const bool enable_sink_clustering = config.enable_sink_clustering.value_or(flow_config.is_enable_sink_clustering());
  const auto sink_load_preparation_policy = BuildSinkTreeLoadPreparationPolicy(flow_config, wrapper, enable_sink_clustering);

  RootNetSideEffectGuard root_net_side_effects(design, root_net, root_driver);
  ConnectNet(TopologyNetConnectionInput{
      .net = &root_net,
      .driver = root_driver,
      .sinks = root_loads,
  });

  auto sink_preparation = PrepareSinkTreeLoads(SinkTreeLoadPreparationInput{
      .build = &result,
      .root_loads = &root_loads,
      .wrapper = &wrapper,
      .object_name_prefix = input.object_name_prefix,
      .policy = sink_load_preparation_policy,
  });
  if (!sink_preparation.success) {
    root_net_side_effects.restore();
    return result;
  }
  if (result.summary.sink_clustering_enabled) {
    ConnectNet(TopologyNetConnectionInput{
        .net = &root_net,
        .driver = root_driver,
        .sinks = sink_preparation.htree_sinks,
    });
  }

  auto htree_input = BuildSinkHtreeInput(input, root_net);
  auto htree_config = BuildSinkHtreeConfig(flow_config);
  auto htree_build = HTree::build(htree_input, htree_config);
  if (!htree_build.summary.success) {
    const std::string htree_failure = htree_build.summary.failure_reason.empty() ? "unknown H-tree failure" : htree_build.summary.failure_reason;
    CTSLOG.warn(Loc::current(), "Topology: H-tree build failed: ", htree_failure);
    result.summary.failure_reason = "H-tree build failed: " + htree_failure;
    root_net_side_effects.restore();
    return result;
  }
  if (htree_build.output.root_net != &root_net) {
    CTSLOG.warn(Loc::current(), "Topology: H-tree output root net does not match input root net \"", root_net.get_name(), "\".");
    result.summary.failure_reason = "H-tree output root net mismatch";
    root_net_side_effects.restore();
    return result;
  }
  RecordSinkHtreeBuild(result, std::move(htree_build));

  if (result.summary.sink_clustering_enabled) {
    const auto dbu_per_um = wrapper.queryDbUnit();
    if (!dbu_per_um.has_value()) {
      CTSLOG.warn(Loc::current(), "Topology: clustered sink-tree distance reporting failed because DBU-per-micron is unavailable.");
      result.summary.failure_reason = "dbu_per_um_unavailable";
      root_net_side_effects.restore();
      return result;
    }
    result.summary.cluster_leaf_distance_summary = CalculateClusterLeafDistance(ClusterLeafDistanceInput{
        .dbu_per_um = *dbu_per_um,
        .build = &result,
    });
  }
  result.summary.success = true;
  return result;
}

}  // namespace icts::topology
