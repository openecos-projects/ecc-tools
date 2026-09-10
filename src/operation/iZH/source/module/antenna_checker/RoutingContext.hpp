// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
// ***************************************************************************************
#pragma once

#include "AFComParam.hpp"
#include "IdbCellMaster.h"
#include "IdbDesign.h"
#include "IdbDie.h"
#include "IdbLayer.h"
#include "IdbViaMaster.h"
#include "IdbVias.h"
#include "ZHHeader.hpp"

namespace izh {

class RCRoutingLayer
{
 public:
  idb::IdbLayerRouting* layer = nullptr;
  int order = -1;
  int32_t pitch = 0;
  int32_t width = 0;
  int32_t min_area = 0;
  int32_t spacing = 0;
  bool horizontal = true;
};

class RCCutLayer
{
 public:
  idb::IdbLayerCut* layer = nullptr;
  int order = -1;
  int32_t spacing = 0;
  idb::IdbVia* via = nullptr;
};

class RCRow
{
 public:
  int32_t origin_x = 0;
  int32_t origin_y = 0;
  int32_t site_width = 0;
  int32_t row_height = 0;
  int32_t site_count = 0;
  idb::IdbOrient orient = idb::IdbOrient::kN_R0;
};

class RoutingContext
{
 public:
  static RoutingContext build(idb::IdbDesign* design, const AFComParam& param);

  idb::IdbDesign* get_design() const { return _design; }
  int get_micron_dbu() const { return _micron_dbu; }
  int32_t get_die_llx() const { return _die_llx; }
  int32_t get_die_lly() const { return _die_lly; }
  int32_t get_die_urx() const { return _die_urx; }
  int32_t get_die_ury() const { return _die_ury; }
  const std::vector<RCRoutingLayer>& get_routing_layers() const { return _routing_layers; }
  const std::vector<RCCutLayer>& get_cut_layers() const { return _cut_layers; }
  const std::vector<RCRow>& get_rows() const { return _rows; }
  const std::vector<idb::IdbCellMaster*>& get_diode_masters() const { return _diode_masters; }
  int32_t get_search_radius() const { return _search_radius; }
  int32_t get_max_jog() const { return _max_jog; }
  bool get_enable_drc() const { return _enable_drc; }
  int get_top_routing_order() const { return _top_routing_order; }

  const RCRoutingLayer* findRoutingByOrder(int order) const;
  const RCRoutingLayer* findNextRouting(int order) const;
  const RCCutLayer* findCutBetween(int below_order, int above_order) const;
  idb::IdbVia* findViaBetween(int below_order, int above_order) const;

 private:
  idb::IdbDesign* _design = nullptr;
  int _micron_dbu = 1000;
  int32_t _die_llx = 0;
  int32_t _die_lly = 0;
  int32_t _die_urx = 0;
  int32_t _die_ury = 0;
  std::vector<RCRoutingLayer> _routing_layers;
  std::vector<RCCutLayer> _cut_layers;
  std::vector<RCRow> _rows;
  std::vector<idb::IdbCellMaster*> _diode_masters;
  int32_t _search_radius = 0;
  int32_t _max_jog = 0;
  bool _enable_drc = false;
  int _top_routing_order = -1;
};

}  // namespace izh
