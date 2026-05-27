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

#include <utility>
#include <vector>

#include "EtchPool.hh"
#include "LayoutData.hh"
#include "RCXData.hh"
#include "RCTable.hh"
#include "UnitUtils.hh"
namespace ircx {

class LayerTable;
class TopoEdge;
class TopoPool;
}

namespace itf {
class LayerConductor;
class LayerVia;
class ProcessCorner;
}

namespace ircx {

class ResistanceCalc
{
 public:
  ResistanceCalc() = default;
  ~ResistanceCalc() = default;

  void set_layout_data(const LayoutData* v) {
    layout_data_ = v;
    dbu_to_micron_ = dbuToMicronScale(v->micron_to_dbu);
  }
  void set_layer_table(const LayerTable* v) { layer_table_ = v; }
  void set_topo_pool(const TopoPool* v) { topo_pool_ = v; }
  void set_corner_net_etch_pools(const std::vector<EtchPool>* v) { corner_net_etch_pools_ = v; }
  void set_rc_table(RCTable* v) { rc_table_ = v; }
  void set_corner_data(const std::vector<RCXData::CornerData>* v) { corner_data_ = v; }

  bool calc();
  std::pair<Micron, Micron> node_range(const TopoEdge& e) const;

  ResistanceCalc(const ResistanceCalc&) = delete;
  ResistanceCalc(ResistanceCalc&&) = delete;
  auto operator=(const ResistanceCalc&) -> ResistanceCalc& = delete;
  auto operator=(ResistanceCalc&&) -> ResistanceCalc& = delete;

 private:
  F64 apply_conductor_temperature_derating(F64 operating_temperature,
                                           const itf::ProcessCorner& corner,
                                           const itf::LayerConductor& layer,
                                           Micron width,
                                           F64 base_resistance) const;
  F64 apply_via_temperature_derating(F64 operating_temperature,
                                     const itf::ProcessCorner& corner,
                                     const itf::LayerVia& layer,
                                     F64 area,
                                     F64 base_resistance) const;

  Micron dbu_to_micron_{1};

  const LayoutData* layout_data_{nullptr};
  const LayerTable* layer_table_{nullptr};
  const TopoPool* topo_pool_{nullptr};
  const std::vector<EtchPool>* corner_net_etch_pools_{nullptr};
  const std::vector<RCXData::CornerData>* corner_data_{nullptr};

  RCTable* rc_table_{nullptr};
};

}  // namespace ircx
