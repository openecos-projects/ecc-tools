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
#include <limits>
#include <omp.h>

#include "ProcessVariation.hh"
#include "TopoPool.hh"
#include "ThicknessModel.hh"
#include "WidthModel.hh"
#include "log/Log.hh"
namespace ircx {

void ProcessVariation::reset()
{
  layout_data_ = nullptr;
  net_env_pools_ = nullptr;
  layer_table_ = nullptr;
  topo_pool_ = nullptr;
  corners_.clear();
  metal_density_.clear();
  corner_num_ = 0;
  net_num_ = 0;
  corner_net_etch_pools_.clear();
}

bool ProcessVariation::buildEtchPools()
{
  if (!layout_data_) {
    LOG_ERROR << "build process variation failed: LayoutData not initialized.";
    return false;
  }
  if (!net_env_pools_) {
    LOG_ERROR << "build process variation failed: environment pools not set.";
    return false;
  }
  if (!layer_table_) {
    LOG_ERROR << "build process variation failed: LayerTable not initialized.";
    return false;
  }
  if (!topo_pool_) {
    LOG_ERROR << "build process variation failed: TopoPool not initialized.";
    return false;
  }
  if (corners_.empty()) {
    LOG_ERROR << "build process variation failed: process corners not set.";
    return false;
  }

  initMetalDensity();
  initEtchIntervals();


  WidthModel wm;
  wm.set_topo_pool(topo_pool_);
  wm.set_layer_table(layer_table_);
  wm.set_corners(corners());
  for (Size corner_idx = 0; corner_idx < corner_num_; ++corner_idx) {
    #pragma omp parallel for schedule(dynamic)
    for (Size net_idx = 0; net_idx < net_num_; ++net_idx) {
      wm.apply_width_variation(
          corner_idx,
          net_idx,
          corner_net_etch_pools_[corner_net_pool_index(corner_idx, net_idx)]);
    }
  }

  ThicknessModel tm;
  tm.set_layout_data(layout_data_);
  tm.set_topo_pool(topo_pool_);
  tm.set_layer_table(layer_table_);
  tm.set_corners(corners());
  tm.set_metal_density(&metal_density_);

  for (Size corner_idx = 0; corner_idx < corner_num_; ++corner_idx) {
    #pragma omp parallel for schedule(dynamic)
    for (Size net_idx = 0; net_idx < net_num_; ++net_idx) {
      tm.apply_thickness_variation(
          corner_idx,
          net_idx,
          corner_net_etch_pools_[corner_net_pool_index(corner_idx, net_idx)]);
    }
  }

  return true;
}

void ProcessVariation::initMetalDensity()
{
  metal_density_.set_topo_pool(topo_pool_);
  metal_density_.build();
}

void ProcessVariation::initEtchIntervals()
{
  net_num_ = layout_data_->regular_net_count();
  corner_num_ = corners_.size();
  corner_net_etch_pools_.clear();
  corner_net_etch_pools_.resize(net_num_ * corner_num_);

  const std::vector<EnvPool>& net_env_pools = *net_env_pools_;

  Micron dbu_to_micron = 1. / layout_data_->micron_to_dbu;

  for (Size corner_idx = 0; corner_idx < corner_num_; ++corner_idx) {
    #pragma omp parallel for schedule(dynamic)
    for (Size net_idx = 0; net_idx < net_num_; ++net_idx) {
      const auto net_edges = topo_pool_->net_edges(net_idx);
      const Size edge_count = net_edges.size();

      EtchPool& etch_pool = corner_net_etch_pools_[corner_net_pool_index(corner_idx, net_idx)];
      const EnvPool& env_pool = net_env_pools[net_idx];

      for (Size edge_idx = 0; edge_idx < edge_count; ++edge_idx) {
        const TopoEdge& edge = net_edges[edge_idx];

        if (edge.is_via()) {
          etch_pool.append_edge_etch_interval_pool({});  // placeholder to keep index aligned with TopoPool
          continue;
        }

        const std::span<const EnvInterval> env_intervals =
            env_pool.edge_env_interval_pool(edge_idx);
        const Size interval_count = env_intervals.size();
        std::vector<EtchInterval> etch_intervals(interval_count);

        for (Size interval_idx = 0; interval_idx < interval_count; ++interval_idx) {
          const EnvInterval& env_interval = env_intervals[interval_idx];
          EtchInterval& etch_interval = etch_intervals[interval_idx];

          etch_interval.a0     = env_interval.a0 * dbu_to_micron;
          etch_interval.a1     = env_interval.a1 * dbu_to_micron;
          etch_interval.center = edge.fixed() * dbu_to_micron;
          etch_interval.width  = edge.width() * dbu_to_micron;

          // convert center-to-center spacing (EnvInterval) to edge-to-edge gap (EtchInterval)
          if (env_interval.lo_adjacent)
            etch_interval.lo_spacing =
                (env_interval.lo_spacing - env_interval.lo_adjacent->half_width() - edge.half_width())
                * dbu_to_micron;
          if (env_interval.hi_adjacent)
            etch_interval.hi_spacing =
                (env_interval.hi_spacing - env_interval.hi_adjacent->half_width() - edge.half_width())
                * dbu_to_micron;

          etch_interval.thickness = 0;
          etch_interval.height    = 0;
        }

        etch_pool.append_edge_etch_interval_pool(std::move(etch_intervals));
      }
    }
  }
}

// accessors

EtchPool&
ProcessVariation::corner_net_etch_pool(Size corner_idx, Size net_id)
{
  Size pool_idx = corner_net_pool_index(corner_idx, net_id);
  return corner_net_etch_pools_[pool_idx];
}

const EtchPool&
ProcessVariation::corner_net_etch_pool(Size corner_idx, Size net_id) const
{
  Size pool_idx = corner_net_pool_index(corner_idx, net_id);
  return corner_net_etch_pools_[pool_idx];
}

} // namespace ircx
