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
#include "TopoBuilder.hpp"

#include "LayerShape.hpp"
#include "Utility.hpp"

namespace ircx {

// public

void TopoBuilder::initInst()
{
  if (_tb_instance == nullptr) {
    _tb_instance = new TopoBuilder();
  }
}

TopoBuilder& TopoBuilder::getInst()
{
  if (_tb_instance == nullptr) {
    RCXLOG.error(Loc::current(), "The instance not initialized!");
  }
  return *_tb_instance;
}

void TopoBuilder::destroyInst()
{
  if (_tb_instance != nullptr) {
    delete _tb_instance;
    _tb_instance = nullptr;
  }
}

// function

void TopoBuilder::build()
{
  Monitor monitor;
  RCXLOG.info(Loc::current(), "Starting...");

  buildRegularNetTopoList();
  buildSpecialEdgeList();

  RCXLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

// private

TopoBuilder* TopoBuilder::_tb_instance = nullptr;

void TopoBuilder::buildRegularNetTopoList()
{
  LayoutData& layout_data = RCXDM.getDatabase().get_layout_data();
  std::vector<Net>& net_list = layout_data.get_net_list();
  int32_t net_num = layout_data.get_regular_net_num();
  if (net_num == 0) {
    return;
  }

  std::vector<TBTopo> net_topo_list(net_num);
  int32_t thread_num = RCXUTIL.getThreadNum(net_num, RCXDM.getConfig().thread_number);
#pragma omp parallel for schedule(dynamic) num_threads(thread_num)
  for (int32_t net_idx = 0; net_idx < net_num; ++net_idx) {
    net_topo_list[net_idx] = buildNetTopo(net_list[net_idx]);
  }

  int32_t node_num = 0;
  int32_t edge_num = 0;
  for (TBTopo& net_topo : net_topo_list) {
    node_num += static_cast<int32_t>(net_topo.get_node_list().size());
    edge_num += static_cast<int32_t>(net_topo.get_edge_list().size());
  }

  TopoPool& topo_pool = RCXDM.getDatabase().get_topo_pool();
  topo_pool.reserve(net_num, node_num, edge_num);
  for (TBTopo& net_topo : net_topo_list) {
    topo_pool.add_net(std::move(net_topo.get_node_list()), std::move(net_topo.get_edge_list()));
  }

#pragma omp parallel for schedule(dynamic) num_threads(thread_num)
  for (int32_t net_idx = 0; net_idx < net_num; ++net_idx) {
    std::span<TopoEdge> edge_list = topo_pool.get_net_edge_list(net_idx);
    for (TopoEdge& edge : edge_list) {
      edge.set_start_node_idx(topo_pool.get_node_idx(net_idx, edge.get_start_node_idx()));
      edge.set_end_node_idx(topo_pool.get_node_idx(net_idx, edge.get_end_node_idx()));
    }
  }
}

TBTopo TopoBuilder::buildNetTopo(Net& net)
{
  TBTopo net_topo;
  std::vector<TopoNode>& node_list = net_topo.get_node_list();
  std::vector<TopoEdge>& edge_list = net_topo.get_edge_list();
  std::map<TBNodeKey, int32_t> node_key_to_idx_map;
  std::set<std::string> consumed_pin_name_set;

  for (Segment& segment : net.get_segment_list()) {
    appendNodeIfAbsent(net, node_list, node_key_to_idx_map, consumed_pin_name_set, segment.get_layer_idx(), segment.get_start_point());
    appendNodeIfAbsent(net, node_list, node_key_to_idx_map, consumed_pin_name_set, segment.get_layer_idx(), segment.get_end_point());
  }
  for (Via& via : net.get_via_list()) {
    appendNodeIfAbsent(net, node_list, node_key_to_idx_map, consumed_pin_name_set, via.get_top_layer_shape().get_layer_idx(), via.get_point());
    appendNodeIfAbsent(net, node_list, node_key_to_idx_map, consumed_pin_name_set, via.get_bottom_layer_shape().get_layer_idx(), via.get_point());
  }

  for (Segment& segment : net.get_segment_list()) {
    int32_t layer_idx = segment.get_layer_idx();
    int32_t start_node_idx = node_key_to_idx_map[TBNodeKey(layer_idx, RCXUTIL.x(segment.get_start_point()), RCXUTIL.y(segment.get_start_point()))];
    int32_t end_node_idx = node_key_to_idx_map[TBNodeKey(layer_idx, RCXUTIL.x(segment.get_end_point()), RCXUTIL.y(segment.get_end_point()))];
    mergeNodeShape(node_list, start_node_idx, getSegmentEndpointShape(segment, segment.get_start_point()));
    mergeNodeShape(node_list, end_node_idx, getSegmentEndpointShape(segment, segment.get_end_point()));

    TopoEdge edge(net.get_net_idx());
    edge.set_layer_idx(layer_idx);
    if (RCXUTIL.x(segment.get_start_point()) < RCXUTIL.x(segment.get_end_point())
        || (RCXUTIL.x(segment.get_start_point()) == RCXUTIL.x(segment.get_end_point())
            && RCXUTIL.y(segment.get_start_point()) <= RCXUTIL.y(segment.get_end_point()))) {
      edge.set_start_node_idx(start_node_idx);
      edge.set_end_node_idx(end_node_idx);
    } else {
      edge.set_start_node_idx(end_node_idx);
      edge.set_end_node_idx(start_node_idx);
    }
    edge.set_shape(segment.get_shape());
    edge_list.push_back(std::move(edge));
  }

  for (Via& via : net.get_via_list()) {
    int32_t top_layer_idx = via.get_top_layer_shape().get_layer_idx();
    int32_t bottom_layer_idx = via.get_bottom_layer_shape().get_layer_idx();
    int32_t top_node_idx = node_key_to_idx_map[TBNodeKey(top_layer_idx, RCXUTIL.x(via.get_point()), RCXUTIL.y(via.get_point()))];
    int32_t bottom_node_idx = node_key_to_idx_map[TBNodeKey(bottom_layer_idx, RCXUTIL.x(via.get_point()), RCXUTIL.y(via.get_point()))];
    mergeNodeShape(node_list, top_node_idx, via.get_top_layer_shape().get_shape());
    mergeNodeShape(node_list, bottom_node_idx, via.get_bottom_layer_shape().get_shape());

    TopoEdge edge(net.get_net_idx());
    edge.set_layer_idx(via.get_cut_layer_shape().get_layer_idx());
    edge.set_start_node_idx(top_node_idx);
    edge.set_end_node_idx(bottom_node_idx);
    edge.set_shape(via.get_cut_layer_shape().get_shape());
    edge.set_via_name(via.get_via_name());
    edge_list.push_back(std::move(edge));
  }
  return net_topo;
}

void TopoBuilder::appendNodeIfAbsent(Net& net, std::vector<TopoNode>& node_list, std::map<TBNodeKey, int32_t>& node_key_to_idx_map,
                                     std::set<std::string>& consumed_pin_name_set, int32_t layer_idx, const GTLPointInt& point)
{
  TBNodeKey node_key(layer_idx, RCXUTIL.x(point), RCXUTIL.y(point));
  if (node_key_to_idx_map.count(node_key) != 0) {
    return;
  }

  TopoNode node(net.get_net_idx());
  node.set_layer_idx(layer_idx);
  node.set_point(point);

  bool is_shape_valid = false;
  for (Pin& pin : net.get_pin_list()) {
    if (consumed_pin_name_set.count(pin.get_pin_name()) != 0) {
      continue;
    }
    for (LayerShape& layer_shape : pin.get_layer_shape_list()) {
      if (layer_shape.get_layer_idx() != layer_idx) {
        continue;
      }

      GTLRectInt& pin_shape = layer_shape.get_shape();
      if (RCXUTIL.rectContainsPoint(pin_shape, point)) {
        consumed_pin_name_set.insert(pin.get_pin_name());
        node.set_pin_name(pin.get_pin_name());
        node.set_shape(pin_shape);
        node.set_is_shape_valid(true);
        is_shape_valid = true;
        break;
      }
    }
    if (is_shape_valid) {
      break;
    }
  }
  if (!is_shape_valid) {
    node.set_shape(GTLRectInt(RCXUTIL.x(point) - 1, RCXUTIL.y(point) - 1, RCXUTIL.x(point) + 1, RCXUTIL.y(point) + 1));
  }

  node_key_to_idx_map[node_key] = appendNode(node_list, std::move(node));
}

int32_t TopoBuilder::appendNode(std::vector<TopoNode>& node_list, TopoNode node)
{
  node_list.push_back(std::move(node));
  return static_cast<int32_t>(node_list.size()) - 1;
}

void TopoBuilder::mergeNodeShape(std::vector<TopoNode>& node_list, int32_t node_idx, const GTLRectInt& shape)
{
  TopoNode& node = node_list[node_idx];
  if (!node.get_is_shape_valid()) {
    node.set_shape(shape);
    node.set_is_shape_valid(true);
    return;
  }

  GTLRectInt& old_shape = node.get_shape();
  node.set_shape(RCXUTIL.getBoundingRect(old_shape, shape));
}

GTLRectInt TopoBuilder::getSegmentEndpointShape(Segment& segment, const GTLPointInt& point)
{
  bool is_horizontal = RCXUTIL.isHorizontalDominant(segment.get_start_point(), segment.get_end_point());
  GTLRectInt& shape = segment.get_shape();
  if (is_horizontal) {
    return GTLRectInt(RCXUTIL.x(point), RCXUTIL.minY(shape), RCXUTIL.x(point), RCXUTIL.maxY(shape));
  }
  return GTLRectInt(RCXUTIL.minX(shape), RCXUTIL.y(point), RCXUTIL.maxX(shape), RCXUTIL.y(point));
}

void TopoBuilder::buildSpecialEdgeList()
{
  Net& special_net = RCXDM.getDatabase().get_layout_data().get_special_net();
  std::vector<TopoEdge> special_edge_list;
  special_edge_list.reserve(special_net.get_segment_list().size() + special_net.get_patch_list().size());

  for (Segment& segment : special_net.get_segment_list()) {
    TopoEdge edge;
    edge.set_layer_idx(segment.get_layer_idx());
    edge.set_shape(segment.get_shape());
    special_edge_list.push_back(std::move(edge));
  }
  for (Patch& patch : special_net.get_patch_list()) {
    TopoEdge edge;
    edge.set_layer_idx(patch.get_layer_idx());
    edge.set_shape(patch.get_shape());
    special_edge_list.push_back(std::move(edge));
  }
  RCXDM.getDatabase().get_topo_pool().add_special_edge_list(std::move(special_edge_list));
}

}  // namespace ircx
