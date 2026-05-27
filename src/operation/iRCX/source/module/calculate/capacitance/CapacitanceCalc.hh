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

#include <span>
#include <vector>

#include "EtchPool.hh"
#include "EnvPool.hh"
#include "LayoutData.hh"
#include "RCXData.hh"
#include "RCTable.hh"
#include "UnitUtils.hh"
namespace ircx {

namespace parser {
class CapTable;
}

class LayoutData;
class LayerTable;
class TopoEdge;
class TopoPool;
}

namespace itf {
class ProcessCorner;
}

namespace ircx {

class CapacitanceCalc
{
 public:
  CapacitanceCalc() = default;
  ~CapacitanceCalc() = default;

  void set_layout_data(const LayoutData* v) {
    layout_data_ = v;
    dbu_to_micron_ = dbuToMicronScale(v->micron_to_dbu);
  }
  void set_net_env_pools(const std::vector<EnvPool>* v) { net_env_pools_ = v; }
  void set_corner_net_etch_pools(const std::vector<EtchPool>* v) { corner_net_etch_pools_ = v; }
  void set_layer_table(const LayerTable* v) { layer_table_ = v; }
  void set_topo_pool(const TopoPool* v) { topo_pool_ = v; }
  void set_corner_data(const std::vector<RCXData::CornerData>* v) { corner_data_ = v; }
  void set_rc_table(RCTable* v) { rc_table_ = v; }

  bool calc();

  CapacitanceCalc(const CapacitanceCalc&) = delete;
  CapacitanceCalc(CapacitanceCalc&&) = delete;
  auto operator=(const CapacitanceCalc&) -> CapacitanceCalc& = delete;
  auto operator=(CapacitanceCalc&&) -> CapacitanceCalc& = delete;

 private:
  bool validateInputs() const;
  void calcNet(
      Size corner_idx,
      Size net_idx,
      const parser::CapTable& cap_table,
      const EtchPool& corner_net_etch_pool,
      const EnvPool& net_env_pool);
  void calcEdge(
      Size corner_idx,
      Size net_idx,
      Size edge_idx,
      const TopoEdge& edge,
      const parser::CapTable& cap_table,
      std::span<F64> edge_ground_caps,
      const EnvPool& net_env_pool,
      const EtchPool& corner_net_etch_pool);

  Micron dbu_to_micron_{1};

  const LayoutData* layout_data_{nullptr};
  const std::vector<EnvPool>* net_env_pools_{nullptr};
  const std::vector<EtchPool>* corner_net_etch_pools_{nullptr};
  const LayerTable* layer_table_{nullptr};
  const TopoPool* topo_pool_{nullptr};
  const std::vector<RCXData::CornerData>* corner_data_{nullptr};

  RCTable* rc_table_{nullptr};
};

}  // namespace ircx
