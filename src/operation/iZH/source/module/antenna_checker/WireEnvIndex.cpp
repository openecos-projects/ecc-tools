// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
// ***************************************************************************************

#include "WireEnvIndex.hpp"

#include "IdbBlockages.h"
#include "IdbInstance.h"
#include "IdbLayer.h"
#include "IdbNet.h"
#include "IdbRegularWire.h"
#include "IdbVias.h"

namespace izh {

void WireEnvIndex::insertRect(std::map<int, RTree>& tree_map, int order, int32_t lx, int32_t ly, int32_t hx, int32_t hy, idb::IdbNet* net)
{
  if (hx < lx) {
    std::swap(hx, lx);
  }
  if (hy < ly) {
    std::swap(hy, ly);
  }
  BGRectInt box(BGPointInt(lx, ly), BGPointInt(hx, hy));
  tree_map[order].insert(std::make_pair(box, net));
}

void WireEnvIndex::insertWire(int layer_order, int32_t lx, int32_t ly, int32_t hx, int32_t hy, idb::IdbNet* net)
{
  insertRect(_wire_rtree, layer_order, lx, ly, hx, hy, net);
}

void WireEnvIndex::insertVia(idb::IdbVia* via, idb::IdbNet* net)
{
  if (via == nullptr) {
    return;
  }
  idb::IdbLayerShape cut_shape = via->get_cut_layer_shape();
  if (cut_shape.get_layer() != nullptr) {
    int cut_order = static_cast<int>(cut_shape.get_layer()->get_order());
    for (idb::IdbRect* r : cut_shape.get_rect_list()) {
      if (r != nullptr) {
        insertRect(_via_rtree, cut_order, r->get_low_x(), r->get_low_y(), r->get_high_x(), r->get_high_y(), net);
      }
    }
  }
  idb::IdbLayerShape bottom_shape = via->get_bottom_layer_shape();
  if (bottom_shape.get_layer() != nullptr) {
    int order = static_cast<int>(bottom_shape.get_layer()->get_order());
    for (idb::IdbRect* r : bottom_shape.get_rect_list()) {
      if (r != nullptr) {
        insertRect(_wire_rtree, order, r->get_low_x(), r->get_low_y(), r->get_high_x(), r->get_high_y(), net);
      }
    }
  }
  idb::IdbLayerShape top_shape = via->get_top_layer_shape();
  if (top_shape.get_layer() != nullptr) {
    int order = static_cast<int>(top_shape.get_layer()->get_order());
    for (idb::IdbRect* r : top_shape.get_rect_list()) {
      if (r != nullptr) {
        insertRect(_wire_rtree, order, r->get_low_x(), r->get_low_y(), r->get_high_x(), r->get_high_y(), net);
      }
    }
  }
}

void WireEnvIndex::insertInstance(idb::IdbInstance* inst)
{
  if (inst == nullptr || inst->get_bounding_box() == nullptr) {
    return;
  }
  idb::IdbRect* box = inst->get_bounding_box();
  _inst_rtree.insert(std::make_pair(BGRectInt(BGPointInt(box->get_low_x(), box->get_low_y()), BGPointInt(box->get_high_x(), box->get_high_y())), 0));
}

void WireEnvIndex::rebuild(idb::IdbDesign* design, const RoutingContext& ctx)
{
  _wire_rtree.clear();
  _via_rtree.clear();
  _inst_rtree = InstRTree();
  if (design == nullptr) {
    return;
  }

  if (design->get_net_list() != nullptr) {
    for (idb::IdbNet* net : design->get_net_list()->get_net_list()) {
      if (net == nullptr || net->get_wire_list() == nullptr) {
        continue;
      }
      for (idb::IdbRegularWire* wire : net->get_wire_list()->get_wire_list()) {
        if (wire == nullptr) {
          continue;
        }
        for (idb::IdbRegularWireSegment* seg : wire->get_segment_list()) {
          if (seg == nullptr) {
            continue;
          }
          if (seg->is_wire() && seg->get_layer() != nullptr) {
            idb::IdbRect rect = seg->get_segment_rect();
            insertRect(_wire_rtree, static_cast<int>(seg->get_layer()->get_order()), rect.get_low_x(), rect.get_low_y(), rect.get_high_x(),
                       rect.get_high_y(), net);
          }
          if (seg->is_rect() && seg->get_layer() != nullptr && seg->get_point_start() != nullptr && seg->get_delta_rect() != nullptr) {
            int32_t x = seg->get_point_start()->get_x();
            int32_t y = seg->get_point_start()->get_y();
            insertRect(_wire_rtree, static_cast<int>(seg->get_layer()->get_order()), x + seg->get_delta_rect()->get_low_x(),
                       y + seg->get_delta_rect()->get_low_y(), x + seg->get_delta_rect()->get_high_x(),
                       y + seg->get_delta_rect()->get_high_y(), net);
          }
          if (seg->is_via()) {
            for (idb::IdbVia* via : seg->get_via_list()) {
              insertVia(via, net);
            }
          }
        }
      }
    }
  }

  std::vector<InstItem> inst_items;
  if (design->get_instance_list() != nullptr) {
    for (idb::IdbInstance* inst : design->get_instance_list()->get_instance_list()) {
      if (inst == nullptr || inst->get_bounding_box() == nullptr) {
        continue;
      }
      idb::IdbRect* box = inst->get_bounding_box();
      inst_items.emplace_back(BGRectInt(BGPointInt(box->get_low_x(), box->get_low_y()), BGPointInt(box->get_high_x(), box->get_high_y())),
                              0);
    }
  }
  if (design->get_blockage_list() != nullptr) {
    for (idb::IdbBlockage* blk : design->get_blockage_list()->get_blockage_list()) {
      if (blk == nullptr || !blk->is_palcement_blockage()) {
        continue;
      }
      for (idb::IdbRect* r : blk->get_rect_list()) {
        if (r != nullptr) {
          inst_items.emplace_back(BGRectInt(BGPointInt(r->get_low_x(), r->get_low_y()), BGPointInt(r->get_high_x(), r->get_high_y())), 1);
        }
      }
    }
  }
  if (!inst_items.empty()) {
    _inst_rtree = InstRTree(inst_items.begin(), inst_items.end());
  }

  (void) ctx;
}

bool WireEnvIndex::hasOverlap(int layer_order, int32_t lx, int32_t ly, int32_t hx, int32_t hy, idb::IdbNet* skip_net) const
{
  auto it = _wire_rtree.find(layer_order);
  if (it == _wire_rtree.end()) {
    return false;
  }
  if (hx < lx) {
    std::swap(hx, lx);
  }
  if (hy < ly) {
    std::swap(hy, ly);
  }
  BGRectInt query(BGPointInt(lx, ly), BGPointInt(hx, hy));
  std::vector<Item> hits;
  it->second.query(bgi::intersects(query), std::back_inserter(hits));
  for (const auto& hit : hits) {
    if (hit.second != skip_net) {
      return true;
    }
  }
  return false;
}

bool WireEnvIndex::hasViaOverlap(int32_t x, int32_t y, int cut_order, int32_t spacing, idb::IdbNet* skip_net) const
{
  auto it = _via_rtree.find(cut_order);
  if (it == _via_rtree.end()) {
    return false;
  }
  int32_t pad = std::max(spacing, 1);
  BGRectInt query(BGPointInt(x - pad, y - pad), BGPointInt(x + pad, y + pad));
  std::vector<Item> hits;
  it->second.query(bgi::intersects(query), std::back_inserter(hits));
  for (const auto& hit : hits) {
    if (hit.second != skip_net) {
      return true;
    }
  }
  return false;
}

bool WireEnvIndex::inDie(const RoutingContext& ctx, int32_t x, int32_t y) const
{
  return x >= ctx.get_die_llx() && x <= ctx.get_die_urx() && y >= ctx.get_die_lly() && y <= ctx.get_die_ury();
}

bool WireEnvIndex::hasInstanceOverlap(int32_t lx, int32_t ly, int32_t hx, int32_t hy) const
{
  if (hx < lx) {
    std::swap(hx, lx);
  }
  if (hy < ly) {
    std::swap(hy, ly);
  }
  BGRectInt query(BGPointInt(lx, ly), BGPointInt(hx, hy));
  std::vector<InstItem> hits;
  _inst_rtree.query(bgi::intersects(query), std::back_inserter(hits));
  return !hits.empty();
}

bool WireEnvIndex::findFreeDiodeSite(const RoutingContext& ctx, int32_t pin_x, int32_t pin_y, int32_t width, int32_t height, int32_t& out_x,
                                     int32_t& out_y, idb::IdbOrient& out_orient) const
{
  if (ctx.get_rows().empty() || width <= 0 || height <= 0) {
    return false;
  }

  std::vector<const RCRow*> sorted_rows;
  sorted_rows.reserve(ctx.get_rows().size());
  for (const RCRow& row : ctx.get_rows()) {
    sorted_rows.push_back(&row);
  }
  std::sort(sorted_rows.begin(), sorted_rows.end(), [pin_y](const RCRow* a, const RCRow* b) {
    return std::abs(a->origin_y - pin_y) < std::abs(b->origin_y - pin_y);
  });

  int32_t max_row_attempts = std::min(static_cast<int32_t>(sorted_rows.size()), 3);

  for (int32_t ri = 0; ri < max_row_attempts; ++ri) {
    const RCRow* row = sorted_rows[ri];
    int32_t site_need = (width + row->site_width - 1) / row->site_width;
    if (site_need <= 0) {
      site_need = 1;
    }
    int32_t pin_site = (pin_x - row->origin_x) / row->site_width;
    int32_t max_sites = ctx.get_search_radius() / std::max(row->site_width, 1);

    for (int32_t d = 0; d <= max_sites; ++d) {
      for (int32_t sign : {0, 1, -1}) {
        if (d == 0 && sign != 0) {
          continue;
        }
        int32_t site = pin_site + sign * d;
        if (site < 0 || site + site_need > row->site_count) {
          continue;
        }
        int32_t x = row->origin_x + site * row->site_width;
        int32_t y = row->origin_y;
        if (hasInstanceOverlap(x, y, x + width, y + height)) {
          continue;
        }
        if (!inDie(ctx, x, y) || !inDie(ctx, x + width, y + height)) {
          continue;
        }
        out_x = x;
        out_y = y;
        out_orient = row->orient;
        return true;
      }
    }
  }
  return false;
}

}  // namespace izh
