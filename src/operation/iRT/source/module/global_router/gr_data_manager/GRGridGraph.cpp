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
#include "GRGridGraph.hpp"

#include "RTHeader.hpp"
#include "RoutingLayer.hpp"

namespace irt {

void GRGridGraph::build(Database& database, int32_t bottom_layer_idx, int32_t top_layer_idx)
{
  _layer_num = static_cast<int32_t>(database.get_routing_layer_list().size());
  _bottom_layer_idx = bottom_layer_idx;
  _top_layer_idx = top_layer_idx;
  _x_size = database.get_gcell_map().get_x_size();
  _y_size = database.get_gcell_map().get_y_size();

  _is_horizontal_layer.assign(_layer_num, false);
  for (int32_t layer_idx = 0; layer_idx < _layer_num; layer_idx++) {
    _is_horizontal_layer[layer_idx] = database.get_routing_layer_list()[layer_idx].isPreferH();
  }

  _edge_map.resize(_layer_num);
  for (int32_t layer_idx = 0; layer_idx < _layer_num; layer_idx++) {
    if (_is_horizontal_layer[layer_idx]) {
      _edge_map[layer_idx].init(std::max(0, _x_size - 1), _y_size);
    } else {
      _edge_map[layer_idx].init(_x_size, std::max(0, _y_size - 1));
    }
  }
}

bool GRGridGraph::hasWireEdge(const GRWireEdgeId& edge_id) const
{
  if (edge_id.layer_idx < 0 || edge_id.layer_idx >= _layer_num) {
    return false;
  }
  const GridMap<GRWireEdge>& edge_map = _edge_map[edge_id.layer_idx];
  return 0 <= edge_id.x && edge_id.x < edge_map.get_x_size() && 0 <= edge_id.y && edge_id.y < edge_map.get_y_size();
}

const GRWireEdge& GRGridGraph::getWireEdge(const GRWireEdgeId& edge_id) const
{
  return _edge_map[edge_id.layer_idx][edge_id.x][edge_id.y];
}

GRWireEdge& GRGridGraph::getWireEdge(const GRWireEdgeId& edge_id)
{
  return _edge_map[edge_id.layer_idx][edge_id.x][edge_id.y];
}

}  // namespace irt
