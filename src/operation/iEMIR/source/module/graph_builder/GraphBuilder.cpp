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
#include "GraphBuilder.hpp"

#include "DataManager.hpp"
#include "Logger.hpp"
#include "Monitor.hpp"
#include "PowerEdge.hpp"
#include "PowerEdgeType.hpp"
#include "PowerGraph.hpp"
#include "PowerNode.hpp"
#include "PowerNodeType.hpp"
#include "PowerPin.hpp"
#include "PowerVia.hpp"
#include "PowerWireSegment.hpp"

namespace iemir {

// public

void GraphBuilder::initInst()
{
  if (_gb_instance == nullptr) {
    _gb_instance = new GraphBuilder();
  }
}

GraphBuilder& GraphBuilder::getInst()
{
  if (_gb_instance == nullptr) {
    EMIRLOG.error(Loc::current(), "The instance not initialized!");
  }
  return *_gb_instance;
}

void GraphBuilder::destroyInst()
{
  if (_gb_instance != nullptr) {
    delete _gb_instance;
    _gb_instance = nullptr;
  }
}

// function

void GraphBuilder::build()
{
  Monitor monitor;
  EMIRLOG.info(Loc::current(), "Starting...");

  buildPowerGraphList();

  EMIRLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

// private

GraphBuilder* GraphBuilder::_gb_instance = nullptr;

GBModel GraphBuilder::initGBModel()
{
  GBModel gb_model;
  return gb_model;
}

void GraphBuilder::buildPowerGraphList()
{
  Database& database = EMIRDM.getDatabase();
  database.get_power_graph_map().clear();
  for (std::pair<const std::string, PowerNet>& power_net_pair : database.get_power_net_map()) {
    buildPowerGraph(power_net_pair.second);
  }
}

void GraphBuilder::buildPowerGraph(PowerNet& power_net)
{
  PowerGraph power_graph;
  GBModel gb_model = initGBModel();
  initPowerGraph(power_graph, power_net);
  buildWireNodeList(power_graph, power_net, gb_model);
  buildWireIntersectionNodeList(power_graph, power_net, gb_model);
  buildViaNodeList(power_graph, power_net, gb_model);
  buildPinNodeList(power_graph, power_net, gb_model);
  buildGeneratedSourceNodeList(power_graph);
  buildWireEdgeList(power_graph, power_net, gb_model);
  buildViaEdgeList(power_graph, power_net);
  checkPowerGraphConnectivity(power_graph);
  EMIRDM.getDatabase().get_power_graph_map()[power_graph.get_net_name()] = power_graph;
}

void GraphBuilder::initPowerGraph(PowerGraph& power_graph, PowerNet& power_net)
{
  power_graph.set_net_name(power_net.get_net_name());
  power_graph.set_net_type(power_net.get_type());
}

void GraphBuilder::buildWireNodeList(PowerGraph& power_graph, PowerNet& power_net, GBModel& gb_model)
{
  std::vector<PowerWireSegment>& power_wire_segment_list = power_net.get_wire_segment_list();
  for (std::size_t segment_idx = 0; segment_idx < power_wire_segment_list.size(); segment_idx++) {
    buildWireEndpointNode(power_graph, power_wire_segment_list[segment_idx], segment_idx, gb_model);
  }
}

void GraphBuilder::buildWireEndpointNode(PowerGraph& power_graph, PowerWireSegment& power_wire_segment, std::size_t segment_idx, GBModel& gb_model)
{
  std::size_t first_node_id
      = getPowerNode(power_graph, power_wire_segment.get_layer_idx(), power_wire_segment.get_first_x(), power_wire_segment.get_first_y(), PowerNodeType::kWire);
  std::size_t second_node_id = getPowerNode(power_graph, power_wire_segment.get_layer_idx(), power_wire_segment.get_second_x(),
                                            power_wire_segment.get_second_y(), PowerNodeType::kWire);
  appendWireNodeId(gb_model, segment_idx, first_node_id);
  appendWireNodeId(gb_model, segment_idx, second_node_id);
}

std::size_t GraphBuilder::getPowerNode(PowerGraph& power_graph, int32_t layer_idx, int32_t x, int32_t y, PowerNodeType power_node_type)
{
  std::vector<PowerNode>& power_node_list = power_graph.get_node_list();
  for (PowerNode& power_node : power_node_list) {
    if (power_node.get_layer_idx() == layer_idx && power_node.get_x() == x && power_node.get_y() == y) {
      if (power_node_type == PowerNodeType::kSource) {
        power_node.set_type(PowerNodeType::kSource);
        power_node.set_is_source(true);
      } else if (power_node.get_type() == PowerNodeType::kWire && power_node_type != PowerNodeType::kWire) {
        power_node.set_type(power_node_type);
      }
      return power_node.get_node_id();
    }
  }
  PowerNode power_node;
  power_node.set_node_id(power_node_list.size());
  power_node.set_type(power_node_type);
  power_node.set_layer_idx(layer_idx);
  power_node.set_x(x);
  power_node.set_y(y);
  power_node.set_is_source(power_node_type == PowerNodeType::kSource);
  power_node_list.push_back(power_node);
  return power_node.get_node_id();
}

void GraphBuilder::buildWireIntersectionNodeList(PowerGraph& power_graph, PowerNet& power_net, GBModel& gb_model)
{
  std::vector<PowerWireSegment>& power_wire_segment_list = power_net.get_wire_segment_list();
  for (std::size_t first_segment_idx = 0; first_segment_idx < power_wire_segment_list.size(); first_segment_idx++) {
    for (std::size_t second_segment_idx = first_segment_idx + 1; second_segment_idx < power_wire_segment_list.size(); second_segment_idx++) {
      buildWireIntersectionNode(power_graph, power_wire_segment_list[first_segment_idx], power_wire_segment_list[second_segment_idx], first_segment_idx,
                                second_segment_idx, gb_model);
    }
  }
}

void GraphBuilder::buildWireIntersectionNode(PowerGraph& power_graph, PowerWireSegment& first_power_wire_segment, PowerWireSegment& second_power_wire_segment,
                                             std::size_t first_segment_idx, std::size_t second_segment_idx, GBModel& gb_model)
{
  if (first_power_wire_segment.get_layer_idx() != second_power_wire_segment.get_layer_idx()) {
    return;
  }
  appendWireCoordinateNode(power_graph, first_power_wire_segment, second_power_wire_segment, first_segment_idx, second_segment_idx,
                           first_power_wire_segment.get_first_x(), first_power_wire_segment.get_first_y(), gb_model);
  appendWireCoordinateNode(power_graph, first_power_wire_segment, second_power_wire_segment, first_segment_idx, second_segment_idx,
                           first_power_wire_segment.get_second_x(), first_power_wire_segment.get_second_y(), gb_model);
  appendWireCoordinateNode(power_graph, first_power_wire_segment, second_power_wire_segment, first_segment_idx, second_segment_idx,
                           second_power_wire_segment.get_first_x(), second_power_wire_segment.get_first_y(), gb_model);
  appendWireCoordinateNode(power_graph, first_power_wire_segment, second_power_wire_segment, first_segment_idx, second_segment_idx,
                           second_power_wire_segment.get_second_x(), second_power_wire_segment.get_second_y(), gb_model);
  int32_t x = 0;
  int32_t y = 0;
  if (!getWireIntersectionCoordinate(first_power_wire_segment, second_power_wire_segment, x, y)) {
    return;
  }
  appendWireCoordinateNode(power_graph, first_power_wire_segment, second_power_wire_segment, first_segment_idx, second_segment_idx, x, y, gb_model);
}

void GraphBuilder::appendWireCoordinateNode(PowerGraph& power_graph, PowerWireSegment& first_power_wire_segment, PowerWireSegment& second_power_wire_segment,
                                            std::size_t first_segment_idx, std::size_t second_segment_idx, int32_t x, int32_t y, GBModel& gb_model)
{
  if (!isOnWireSegment(first_power_wire_segment, x, y) || !isOnWireSegment(second_power_wire_segment, x, y)) {
    return;
  }
  std::size_t node_id = getPowerNode(power_graph, first_power_wire_segment.get_layer_idx(), x, y, PowerNodeType::kWire);
  appendWireNodeId(gb_model, first_segment_idx, node_id);
  appendWireNodeId(gb_model, second_segment_idx, node_id);
}

bool GraphBuilder::getWireIntersectionCoordinate(PowerWireSegment& first_power_wire_segment, PowerWireSegment& second_power_wire_segment, int32_t& x,
                                                 int32_t& y)
{
  bool first_is_horizontal = first_power_wire_segment.get_first_y() == first_power_wire_segment.get_second_y();
  bool first_is_vertical = first_power_wire_segment.get_first_x() == first_power_wire_segment.get_second_x();
  bool second_is_horizontal = second_power_wire_segment.get_first_y() == second_power_wire_segment.get_second_y();
  bool second_is_vertical = second_power_wire_segment.get_first_x() == second_power_wire_segment.get_second_x();
  if (!((first_is_horizontal || first_is_vertical) && (second_is_horizontal || second_is_vertical))) {
    EMIRLOG.error(Loc::current(), "The power wire segment is oblique!");
  }
  if (first_is_horizontal && second_is_vertical) {
    x = second_power_wire_segment.get_first_x();
    y = first_power_wire_segment.get_first_y();
  } else if (first_is_vertical && second_is_horizontal) {
    x = first_power_wire_segment.get_first_x();
    y = second_power_wire_segment.get_first_y();
  } else {
    return false;
  }
  int32_t first_min_x = std::min(first_power_wire_segment.get_first_x(), first_power_wire_segment.get_second_x());
  int32_t first_max_x = std::max(first_power_wire_segment.get_first_x(), first_power_wire_segment.get_second_x());
  int32_t first_min_y = std::min(first_power_wire_segment.get_first_y(), first_power_wire_segment.get_second_y());
  int32_t first_max_y = std::max(first_power_wire_segment.get_first_y(), first_power_wire_segment.get_second_y());
  int32_t second_min_x = std::min(second_power_wire_segment.get_first_x(), second_power_wire_segment.get_second_x());
  int32_t second_max_x = std::max(second_power_wire_segment.get_first_x(), second_power_wire_segment.get_second_x());
  int32_t second_min_y = std::min(second_power_wire_segment.get_first_y(), second_power_wire_segment.get_second_y());
  int32_t second_max_y = std::max(second_power_wire_segment.get_first_y(), second_power_wire_segment.get_second_y());
  return first_min_x <= x && x <= first_max_x && first_min_y <= y && y <= first_max_y && second_min_x <= x && x <= second_max_x && second_min_y <= y
         && y <= second_max_y;
}

void GraphBuilder::appendWireNodeId(GBModel& gb_model, std::size_t segment_idx, std::size_t node_id)
{
  std::vector<std::size_t>& node_id_list = gb_model.get_segment_node_id_list_map()[segment_idx];
  if (std::find(node_id_list.begin(), node_id_list.end(), node_id) == node_id_list.end()) {
    node_id_list.push_back(node_id);
  }
}

void GraphBuilder::buildViaNodeList(PowerGraph& power_graph, PowerNet& power_net, GBModel& gb_model)
{
  for (PowerVia& power_via : power_net.get_via_list()) {
    std::size_t bottom_node_id = getPowerNode(power_graph, power_via.get_bottom_layer_idx(), power_via.get_x(), power_via.get_y(), PowerNodeType::kVia);
    std::size_t top_node_id = getPowerNode(power_graph, power_via.get_top_layer_idx(), power_via.get_x(), power_via.get_y(), PowerNodeType::kVia);
    for (std::size_t segment_idx = 0; segment_idx < power_net.get_wire_segment_list().size(); segment_idx++) {
      PowerWireSegment& power_wire_segment = power_net.get_wire_segment_list()[segment_idx];
      if (power_wire_segment.get_layer_idx() == power_via.get_bottom_layer_idx()
          && isOnWireSegment(power_wire_segment, power_graph.get_node_list()[bottom_node_id])) {
        appendWireNodeId(gb_model, segment_idx, bottom_node_id);
      }
      if (power_wire_segment.get_layer_idx() == power_via.get_top_layer_idx()
          && isOnWireSegment(power_wire_segment, power_graph.get_node_list()[top_node_id])) {
        appendWireNodeId(gb_model, segment_idx, top_node_id);
      }
    }
  }
}

void GraphBuilder::buildPinNodeList(PowerGraph& power_graph, PowerNet& power_net, GBModel& gb_model)
{
  for (PowerPin& power_pin : power_net.get_pin_list()) {
    PowerNodeType power_node_type = power_pin.get_is_source() ? PowerNodeType::kSource : PowerNodeType::kInstancePin;
    std::size_t node_id = getPowerNode(power_graph, power_pin.get_layer_idx(), power_pin.get_x(), power_pin.get_y(), power_node_type);
    PowerNode& power_node = power_graph.get_node_list()[node_id];
    if (power_pin.get_is_source()) {
      std::vector<std::size_t>& source_node_id_list = power_graph.get_source_node_id_list();
      if (std::find(source_node_id_list.begin(), source_node_id_list.end(), node_id) == source_node_id_list.end()) {
        source_node_id_list.push_back(node_id);
      }
    } else {
      power_node.get_instance_id_set().insert(power_pin.get_instance_id());
      std::vector<std::size_t>& node_id_list = power_graph.get_instance_node_id_list_map()[power_pin.get_instance_id()];
      if (std::find(node_id_list.begin(), node_id_list.end(), node_id) == node_id_list.end()) {
        node_id_list.push_back(node_id);
      }
    }
    for (std::size_t segment_idx = 0; segment_idx < power_net.get_wire_segment_list().size(); segment_idx++) {
      PowerWireSegment& power_wire_segment = power_net.get_wire_segment_list()[segment_idx];
      if (power_wire_segment.get_layer_idx() == power_pin.get_layer_idx() && isOnWireSegment(power_wire_segment, power_node)) {
        appendWireNodeId(gb_model, segment_idx, node_id);
      }
    }
  }
}

void GraphBuilder::buildGeneratedSourceNodeList(PowerGraph& power_graph)
{
  if (!power_graph.get_source_node_id_list().empty()) {
    return;
  }
  buildFullSourceNodeList(power_graph);
}

void GraphBuilder::buildFullSourceNodeList(PowerGraph& power_graph)
{
  int32_t top_layer_idx = -1;
  for (PowerNode& power_node : power_graph.get_node_list()) {
    top_layer_idx = std::max(top_layer_idx, power_node.get_layer_idx());
  }
  for (PowerNode& power_node : power_graph.get_node_list()) {
    if (power_node.get_layer_idx() != top_layer_idx) {
      continue;
    }
    power_node.set_type(PowerNodeType::kSource);
    power_node.set_is_source(true);
    power_graph.get_source_node_id_list().push_back(power_node.get_node_id());
  }
}

bool GraphBuilder::isOnWireSegment(PowerWireSegment& power_wire_segment, int32_t x, int32_t y)
{
  int32_t half_width = power_wire_segment.get_width() / 2;
  int32_t min_x = std::min(power_wire_segment.get_first_x(), power_wire_segment.get_second_x());
  int32_t max_x = std::max(power_wire_segment.get_first_x(), power_wire_segment.get_second_x());
  int32_t min_y = std::min(power_wire_segment.get_first_y(), power_wire_segment.get_second_y());
  int32_t max_y = std::max(power_wire_segment.get_first_y(), power_wire_segment.get_second_y());
  if (power_wire_segment.get_first_y() == power_wire_segment.get_second_y()) {
    return min_x <= x && x <= max_x && std::abs(y - power_wire_segment.get_first_y()) <= half_width;
  }
  if (power_wire_segment.get_first_x() == power_wire_segment.get_second_x()) {
    return min_y <= y && y <= max_y && std::abs(x - power_wire_segment.get_first_x()) <= half_width;
  }
  EMIRLOG.error(Loc::current(), "The power wire segment is oblique!");
  return false;
}

bool GraphBuilder::isOnWireSegment(PowerWireSegment& power_wire_segment, PowerNode& power_node)
{
  return isOnWireSegment(power_wire_segment, power_node.get_x(), power_node.get_y());
}

void GraphBuilder::buildWireEdgeList(PowerGraph& power_graph, PowerNet& power_net, GBModel& gb_model)
{
  std::vector<PowerWireSegment>& power_wire_segment_list = power_net.get_wire_segment_list();
  for (std::size_t segment_idx = 0; segment_idx < power_wire_segment_list.size(); segment_idx++) {
    PowerWireSegment& power_wire_segment = power_wire_segment_list[segment_idx];
    std::vector<std::size_t>& node_id_list = gb_model.get_segment_node_id_list_map()[segment_idx];
    bool is_horizontal = power_wire_segment.get_first_y() == power_wire_segment.get_second_y();
    std::sort(node_id_list.begin(), node_id_list.end(), [&power_graph, is_horizontal](std::size_t first_node_id, std::size_t second_node_id) {
      PowerNode& first_power_node = power_graph.get_node_list()[first_node_id];
      PowerNode& second_power_node = power_graph.get_node_list()[second_node_id];
      return is_horizontal ? first_power_node.get_x() < second_power_node.get_x() : first_power_node.get_y() < second_power_node.get_y();
    });
    for (std::size_t node_idx = 1; node_idx < node_id_list.size(); node_idx++) {
      PowerNode& first_power_node = power_graph.get_node_list()[node_id_list[node_idx - 1]];
      PowerNode& second_power_node = power_graph.get_node_list()[node_id_list[node_idx]];
      int32_t length = std::abs(first_power_node.get_x() - second_power_node.get_x()) + std::abs(first_power_node.get_y() - second_power_node.get_y());
      if (length == 0) {
        continue;
      }
      if (power_wire_segment.get_width() <= 0 || power_wire_segment.get_resistance_per_square() <= 0.0) {
        EMIRLOG.error(Loc::current(), "The power wire resistance data is invalid!");
      }
      double resistance = power_wire_segment.get_resistance_per_square() * static_cast<double>(length) / power_wire_segment.get_width();
      addPowerEdge(power_graph, PowerEdgeType::kWire, first_power_node.get_node_id(), second_power_node.get_node_id(), power_wire_segment.get_layer_idx(),
                   power_wire_segment.get_width(), length, resistance);
    }
  }
}

void GraphBuilder::buildViaEdgeList(PowerGraph& power_graph, PowerNet& power_net)
{
  for (PowerVia& power_via : power_net.get_via_list()) {
    std::size_t bottom_node_id = getPowerNode(power_graph, power_via.get_bottom_layer_idx(), power_via.get_x(), power_via.get_y(), PowerNodeType::kVia);
    std::size_t top_node_id = getPowerNode(power_graph, power_via.get_top_layer_idx(), power_via.get_x(), power_via.get_y(), PowerNodeType::kVia);
    if (power_via.get_resistance() <= 0.0) {
      EMIRLOG.error(Loc::current(), "The power via resistance data is invalid!");
    }
    addPowerEdge(power_graph, PowerEdgeType::kVia, bottom_node_id, top_node_id, power_via.get_bottom_layer_idx(), 0, 0, power_via.get_resistance());
  }
}

void GraphBuilder::addPowerEdge(PowerGraph& power_graph, PowerEdgeType power_edge_type, std::size_t first_node_id, std::size_t second_node_id,
                                int32_t layer_idx, int32_t width, int32_t length, double resistance)
{
  if (first_node_id == second_node_id) {
    return;
  }
  for (PowerEdge& power_edge : power_graph.get_edge_list()) {
    bool is_same_node_pair = (power_edge.get_first_node_id() == first_node_id && power_edge.get_second_node_id() == second_node_id)
                             || (power_edge.get_first_node_id() == second_node_id && power_edge.get_second_node_id() == first_node_id);
    if (is_same_node_pair && power_edge.get_type() == power_edge_type) {
      return;
    }
  }
  PowerEdge power_edge;
  power_edge.set_edge_id(power_graph.get_edge_list().size());
  power_edge.set_type(power_edge_type);
  power_edge.set_first_node_id(first_node_id);
  power_edge.set_second_node_id(second_node_id);
  power_edge.set_layer_idx(layer_idx);
  power_edge.set_width(width);
  power_edge.set_length(length);
  power_edge.set_resistance(resistance);
  power_graph.get_edge_list().push_back(power_edge);
}

void GraphBuilder::checkPowerGraphConnectivity(PowerGraph& power_graph)
{
  if (power_graph.get_source_node_id_list().empty()) {
    EMIRLOG.error(Loc::current(), "The power graph does not contain a source node!");
  }
  std::map<std::size_t, std::vector<std::size_t>> node_adjacency_map;
  for (PowerEdge& power_edge : power_graph.get_edge_list()) {
    node_adjacency_map[power_edge.get_first_node_id()].push_back(power_edge.get_second_node_id());
    node_adjacency_map[power_edge.get_second_node_id()].push_back(power_edge.get_first_node_id());
  }
  std::queue<std::size_t> node_id_queue;
  std::set<std::size_t> visited_node_id_set;
  for (std::size_t source_node_id : power_graph.get_source_node_id_list()) {
    node_id_queue.push(source_node_id);
  }
  while (!node_id_queue.empty()) {
    std::size_t node_id = node_id_queue.front();
    node_id_queue.pop();
    if (visited_node_id_set.count(node_id) != 0) {
      continue;
    }
    visited_node_id_set.insert(node_id);
    for (std::size_t adjacent_node_id : node_adjacency_map[node_id]) {
      if (visited_node_id_set.count(adjacent_node_id) == 0) {
        node_id_queue.push(adjacent_node_id);
      }
    }
  }
  if (visited_node_id_set.size() != power_graph.get_node_list().size()) {
    EMIRLOG.error(Loc::current(), "The power graph is disconnected!");
  }
  power_graph.set_is_connected(true);
}

}  // namespace iemir
