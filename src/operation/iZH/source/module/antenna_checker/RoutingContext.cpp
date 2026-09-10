// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
// ***************************************************************************************

#include "RoutingContext.hpp"

#include "Logger.hpp"
#include "Utility.hpp"

#include "IdbDie.h"
#include "IdbLayout.h"
#include "IdbRow.h"
#include "IdbSite.h"
#include "IdbUnits.h"
#include "IdbVias.h"

namespace izh {

const RCRoutingLayer* RoutingContext::findRoutingByOrder(int order) const
{
  for (const auto& layer : _routing_layers) {
    if (layer.order == order) {
      return &layer;
    }
  }
  return nullptr;
}

const RCRoutingLayer* RoutingContext::findNextRouting(int order) const
{
  const RCRoutingLayer* best = nullptr;
  for (const auto& layer : _routing_layers) {
    if (layer.order > order && (best == nullptr || layer.order < best->order)) {
      best = &layer;
    }
  }
  return best;
}

const RCCutLayer* RoutingContext::findCutBetween(int below_order, int above_order) const
{
  const RCCutLayer* best = nullptr;
  for (const auto& cut : _cut_layers) {
    if (cut.order > below_order && cut.order < above_order) {
      if (best == nullptr || cut.order < best->order) {
        best = &cut;
      }
    }
  }
  return best;
}

idb::IdbVia* RoutingContext::findViaBetween(int below_order, int above_order) const
{
  const RCCutLayer* cut = findCutBetween(below_order, above_order);
  return cut != nullptr ? cut->via : nullptr;
}

RoutingContext RoutingContext::build(idb::IdbDesign* design, const AFComParam& param)
{
  RoutingContext ctx;
  ctx._design = design;
  ctx._enable_drc = param.get_enable_drc();
  if (design == nullptr || design->get_layout() == nullptr) {
    return ctx;
  }

  idb::IdbLayout* layout = design->get_layout();
  if (layout->get_units() != nullptr) {
    const int dbu = layout->get_units()->get_micron_dbu();
    if (dbu > 0) {
      ctx._micron_dbu = dbu;
    }
  }

  if (layout->get_die() != nullptr) {
    ctx._die_llx = layout->get_die()->get_llx();
    ctx._die_lly = layout->get_die()->get_lly();
    ctx._die_urx = layout->get_die()->get_urx();
    ctx._die_ury = layout->get_die()->get_ury();
  }

  idb::IdbLayers* layers = layout->get_layers();
  if (layers != nullptr) {
    for (idb::IdbLayer* layer : layers->get_routing_layers()) {
      idb::IdbLayerRouting* routing = dynamic_cast<idb::IdbLayerRouting*>(layer);
      if (routing == nullptr) {
        continue;
      }
      RCRoutingLayer info;
      info.layer = routing;
      info.order = static_cast<int>(routing->get_order());
      info.pitch = routing->get_pitch_prefer() > 0 ? routing->get_pitch_prefer() : routing->get_pitch_x();
      info.width = routing->get_width();
      info.min_area = routing->get_area();
      info.spacing = routing->get_spacing(info.width);
      info.horizontal = routing->is_horizontal();
      if (info.pitch <= 0) {
        info.pitch = info.width > 0 ? info.width * 2 : 1;
      }
      if (info.spacing <= 0) {
        info.spacing = info.width;
      }
      ctx._routing_layers.push_back(info);
      ctx._top_routing_order = std::max(ctx._top_routing_order, info.order);
    }

    for (idb::IdbLayer* layer : layers->get_cut_layers()) {
      idb::IdbLayerCut* cut = dynamic_cast<idb::IdbLayerCut*>(layer);
      if (cut == nullptr) {
        continue;
      }
      RCCutLayer info;
      info.layer = cut;
      info.order = static_cast<int>(cut->get_order());
      if (!cut->get_spacings().empty() && cut->get_spacings().front() != nullptr) {
        info.spacing = cut->get_spacings().front()->get_spacing();
      }
      ctx._cut_layers.push_back(info);
    }
  }

  idb::IdbVias* lef_vias = layout->get_via_list();
  if (lef_vias != nullptr) {
    for (RCCutLayer& cut : ctx._cut_layers) {
      for (idb::IdbVia* via : lef_vias->get_via_list()) {
        if (via == nullptr || via->get_instance() == nullptr) {
          continue;
        }
        idb::IdbLayerShape* cut_shape = via->get_instance()->get_cut_layer_shape();
        if (cut_shape == nullptr || cut_shape->get_layer() != cut.layer) {
          continue;
        }
        cut.via = via;
        break;
      }
    }
  }

  if (layout->get_rows() != nullptr) {
    for (idb::IdbRow* row : layout->get_rows()->get_row_list()) {
      if (row == nullptr || row->get_site() == nullptr || row->get_original_coordinate() == nullptr) {
        continue;
      }
      if (!row->is_horizontal()) {
        continue;
      }
      RCRow rc_row;
      rc_row.origin_x = row->get_original_coordinate()->get_x();
      rc_row.origin_y = row->get_original_coordinate()->get_y();
      rc_row.site_width = row->get_step_x() > 0 ? row->get_step_x() : row->get_site()->get_width();
      rc_row.row_height = row->get_site()->get_height();
      rc_row.site_count = row->get_row_num_x();
      rc_row.orient = row->get_orient() == idb::IdbOrient::kNone ? idb::IdbOrient::kN_R0 : row->get_orient();
      if (rc_row.site_width > 0 && rc_row.row_height > 0 && rc_row.site_count > 0) {
        ctx._rows.push_back(rc_row);
      }
    }
  }

  int32_t site_width = ctx._rows.empty() ? 0 : ctx._rows.front().site_width;
  int32_t pitch = ctx._routing_layers.empty() ? site_width : ctx._routing_layers.front().pitch;
  ctx._search_radius = param.get_search_radius();
  ctx._max_jog = param.get_max_jog();
  if (ctx._search_radius <= 0) {
    ctx._search_radius = site_width > 0 ? site_width * 10 : (pitch > 0 ? pitch * 10 : 1000);
  }
  if (ctx._max_jog <= 0) {
    ctx._max_jog = pitch > 0 ? pitch * 20 : ctx._search_radius;
  }

  if (layout->get_cell_master_list() != nullptr) {
    if (!param.get_diode_name_list().empty()) {
      for (const std::string& name : param.get_diode_name_list()) {
        idb::IdbCellMaster* master = layout->get_cell_master_list()->find_cell_master(name);
        if (master != nullptr) {
          ctx._diode_masters.push_back(master);
        } else {
          ZHLOG.warn(Loc::current(), "Cannot find antenna diode cell: ", name);
        }
      }
    } else {
      for (idb::IdbCellMaster* master : layout->get_cell_master_list()->get_cell_master()) {
        if (master != nullptr && master->is_antenna_cell()) {
          ctx._diode_masters.push_back(master);
        }
      }
    }
    std::sort(ctx._diode_masters.begin(), ctx._diode_masters.end(),
              [](idb::IdbCellMaster* a, idb::IdbCellMaster* b) { return a->get_width() < b->get_width(); });
  }

  return ctx;
}

}  // namespace izh
