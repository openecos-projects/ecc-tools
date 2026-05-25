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
#pragma once

#include <vector>

#include "Types.hh"
#include "EtchPool.hh"
#include "EnvPool.hh"
#include "MetalDensity.hh"
namespace ircx {
  class LayoutData;
  class ProcessCorner;
  class LayerTable;
  class MetalDensity;
  class TopoPool;
}

namespace itf {
  class ProcessCorner;
}

namespace ircx {

class ProcessVariation final
{
 public:
  ProcessVariation() = default;
  ~ProcessVariation() = default;

  // Disallow copy/move
  ProcessVariation(const ProcessVariation&) = delete;
  ProcessVariation& operator=(const ProcessVariation&) = delete;
  ProcessVariation(ProcessVariation&&) = delete;
  ProcessVariation& operator=(ProcessVariation&&) = delete;

  void set_layout_data(const LayoutData* v) { layout_data_ = v; }
  void set_net_env_pools(const std::vector<EnvPool>* v) { net_env_pools_ = v; }
  void set_layer_table(const LayerTable* v) { layer_table_ = v; }
  void set_topo_pool(const TopoPool* v) { topo_pool_ = v; }
  void set_corners(const std::vector<::itf::ProcessCorner*>& v) { corners_ = v; }

  std::vector<::itf::ProcessCorner*>& corners() { return corners_; }
  const std::vector<::itf::ProcessCorner*>& corners() const { return corners_; }
  Size corner_num() const { return corner_num_; }

  EtchPool& corner_net_etch_pool(Size corner_idx, Size net_id);
  const EtchPool& corner_net_etch_pool(Size corner_idx, Size net_id) const;
  const std::vector<EtchPool>& corner_net_etch_pools() const { return corner_net_etch_pools_; }

  // other built data
  const MetalDensity* metal_density() const { return &metal_density_; }

  // entry points
  void reset();
  [[nodiscard]] bool buildEtchPools();

 private:
  void initMetalDensity();
  void initEtchIntervals();

  Size corner_net_pool_index(Size corner_idx, Size net_id) const {
    return corner_idx * net_num_ + net_id;
  }

  // set from outside
  const LayoutData* layout_data_{nullptr};
  const std::vector<EnvPool>* net_env_pools_{nullptr};
  const LayerTable* layer_table_{nullptr};
  const TopoPool* topo_pool_{nullptr};
  std::vector<::itf::ProcessCorner*> corners_{};

  // built here
  MetalDensity metal_density_;

  Size corner_num_{0};

  Size net_num_{0};
  std::vector<EtchPool> corner_net_etch_pools_;
};

} // namespace ircx
