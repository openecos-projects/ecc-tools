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

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <boost/geometry.hpp>
#include <boost/geometry/geometries/box.hpp>
#include <boost/geometry/geometries/point_xy.hpp>
#include <boost/geometry/index/predicates.hpp>
#include <boost/geometry/index/rtree.hpp>

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
namespace {

struct PinPortKey
{
  bool is_source = false;
  uint64_t instance_id = 0;
  std::string pin_name;
  uint64_t port_id = 0;

  [[nodiscard]] bool operator<(const PinPortKey& other) const
  {
    return std::tie(is_source, instance_id, pin_name, port_id) < std::tie(other.is_source, other.instance_id, other.pin_name, other.port_id);
  }
};

struct PendingPinPort
{
  PowerPin* fallback_pin = nullptr;
  std::vector<std::size_t> node_ids;
};

void appendUniqueNodeId(std::vector<std::size_t>& node_ids, const std::size_t node_id)
{
  if (std::find(node_ids.begin(), node_ids.end(), node_id) == node_ids.end()) {
    node_ids.push_back(node_id);
  }
}

namespace bg = boost::geometry;
namespace bgi = boost::geometry::index;

using ViaContactPoint = bg::model::d2::point_xy<int32_t>;
using ViaContactBox = bg::model::box<ViaContactPoint>;
using ViaContactIndexValue = std::pair<ViaContactBox, std::size_t>;
using ViaContactIndex = bgi::rtree<ViaContactIndexValue, bgi::quadratic<16>>;

[[nodiscard]] ViaContactBox toViaContactBox(const PowerViaMetalRect& rectangle)
{
  return {{rectangle.ll_x, rectangle.ll_y}, {rectangle.ur_x, rectangle.ur_y}};
}

[[nodiscard]] ViaContactBox toViaContactBox(PowerPin& power_pin)
{
  return {{power_pin.get_ll_x(), power_pin.get_ll_y()}, {power_pin.get_ur_x(), power_pin.get_ur_y()}};
}

class ViaContactIndexByLayer
{
 public:
  void add(int32_t layer_idx, const PowerViaMetalRect& rectangle, std::size_t node_id)
  {
    _value_map[layer_idx].emplace_back(toViaContactBox(rectangle), node_id);
  }

  void build()
  {
    for (auto& [layer_idx, values] : _value_map) {
      _rtree_map[layer_idx] = ViaContactIndex(values.begin(), values.end());
    }
  }

  void appendContactNodeIds(PowerPin& power_pin, std::vector<std::size_t>& node_ids) const
  {
    if (!power_pin.has_bounds()) {
      return;
    }
    const auto rtree_iter = _rtree_map.find(power_pin.get_layer_idx());
    if (rtree_iter == _rtree_map.end()) {
      return;
    }
    const ViaContactBox pin_box = toViaContactBox(power_pin);
    for (auto query_iter = rtree_iter->second.qbegin(bgi::intersects(pin_box)); query_iter != rtree_iter->second.qend(); ++query_iter) {
      appendUniqueNodeId(node_ids, query_iter->second);
    }
  }

