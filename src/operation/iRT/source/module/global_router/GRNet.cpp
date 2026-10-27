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
#include "GRNet.hpp"

#include <algorithm>
#include <cstdint>
#include <set>
#include <vector>

#include "ConnectType.hpp"
#include "DataManager.hpp"
#include "Logger.hpp"
#include "Net.hpp"
#include "Pin.hpp"
#include "Utility.hpp"

namespace irt {

void GRNet::init(Net& net, const GRGridGraph& grid)
{
  origin_net_ = &net;
  bbox_ = net.get_bounding_box();
  is_clock_ = net.get_connect_type() == ConnectType::kClock;
  pin_list_.clear();
  exempt_edge_set_.clear();
  tree_ = GRTree();

  for (Pin& pin : net.get_pin_list()) {
    AccessPoint& access_point = pin.get_access_point();
    pin_list_.push_back(GRPin{&pin, access_point.get_grid_coord(), access_point.get_layer_idx()});
  }
  buildExemptEdgeSet(grid);
}

void GRNet::buildExemptEdgeSet(const GRGridGraph& grid)
{
  ScaleAxis& gcell_axis = RTDM.getDatabase().get_gcell_axis();
  for (GRPin& gr_pin : pin_list_) {
    int32_t layer_idx = gr_pin.layer_idx;
    bool is_horizontal = grid.isHorizontalLayer(layer_idx);
    MacroPinEdge macro_pin_edge = gr_pin.pin->get_macro_pin_edge();
    bool is_vertical_macro_edge = macro_pin_edge == MacroPinEdge::kNorth || macro_pin_edge == MacroPinEdge::kSouth;
    bool is_horizontal_macro_edge = macro_pin_edge == MacroPinEdge::kEast || macro_pin_edge == MacroPinEdge::kWest;
    bool toward_low = false;
    if ((is_horizontal && is_horizontal_macro_edge) || (!is_horizontal && is_vertical_macro_edge)) {
      // A macro pin escapes from the instance edge it sits on. The nearest gcell
      // boundary may lie inside the macro body and leave the pin without a way out.
      toward_low = macro_pin_edge == MacroPinEdge::kSouth || macro_pin_edge == MacroPinEdge::kWest;
    } else {
      PlanarRect gcell_rect = RTUTIL.getRealRectByGCell(gr_pin.grid_coord, gcell_axis);
      PlanarCoord gcell_mid_coord = gcell_rect.getMidPoint();
      PlanarCoord real_coord = gr_pin.pin->get_access_point().get_real_coord();
      if (is_horizontal) {
        if (real_coord.get_x() < gcell_mid_coord.get_x()) {
          toward_low = true;
        } else if (real_coord.get_x() > gcell_mid_coord.get_x()) {
          toward_low = false;
        } else {
          toward_low = bbox_.get_grid_rect().getMidPoint().get_x() < gr_pin.grid_coord.get_x();
        }
      } else {
        if (real_coord.get_y() < gcell_mid_coord.get_y()) {
          toward_low = true;
        } else if (real_coord.get_y() > gcell_mid_coord.get_y()) {
          toward_low = false;
        } else {
          toward_low = bbox_.get_grid_rect().getMidPoint().get_y() < gr_pin.grid_coord.get_y();
        }
      }
    }

    int32_t direction = toward_low ? -1 : 1;
    if (is_horizontal) {
      if (direction < 0 && gr_pin.grid_coord.get_x() == 0) {
        direction = 1;
      } else if (direction > 0 && gr_pin.grid_coord.get_x() == grid.getXSize() - 1) {
        direction = -1;
      }
    } else {
      if (direction < 0 && gr_pin.grid_coord.get_y() == 0) {
        direction = 1;
      } else if (direction > 0 && gr_pin.grid_coord.get_y() == grid.getYSize() - 1) {
        direction = -1;
      }
    }
    addPinEscapeCorridor(grid, gr_pin, is_horizontal, direction);
  }
}

void GRNet::addPinEscapeCorridor(const GRGridGraph& grid, const GRPin& gr_pin, bool is_horizontal, int32_t direction)
{
  int32_t layer_idx = gr_pin.layer_idx;
  int32_t x = gr_pin.grid_coord.get_x();
  int32_t y = gr_pin.grid_coord.get_y();
  GRWireEdgeId edge_id = getEscapeEdgeId(x, y, is_horizontal, direction, layer_idx);
  if (!grid.hasWireEdge(edge_id)) {
    return;
  }
  exempt_edge_set_.insert(edge_id);

  // The enlarged pin shape may still block the neighbor gcell; open at most one more gcell outward.
  int32_t next_x = x + (is_horizontal ? direction : 0);
  int32_t next_y = y + (is_horizontal ? 0 : direction);
  if (next_x < 0 || next_x >= grid.getXSize() || next_y < 0 || next_y >= grid.getYSize()) {
    return;
  }
  GRWireEdgeId next_edge_id = getEscapeEdgeId(next_x, next_y, is_horizontal, direction, layer_idx);
  if (!grid.hasWireEdge(next_edge_id)) {
    return;
  }
  if (!grid.hasUsableSupply(next_edge_id)) {
    exempt_edge_set_.insert(next_edge_id);
  }
}

GRWireEdgeId GRNet::getEscapeEdgeId(int32_t x, int32_t y, bool is_horizontal, int32_t direction, int32_t layer_idx) const
{
  if (is_horizontal) {
    return GRWireEdgeId{direction > 0 ? x : x - 1, y, layer_idx};
  }
  return GRWireEdgeId{x, direction > 0 ? y : y - 1, layer_idx};
}

void GRNet::commitUsage(GRGridGraph& grid, const GRParams& params, const GRTree& tree) const
{
  std::set<GRWireEdgeId> planar_edge_set;
  buildPlanarEdgeSet(grid, tree, planar_edge_set);
  std::set<GRWireEdgeId> charged_edge_set;
  addWireDemand(grid, tree, charged_edge_set, 1.0);
  for (const GRViaStack& via_stack : tree.via_list) {
    addViaDemand(grid, via_stack, planar_edge_set, charged_edge_set, params.via_demand);
  }
}

void GRNet::ripupUsage(GRGridGraph& grid, const GRParams& params, const GRTree& tree) const
{
  std::set<GRWireEdgeId> planar_edge_set;
  buildPlanarEdgeSet(grid, tree, planar_edge_set);
  std::set<GRWireEdgeId> charged_edge_set;
  addWireDemand(grid, tree, charged_edge_set, -1.0);
  for (const GRViaStack& via_stack : tree.via_list) {
    addViaDemand(grid, via_stack, planar_edge_set, charged_edge_set, -params.via_demand);
  }
}

void GRNet::buildPlanarEdgeSet(const GRGridGraph& grid, const GRTree& tree, std::set<GRWireEdgeId>& planar_edge_set) const
{
  for (const GRWireSegment& wire_segment : tree.wire_list) {
    for (GRWireEdgeIter iter(grid, wire_segment.first, wire_segment.second); iter.isValid(); iter.next()) {
      planar_edge_set.insert(iter.getEdgeId());
    }
  }
}

void GRNet::addWireDemand(GRGridGraph& grid, const GRTree& tree, std::set<GRWireEdgeId>& charged_edge_set, double delta) const
{
  for (const GRWireSegment& wire_segment : tree.wire_list) {
    for (GRWireEdgeIter iter(grid, wire_segment.first, wire_segment.second); iter.isValid(); iter.next()) {
      const GRWireEdgeId& edge_id = iter.getEdgeId();
      if (isExempt(edge_id)) {
        continue;
      }
      if (!charged_edge_set.insert(edge_id).second) {
        continue;
      }
      grid.addDemand(edge_id, delta, "wire edge");
    }
  }
}

void GRNet::addViaDemand(GRGridGraph& grid, const GRViaStack& via_stack, const std::set<GRWireEdgeId>& planar_edge_set,
                         std::set<GRWireEdgeId>& charged_edge_set, double delta) const
{
  if (via_stack.bottom_layer_idx < grid.getBottomLayerIdx() || via_stack.top_layer_idx > grid.getTopLayerIdx()
      || via_stack.bottom_layer_idx >= via_stack.top_layer_idx) {
    return;
  }
  for (int32_t layer_idx = via_stack.bottom_layer_idx; layer_idx <= via_stack.top_layer_idx; layer_idx++) {
    if (hasPlanarEdgeOrAP(grid, planar_edge_set, via_stack.coord, layer_idx)) {
      continue;
    }
    for (GRViaEdgeIter iter(grid, via_stack.coord, layer_idx); iter.isValid(); iter.next()) {
      const GRWireEdgeId& edge_id = iter.getEdgeId();
      if (isExempt(edge_id)) {
        continue;
      }
      if (!grid.hasUsableSupply(edge_id)) {
        continue;
      }
      if (!charged_edge_set.insert(edge_id).second) {
        continue;
      }
      grid.addDemand(edge_id, delta, "via projection");
    }
  }
}

bool GRNet::hasPlanarEdgeOrAP(const GRGridGraph& grid, const std::set<GRWireEdgeId>& planar_edge_set, const PlanarCoord& coord, int32_t layer_idx) const
{
  if (isAPViaSide(coord, layer_idx)) {
    return true;
  }
  for (GRViaEdgeIter iter(grid, coord, layer_idx); iter.isValid(); iter.next()) {
    if (planar_edge_set.contains(iter.getEdgeId())) {
      return true;
    }
  }
  return false;
}

bool GRNet::isAPViaSide(const PlanarCoord& coord, int32_t layer_idx) const
{
  for (const GRPin& gr_pin : pin_list_) {
    if (gr_pin.grid_coord == coord && gr_pin.layer_idx == layer_idx) {
      return true;
    }
  }
  return false;
}

bool GRNet::hasOverflow(const GRGridGraph& grid) const
{
  std::set<GRWireEdgeId> checked_edge_set;
  std::set<GRWireEdgeId> planar_edge_set;
  for (const GRWireSegment& wire_segment : tree_.wire_list) {
    for (GRWireEdgeIter iter(grid, wire_segment.first, wire_segment.second); iter.isValid(); iter.next()) {
      const GRWireEdgeId& edge_id = iter.getEdgeId();
      if (!grid.hasWireEdge(edge_id)) {
        continue;
      }
      planar_edge_set.insert(edge_id);
      if (isExempt(edge_id)) {
        continue;
      }
      if (checked_edge_set.insert(edge_id).second && grid.getWireEdge(edge_id).getOverflow() > GRParams::kEpsilon) {
        return true;
      }
    }
  }
  for (const GRViaStack& via_stack : tree_.via_list) {
    if (via_stack.bottom_layer_idx < grid.getBottomLayerIdx() || via_stack.top_layer_idx > grid.getTopLayerIdx()
        || via_stack.bottom_layer_idx >= via_stack.top_layer_idx) {
      continue;
    }
    for (int32_t layer_idx = via_stack.bottom_layer_idx; layer_idx <= via_stack.top_layer_idx; layer_idx++) {
      if (hasPlanarEdgeOrAP(grid, planar_edge_set, via_stack.coord, layer_idx)) {
        continue;
      }
      for (GRViaEdgeIter iter(grid, via_stack.coord, layer_idx); iter.isValid(); iter.next()) {
        const GRWireEdgeId& edge_id = iter.getEdgeId();
        if (isExempt(edge_id)) {
          continue;
        }
        if (!grid.hasUsableSupply(edge_id)) {
          continue;
        }
        if (checked_edge_set.insert(edge_id).second && grid.getWireEdge(edge_id).getOverflow() > GRParams::kEpsilon) {
          return true;
        }
      }
    }
  }
  return false;
}

}  // namespace irt
