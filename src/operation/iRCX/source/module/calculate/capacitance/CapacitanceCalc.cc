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
 * @file CapacitanceCalc.cc
 * @brief iRCX module implementation detail.
 */
#include <algorithm>

#include "CapTableQuery.hh"
#include "CapacitanceCalc.hh"
#include "EdgeCapAccumulator.hh"
#include "LayerTable.hh"
#include "LayoutData.hh"
#include "NetEnvironment.hh"
#include "NetEtchProfile.hh"
#include "ParallelUtils.hh"
#include "ProcessCorner.hpp"
#include "CapTable.hpp"
#include "RCTable.hh"
#include "TopoPool.hh"
#include "log/Log.hh"

namespace ircx {

void CapacitanceCalc::set_layout_data(const LayoutData* v)
{
  layout_data_ = v;
  micron_per_dbu_ = unit::toMicron(1, v->dbu_per_micron);
}

namespace {

void resolveCrossLayers(const LayerTable& layer_table,
                        const CrossOverlapSub* cross_seg,
                        std::string& below_layer,
                        std::string& above_layer)
{
  below_layer = "SUBSTRATE";
  above_layer.clear();

  if (cross_seg == nullptr) {
    return;
  }

  if (cross_seg->blw_layer != 0) {
    const Size process_layer_id = layer_table.designToProcessId(cross_seg->blw_layer);
    below_layer = layer_table.processName(process_layer_id);
  }
  if (cross_seg->abv_layer != 0) {
    const Size process_layer_id = layer_table.designToProcessId(cross_seg->abv_layer);
    above_layer = layer_table.processName(process_layer_id);
  }
}

void accumulateSegmentCap(const LayerTable& layer_table,
                          Micron micron_per_dbu,
                          Dbu segment_lo_dbu,
                          Dbu segment_hi_dbu,
                          const CrossOverlapSub* cross_overlap,
                          const SideContext& low_side,
                          const SideContext& high_side,
                          EdgeCapAccumulator& accumulator)
{
  if (segment_hi_dbu <= segment_lo_dbu) {
    return;
  }

  std::string below_layer;
  std::string above_layer;
  resolveCrossLayers(layer_table, cross_overlap, below_layer, above_layer);
  accumulator.accumulateSpan(
      (segment_hi_dbu - segment_lo_dbu) * micron_per_dbu,
      below_layer,
      above_layer,
      low_side,
      high_side);
}

}  // namespace

bool CapacitanceCalc::calc()
{
  if (!validateInputs()) {
    return false;
  }

  const Size corner_count = corner_data_->size();
  const Size net_count = layout_data_->get_regular_net_count();

  for (Size corner_idx = 0; corner_idx < corner_count; ++corner_idx) {
    const auto& corner_data = (*corner_data_)[corner_idx];
    if (!corner_data.process_corner) {
      LOG_ERROR << "process corner missing for corner " << corner_data.name;
      return false;
    }
    const parser::CapTable& cap_table = corner_data.cap_table;

    const int net_threads = parallel::threadCount(net_count);
#pragma omp parallel for schedule(dynamic) num_threads(net_threads)
    for (Size net_idx = 0; net_idx < net_count; ++net_idx) {
      calcNet(
          corner_idx,
          net_idx,
          cap_table,
          corner_net_etch_pools_->at({corner_idx, net_idx}),
          (*net_environments_)[net_idx]);
    }
  }

  // merge per-net coupling cap entries (sequential)
  rc_table_->mergeNetCcapEntries();
  return true;
}

bool CapacitanceCalc::validateInputs() const
{
  if (layout_data_ == nullptr) {
    LOG_ERROR << "calculate capacitance failed: layout data not set.";
    return false;
  }
  if (net_environments_ == nullptr) {
    LOG_ERROR << "calculate capacitance failed: net environments not set.";
    return false;
  }
  if (corner_net_etch_pools_ == nullptr) {
    LOG_ERROR << "calculate capacitance failed: etch profiles not set.";
    return false;
  }
  if (layer_table_ == nullptr) {
    LOG_ERROR << "calculate capacitance failed: layer table not set.";
    return false;
  }
  if (topo_pool_ == nullptr) {
    LOG_ERROR << "calculate capacitance failed: topology pool not set.";
    return false;
  }
  if (rc_table_ == nullptr) {
    LOG_ERROR << "calculate capacitance failed: RC table not set.";
    return false;
  }
  if (corner_data_ == nullptr || corner_data_->empty()) {
    LOG_ERROR << "calculate capacitance failed: process corners not set.";
    return false;
  }
  if (corner_net_etch_pools_->get_corner_num() != corner_data_->size()
      || corner_net_etch_pools_->get_net_num() != layout_data_->get_regular_net_count()) {
    LOG_ERROR << "calculate capacitance failed: etch profile dimensions mismatch.";
    return false;
  }

  return true;
}

void CapacitanceCalc::calcNet(Size corner_idx,
                              Size net_idx,
                              const parser::CapTable& cap_table,
                              const NetEtchProfile& etch_profile,
                              const NetEnvironment& environment)
{
  const auto get_net_edges = topo_pool_->get_net_edges(net_idx);
  auto edge_ground_caps = rc_table_->get_corner_net_gcap_pool({corner_idx, net_idx});

  for (Size edge_idx = 0; edge_idx < get_net_edges.size(); ++edge_idx) {
    calcEdge(
        corner_idx,
        net_idx,
        edge_idx,
        get_net_edges[edge_idx],
        cap_table,
        edge_ground_caps,
        environment,
        etch_profile);
  }
}

void CapacitanceCalc::calcEdge(Size corner_idx,
                               Size net_idx,
                               Size edge_idx,
                               const TopoEdge& edge,
                               const parser::CapTable& cap_table,
                               std::span<F64> edge_ground_caps,
                               const NetEnvironment& environment,
                               const NetEtchProfile& etch_profile)
{
  if (edge.is_via()) {
    return;
  }

  const Size process_layer_id = layer_table_->designToProcessId(edge.get_layer_id());
  const std::string& layer_name = layer_table_->processName(process_layer_id);

  // Global index of current edge in TopoPool::get_edge_pool()
  const Size edge_global_idx = topo_pool_->get_edge_index(edge);

  const auto env_intervals = environment.edgeIntervals(edge_idx);
  const auto etch_intervals = etch_profile.edgeIntervals(edge_idx);
  LOG_ERROR_IF(env_intervals.size() != etch_intervals.size())
      << "environment/etch interval count mismatch for net "
      << net_idx << ", edge " << edge_idx << ".";
  const Size interval_count = std::min(env_intervals.size(), etch_intervals.size());

  const CapTableQuery cap_query(cap_table, layer_name);
  EdgeCapAccumulator accumulator(
      cap_query,
      *topo_pool_,
      *rc_table_,
      edge_ground_caps,
      corner_idx,
      net_idx,
      edge_idx,
      edge_global_idx);

  for (Size interval_idx = 0; interval_idx < interval_count; ++interval_idx) {
    const EdgeEnvironmentInterval& env_interval = env_intervals[interval_idx];
    const EdgeEtchInterval& etch_interval = etch_intervals[interval_idx];

    const Dbu interval_lo_dbu = env_interval.a0;
    const Dbu interval_hi_dbu = env_interval.a1;
    if (interval_hi_dbu <= interval_lo_dbu) {
      continue;
    }

    const SideContext low_side = makeSideContext(
        etch_interval.lo_spacing, env_interval.lo_adjacent, net_idx);
    const SideContext high_side = makeSideContext(
        etch_interval.hi_spacing, env_interval.hi_adjacent, net_idx);

    Dbu cursor_dbu = interval_lo_dbu;
    for (const CrossOverlapSub& cross_overlap : env_interval.cross_segs) {
      if (cursor_dbu < cross_overlap.a0) {
        accumulateSegmentCap(
            *layer_table_,
            micron_per_dbu_,
            cursor_dbu,
            cross_overlap.a0,
            nullptr,
            low_side,
            high_side,
            accumulator);
        cursor_dbu = cross_overlap.a0;
      }

      const Dbu overlap_hi_dbu = std::min(interval_hi_dbu, cross_overlap.a1);
      if (cursor_dbu < overlap_hi_dbu) {
        accumulateSegmentCap(
            *layer_table_,
            micron_per_dbu_,
            cursor_dbu,
            overlap_hi_dbu,
            &cross_overlap,
            low_side,
            high_side,
            accumulator);
        cursor_dbu = overlap_hi_dbu;
      }
    }

    if (cursor_dbu < interval_hi_dbu) {
      accumulateSegmentCap(
          *layer_table_,
          micron_per_dbu_,
          cursor_dbu,
          interval_hi_dbu,
          nullptr,
          low_side,
          high_side,
          accumulator);
    }
  }
}

}  // namespace ircx