 private:
  std::map<int32_t, std::vector<ViaContactIndexValue>> _value_map;
  std::map<int32_t, ViaContactIndex> _rtree_map;
};

[[nodiscard]] ViaContactIndexByLayer buildViaContactIndex(PowerGraph& power_graph, PowerNet& power_net)
{
  std::map<std::tuple<int32_t, int32_t, int32_t>, std::size_t> node_id_map;
  for (PowerNode& node : power_graph.get_node_list()) {
    node_id_map.emplace(std::make_tuple(node.get_layer_idx(), node.get_x(), node.get_y()), node.get_node_id());
  }

  ViaContactIndexByLayer result;
  for (PowerVia& power_via : power_net.get_via_list()) {
    const std::size_t bottom_node_id
        = node_id_map.at(std::make_tuple(power_via.get_bottom_layer_idx(), power_via.get_x(), power_via.get_y()));
    for (const PowerViaMetalRect& rectangle : power_via.get_bottom_rect_list()) {
      result.add(power_via.get_bottom_layer_idx(), rectangle, bottom_node_id);
    }
    const std::size_t top_node_id = node_id_map.at(std::make_tuple(power_via.get_top_layer_idx(), power_via.get_x(), power_via.get_y()));
    for (const PowerViaMetalRect& rectangle : power_via.get_top_rect_list()) {
      result.add(power_via.get_top_layer_idx(), rectangle, top_node_id);
    }
  }
  result.build();
  return result;
}

}  // namespace

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
  const auto registerPinNode = [&power_graph](PowerPin& power_pin, const std::size_t node_id) {
    PowerNode& power_node = power_graph.get_node_list()[node_id];
    if (power_pin.get_is_source()) {
      power_node.set_type(PowerNodeType::kSource);
      power_node.set_is_source(true);
      std::vector<std::size_t>& source_node_id_list = power_graph.get_source_node_id_list();
      appendUniqueNodeId(source_node_id_list, node_id);
    } else {
      power_node.get_instance_id_set().insert(power_pin.get_instance_id());
      std::vector<std::size_t>& node_id_list = power_graph.get_instance_node_id_list_map()[power_pin.get_instance_id()];
      appendUniqueNodeId(node_id_list, node_id);
    }
  };

  const auto appendCenterNode = [this, &power_graph, &power_net, &gb_model](PowerPin& power_pin) {
    const PowerNodeType node_type = power_pin.get_is_source() ? PowerNodeType::kSource : PowerNodeType::kInstancePin;
    const std::size_t node_id = getPowerNode(power_graph, power_pin.get_layer_idx(), power_pin.get_x(), power_pin.get_y(), node_type);
    for (std::size_t segment_idx = 0; segment_idx < power_net.get_wire_segment_list().size(); segment_idx++) {
      PowerWireSegment& power_wire_segment = power_net.get_wire_segment_list()[segment_idx];
      if (power_wire_segment.get_layer_idx() == power_pin.get_layer_idx()
          && isOnWireSegment(power_wire_segment, power_graph.get_node_list()[node_id])) {
        appendWireNodeId(gb_model, segment_idx, node_id);
      }
    }
    return node_id;
  };

  const ViaContactIndexByLayer via_contact_index = buildViaContactIndex(power_graph, power_net);
  std::map<PinPortKey, PendingPinPort> pending_port_map;
  for (PowerPin& power_pin : power_net.get_pin_list()) {
    const PowerNodeType node_type = power_pin.get_is_source() ? PowerNodeType::kSource : PowerNodeType::kInstancePin;
    std::vector<std::size_t> contact_node_ids;
    for (std::size_t segment_idx = 0; segment_idx < power_net.get_wire_segment_list().size(); segment_idx++) {
      PowerWireSegment& power_wire_segment = power_net.get_wire_segment_list()[segment_idx];
      if (power_wire_segment.get_layer_idx() != power_pin.get_layer_idx()) {
        continue;
      }
      int32_t x = power_pin.get_x();
      int32_t y = power_pin.get_y();
      const bool is_contact = power_pin.has_bounds() ? getPinWireContact(power_pin, power_wire_segment, x, y)
                                                     : isOnWireSegment(power_wire_segment, x, y);
      if (!is_contact) {
        continue;
      }
      const std::size_t node_id = getPowerNode(power_graph, power_pin.get_layer_idx(), x, y, node_type);
      appendWireNodeId(gb_model, segment_idx, node_id);
      appendUniqueNodeId(contact_node_ids, node_id);
    }
    via_contact_index.appendContactNodeIds(power_pin, contact_node_ids);

    if (!power_pin.has_port_id()) {
      if (contact_node_ids.empty()) {
        contact_node_ids.push_back(appendCenterNode(power_pin));
      }
      for (const std::size_t node_id : contact_node_ids) {
        registerPinNode(power_pin, node_id);
      }
      for (std::size_t node_idx = 1; node_idx < contact_node_ids.size(); node_idx++) {
        power_graph.add_node_equivalence(contact_node_ids.front(), contact_node_ids[node_idx]);
      }
      continue;
    }

    const PinPortKey key{.is_source = power_pin.get_is_source(),
                         .instance_id = power_pin.get_instance_id(),
                         .pin_name = power_pin.get_pin_name(),
                         .port_id = power_pin.get_port_id()};
    PendingPinPort& pending_port = pending_port_map[key];
    if (pending_port.fallback_pin == nullptr) {
      pending_port.fallback_pin = &power_pin;
    }
    for (const std::size_t node_id : contact_node_ids) {
      appendUniqueNodeId(pending_port.node_ids, node_id);
    }
  }

  for (auto& pending_port_pair : pending_port_map) {
    PendingPinPort& pending_port = pending_port_pair.second;
    if (pending_port.node_ids.empty()) {
      pending_port.node_ids.push_back(appendCenterNode(*pending_port.fallback_pin));
    }
    for (const std::size_t node_id : pending_port.node_ids) {
      registerPinNode(*pending_port.fallback_pin, node_id);
    }
    for (std::size_t node_idx = 1; node_idx < pending_port.node_ids.size(); node_idx++) {
      power_graph.add_node_equivalence(pending_port.node_ids.front(), pending_port.node_ids[node_idx]);
    }
  }
}

