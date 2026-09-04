// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// ***************************************************************************************
#include <cassert>

#include "GSRDirtyWire.hpp"
#include "GSRGridGraph.hpp"
#include "GSRPin.hpp"
#include "RoutingEdge.hpp"

namespace irt {

void testPlanarDemandUsesCurrentRoutingEdge()
{
  GSRComParam com_param(0, 1, 0, 1);
  GSRGridGraph graph(2, 1, com_param);

  std::vector<GridMap<RoutingEdge>> h_edge_map_list(2);
  std::vector<GridMap<RoutingEdge>> v_edge_map_list(2);
  for (int32_t layer_idx = 0; layer_idx < 2; layer_idx++) {
    h_edge_map_list[layer_idx].init(1, 1);
    v_edge_map_list[layer_idx].init(2, 0);
  }
  RoutingEdge& routing_edge = h_edge_map_list[0][0][0];
  routing_edge.set_supply(1);
  routing_edge.get_ignore_net_set().insert(7);

  graph.initFromRoutingEdgeMap(h_edge_map_list, v_edge_map_list);
  std::vector<Segment<LayerCoord>> segment_list{Segment<LayerCoord>(LayerCoord(0, 0, 0), LayerCoord(1, 0, 0))};

  graph.updateDemandToGraph(ChangeType::kAdd, 1, segment_list);
  assert(routing_edge.get_demand() == 1);
  assert(routing_edge.get_demand_net_idx_list() == std::vector<int32_t>{1});
  assert(graph.getTotalOverflow() == 0);

  graph.updateDemandToGraph(ChangeType::kAdd, 1, segment_list);
  assert(routing_edge.get_demand() == 1);

  graph.updateDemandToGraph(ChangeType::kAdd, 7, segment_list);
  assert(routing_edge.get_demand() == 1);

  graph.updateDemandToGraph(ChangeType::kAdd, 2, segment_list);
  assert(routing_edge.get_demand() == 2);
  assert(graph.getTotalOverflow() == 1);
  assert(graph.getTouchedRouteOverflow(segment_list, segment_list) == 1);

  graph.updateDemandToGraph(ChangeType::kDel, 2, segment_list);

  graph.updateDemandToGraph(ChangeType::kDel, 1, segment_list);
  assert(routing_edge.get_demand() == 0);
  assert(routing_edge.get_demand_net_idx_list().empty());

  std::vector<Segment<LayerCoord>> invalid_segment_list{Segment<LayerCoord>(LayerCoord(-1, 0, 0), LayerCoord(1, 0, 0))};
  graph.updateDemandToGraph(ChangeType::kAdd, 3, invalid_segment_list);
  assert(routing_edge.get_demand() == 0);
}

void testPinAccessViaListConnectsOriginalLayer()
{
  GSRPin gsr_pin(0, "pin", LayerCoord(3, 4, 2), LayerCoord(3, 4, 0));
  std::vector<Segment<LayerCoord>> access_segment_list = gsr_pin.getAccessSegmentList();

  assert(access_segment_list.size() == 2);
  assert(access_segment_list[0].get_first() == LayerCoord(3, 4, 0));
  assert(access_segment_list[0].get_second() == LayerCoord(3, 4, 1));
  assert(access_segment_list[1].get_first() == LayerCoord(3, 4, 1));
  assert(access_segment_list[1].get_second() == LayerCoord(3, 4, 2));
}

void testDirtyWireLinesAreUniqueAndGrouped()
{
  std::vector<PlanarCoord> usage_coord_list{PlanarCoord(1, 1), PlanarCoord(1, 1), PlanarCoord(0, 0), PlanarCoord(2, 2)};
  std::vector<GSRDirtyWireLine> dirty_line_list = getGSRDirtyWireLineList(usage_coord_list, 3, 3);

  assert(dirty_line_list.size() == 6);
  assert(dirty_line_list[0].direction == Direction::kHorizontal);
  assert(dirty_line_list[0].line_idx == 0);
  assert(dirty_line_list[0].edge_idx_list == std::vector<int32_t>{0});
  assert(dirty_line_list[1].direction == Direction::kHorizontal);
  assert(dirty_line_list[1].line_idx == 1);
  assert(dirty_line_list[1].edge_idx_list == std::vector<int32_t>({0, 1}));
  assert(dirty_line_list[2].direction == Direction::kHorizontal);
  assert(dirty_line_list[2].line_idx == 2);
  assert(dirty_line_list[2].edge_idx_list == std::vector<int32_t>{1});
  assert(dirty_line_list[3].direction == Direction::kVertical);
  assert(dirty_line_list[3].line_idx == 0);
  assert(dirty_line_list[3].edge_idx_list == std::vector<int32_t>{0});
  assert(dirty_line_list[4].direction == Direction::kVertical);
  assert(dirty_line_list[4].line_idx == 1);
  assert(dirty_line_list[4].edge_idx_list == std::vector<int32_t>({0, 1}));
  assert(dirty_line_list[5].direction == Direction::kVertical);
  assert(dirty_line_list[5].line_idx == 2);
  assert(dirty_line_list[5].edge_idx_list == std::vector<int32_t>{1});
}

}  // namespace irt

int main()
{
  irt::testPlanarDemandUsesCurrentRoutingEdge();
  irt::testPinAccessViaListConnectsOriginalLayer();
  irt::testDirtyWireLinesAreUniqueAndGrouped();
  return 0;
}
