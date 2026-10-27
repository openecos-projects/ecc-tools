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

#include <cstdint>
#include <set>
#include <vector>

#include "ConnectType.hpp"
#include "DataManager.hpp"
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

// exempt the edge where AP lives
void GRNet::buildExemptEdgeSet(const GRGridGraph& grid)
{
  ScaleAxis& gcell_axis = RTDM.getDatabase().get_gcell_axis();
  for (GRPin& gr_pin : pin_list_) {
    Pin* pin = gr_pin.pin;
    int32_t layer_idx = gr_pin.layer_idx;
    bool is_horizontal = grid.isHorizontalLayer(layer_idx);
    // the nearest edge for macro pin
    MacroPinEdge macro_pin_edge = pin->get_macro_pin_edge();
    bool is_vertical_macro_edge = macro_pin_edge == MacroPinEdge::kNorth || macro_pin_edge == MacroPinEdge::kSouth;
    bool is_horizontal_macro_edge = macro_pin_edge == MacroPinEdge::kEast || macro_pin_edge == MacroPinEdge::kWest;
    bool toward_low = false;
    if ((is_horizontal && is_horizontal_macro_edge) || (!is_horizontal && is_vertical_macro_edge)) {
      toward_low = macro_pin_edge == MacroPinEdge::kSouth || macro_pin_edge == MacroPinEdge::kWest;
    } else {
      PlanarRect gcell_rect = RTUTIL.getRealRectByGCell(gr_pin.grid_coord, gcell_axis);
      PlanarCoord gcell_mid_coord = gcell_rect.getMidPoint();
      PlanarCoord real_coord = pin->get_access_point().get_real_coord();
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
    GRWireEdgeId edge_id = getEscapeEdgeId(gr_pin.grid_coord.get_x(), gr_pin.grid_coord.get_y(), is_horizontal, direction, layer_idx);
    if (!grid.hasWireEdge(edge_id)) {
      continue;
    }
    exempt_edge_set_.insert(edge_id);

    if (pin->get_is_macro() || pin->get_is_pad()) {
      addPinEscapeCorridor(grid, gr_pin, is_horizontal, direction);
    }
  }
}

// macro body may still block the neighboring gcell, open at most one more gcell outward.
void GRNet::addPinEscapeCorridor(const GRGridGraph& grid, const GRPin& gr_pin, bool is_horizontal, int32_t direction)
{
  int32_t layer_idx = gr_pin.layer_idx;
  int32_t x = gr_pin.grid_coord.get_x();
  int32_t y = gr_pin.grid_coord.get_y();
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
  updateUsage(grid, params, tree, 1.0);
}

void GRNet::ripupUsage(GRGridGraph& grid, const GRParams& params, const GRTree& tree) const
{
  updateUsage(grid, params, tree, -1.0);
}

void GRNet::updateUsage(GRGridGraph& grid, const GRParams& params, const GRTree& tree, double sign) const
{
  std::set<GRWireEdgeId> planar_edge_set;
  buildPlanarEdgeSet(grid, tree, planar_edge_set);
  std::set<LayerCoord, CmpLayerCoordByXASC> occupied_coord_set;
  buildOccupiedCoordSet(grid, planar_edge_set, occupied_coord_set);
  std::vector<GRWireEdgeId> via_edge_list;
  buildViaEdgeList(grid, tree, occupied_coord_set, via_edge_list);
  for (const GRWireEdgeId& edge_id : planar_edge_set) {
    if (isExempt(edge_id)) {
      continue;
    }
    grid.addDemand(edge_id, sign, "wire edge");
  }
  for (const GRWireEdgeId& edge_id : via_edge_list) {
    grid.addDemand(edge_id, sign * params.via_demand, "via projection");
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

// coords occupied by planar edges and Ap
void GRNet::buildOccupiedCoordSet(const GRGridGraph& grid, const std::set<GRWireEdgeId>& planar_edge_set,
                                  std::set<LayerCoord, CmpLayerCoordByXASC>& occupied_coord_set) const
{
  for (const GRWireEdgeId& edge_id : planar_edge_set) {
    occupied_coord_set.insert(LayerCoord(edge_id.x, edge_id.y, edge_id.layer_idx));
    if (grid.isHorizontalLayer(edge_id.layer_idx)) {
      occupied_coord_set.insert(LayerCoord(edge_id.x + 1, edge_id.y, edge_id.layer_idx));
    } else {
      occupied_coord_set.insert(LayerCoord(edge_id.x, edge_id.y + 1, edge_id.layer_idx));
    }
  }
  for (const GRPin& gr_pin : pin_list_) {
    occupied_coord_set.insert(LayerCoord(gr_pin.grid_coord, gr_pin.layer_idx));
  }
}

// get edges add demand by via
void GRNet::buildViaEdgeList(const GRGridGraph& grid, const GRTree& tree,
                             const std::set<LayerCoord, CmpLayerCoordByXASC>& occupied_coord_set,
                             std::vector<GRWireEdgeId>& via_edge_list) const
{
  std::set<GRWireEdgeId> via_edge_set;
  for (const GRViaStack& via_stack : tree.via_list) {
    if (via_stack.bottom_layer_idx < grid.getBottomLayerIdx() || via_stack.top_layer_idx > grid.getTopLayerIdx()
        || via_stack.bottom_layer_idx >= via_stack.top_layer_idx) {
      continue;
    }
    for (int32_t layer_idx = via_stack.bottom_layer_idx; layer_idx <= via_stack.top_layer_idx; layer_idx++) {
      if (occupied_coord_set.contains(LayerCoord(via_stack.coord, layer_idx))) {
        continue;
      }
      for (GRViaEdgeIter iter(grid, via_stack.coord, layer_idx); iter.isValid(); iter.next()) {
        const GRWireEdgeId& edge_id = iter.getEdgeId();
        if (isExempt(edge_id) || !grid.hasUsableSupply(edge_id)) {
          continue;
        }
        if (via_edge_set.insert(edge_id).second) {
          via_edge_list.push_back(edge_id);
        }
      }
    }
  }
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
  return getOverflow(grid, tree_) > GRParams::kEpsilon;
}

double GRNet::getOverflow(const GRGridGraph& grid, const GRTree& tree) const
{
  std::set<GRWireEdgeId> planar_edge_set;
  buildPlanarEdgeSet(grid, tree, planar_edge_set);
  std::set<LayerCoord, CmpLayerCoordByXASC> occupied_coord_set;
  buildOccupiedCoordSet(grid, planar_edge_set, occupied_coord_set);
  std::vector<GRWireEdgeId> via_edge_list;
  buildViaEdgeList(grid, tree, occupied_coord_set, via_edge_list);
  double total_overflow = 0.0;
  for (const GRWireEdgeId& edge_id : planar_edge_set) {
    if (!grid.hasWireEdge(edge_id) || isExempt(edge_id)) {
      continue;
    }
    total_overflow += grid.getWireEdge(edge_id).getOverflow();
  }
  for (const GRWireEdgeId& edge_id : via_edge_list) {
    total_overflow += grid.getWireEdge(edge_id).getOverflow();
  }
  return total_overflow;
}

}  // namespace irt