bool GraphBuilder::getPinWireContact(PowerPin& power_pin, PowerWireSegment& power_wire_segment, int32_t& x, int32_t& y)
{
  const int32_t half_width = power_wire_segment.get_width() / 2;
  const int32_t min_x = std::min(power_wire_segment.get_first_x(), power_wire_segment.get_second_x());
  const int32_t max_x = std::max(power_wire_segment.get_first_x(), power_wire_segment.get_second_x());
  const int32_t min_y = std::min(power_wire_segment.get_first_y(), power_wire_segment.get_second_y());
  const int32_t max_y = std::max(power_wire_segment.get_first_y(), power_wire_segment.get_second_y());
  if (power_wire_segment.get_first_y() == power_wire_segment.get_second_y()) {
    const int32_t ll_x = std::max(power_pin.get_ll_x(), min_x);
    const int32_t ur_x = std::min(power_pin.get_ur_x(), max_x);
    const int32_t ll_y = std::max(power_pin.get_ll_y(), power_wire_segment.get_first_y() - half_width);
    const int32_t ur_y = std::min(power_pin.get_ur_y(), power_wire_segment.get_first_y() + half_width);
    if (ll_x > ur_x || ll_y > ur_y) {
      return false;
    }
    x = std::clamp(power_pin.get_x(), ll_x, ur_x);
    y = power_wire_segment.get_first_y();
    return true;
  }
  if (power_wire_segment.get_first_x() == power_wire_segment.get_second_x()) {
    const int32_t ll_x = std::max(power_pin.get_ll_x(), power_wire_segment.get_first_x() - half_width);
    const int32_t ur_x = std::min(power_pin.get_ur_x(), power_wire_segment.get_first_x() + half_width);
    const int32_t ll_y = std::max(power_pin.get_ll_y(), min_y);
    const int32_t ur_y = std::min(power_pin.get_ur_y(), max_y);
    if (ll_x > ur_x || ll_y > ur_y) {
      return false;
    }
    x = power_wire_segment.get_first_x();
    y = std::clamp(power_pin.get_y(), ll_y, ur_y);
    return true;
  }
  EMIRLOG.error(Loc::current(), "The power wire segment is oblique!");
  return false;
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
  for (const auto [first_node_id, second_node_id] : power_graph.get_node_equivalence_pair_list()) {
    node_adjacency_map[first_node_id].push_back(second_node_id);
    node_adjacency_map[second_node_id].push_back(first_node_id);
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
