// ***************************************************************************************
// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2026 Peng Cheng Laboratory
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
// See the Mulan PSL v2 for more details.
// ***************************************************************************************

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "DataManager.hpp"
#include "GraphBuilder.hpp"
#include "IRAnalyzer.hpp"
#include "InstancePower.hpp"
#include "Logger.hpp"
#include "PowerEdge.hpp"
#include "PowerEdgeType.hpp"
#include "PowerGraph.hpp"
#include "PowerNet.hpp"
#include "PowerNetType.hpp"
#include "PowerNode.hpp"
#include "PowerPin.hpp"
#include "PowerVia.hpp"
#include "PowerWireSegment.hpp"

namespace {

iemir::PowerWireSegment makeWire(const int32_t layer, const int32_t first_x, const int32_t first_y, const int32_t second_x,
                                 const int32_t second_y, const int32_t width = 10)
{
  iemir::PowerWireSegment wire;
  wire.set_layer_idx(layer);
  wire.set_first_x(first_x);
  wire.set_first_y(first_y);
  wire.set_second_x(second_x);
  wire.set_second_y(second_y);
  wire.set_width(width);
  wire.set_resistance_per_square(1.0);
  return wire;
}

iemir::PowerPin makePin(const uint64_t instance_id, const std::string& name, const uint64_t port_id, const bool is_source,
                        const int32_t layer, const int32_t ll_x, const int32_t ll_y, const int32_t ur_x, const int32_t ur_y)
{
  iemir::PowerPin pin;
  pin.set_instance_id(instance_id);
  pin.set_pin_name(name);
  pin.set_port_id(port_id);
  pin.set_is_source(is_source);
  pin.set_layer_idx(layer);
  pin.set_bounds(ll_x, ll_y, ur_x, ur_y);
  return pin;
}

iemir::PowerVia makeVia(const int32_t bottom_layer, const int32_t top_layer, const int32_t x, const int32_t y, const double resistance,
                        std::initializer_list<iemir::PowerViaMetalRect> bottom_rectangles,
                        std::initializer_list<iemir::PowerViaMetalRect> top_rectangles)
{
  iemir::PowerVia via;
  via.set_bottom_layer_idx(bottom_layer);
  via.set_top_layer_idx(top_layer);
  via.set_x(x);
  via.set_y(y);
  via.set_cut_num(1);
  via.set_resistance(resistance);
  for (const auto rectangle : bottom_rectangles) {
    via.add_bottom_rect(rectangle);
  }
  for (const auto rectangle : top_rectangles) {
    via.add_top_rect(rectangle);
  }
  return via;
}

std::optional<std::size_t> nodeAt(iemir::PowerGraph& graph, const int32_t layer, const int32_t x, const int32_t y)
{
  for (iemir::PowerNode& node : graph.get_node_list()) {
    if (node.get_layer_idx() == layer && node.get_x() == x && node.get_y() == y) {
      return node.get_node_id();
    }
  }
  return std::nullopt;
}

bool containsNode(const std::vector<std::size_t>& node_ids, const std::size_t node_id)
{
  return std::find(node_ids.begin(), node_ids.end(), node_id) != node_ids.end();
}

bool hasPair(iemir::PowerGraph& graph, const std::size_t first, const std::size_t second)
{
  for (const auto [left, right] : graph.get_node_equivalence_pair_list()) {
    if ((left == first && right == second) || (left == second && right == first)) {
      return true;
    }
  }
  return false;
}

bool hasEdge(iemir::PowerGraph& graph, const std::size_t first, const std::size_t second)
{
  for (iemir::PowerEdge& edge : graph.get_edge_list()) {
    if ((edge.get_first_node_id() == first && edge.get_second_node_id() == second)
        || (edge.get_first_node_id() == second && edge.get_second_node_id() == first)) {
      return true;
    }
  }
  return false;
}

bool hasPositiveResistanceViaEdge(iemir::PowerGraph& graph, const std::size_t first, const std::size_t second, const double resistance)
{
  for (iemir::PowerEdge& edge : graph.get_edge_list()) {
    if (((edge.get_first_node_id() == first && edge.get_second_node_id() == second)
         || (edge.get_first_node_id() == second && edge.get_second_node_id() == first))
        && edge.get_type() == iemir::PowerEdgeType::kVia && std::abs(edge.get_resistance() - resistance) <= EMIR_ERROR) {
      return true;
    }
  }
  return false;
}

void addInstancePower(const uint64_t instance_id, const double internal_power)
{
  iemir::InstancePower instance_power;
  instance_power.set_instance_id(instance_id);
  instance_power.set_voltage(1.0);
  instance_power.set_internal_power(internal_power);
  EMIRDM.getDatabase().get_instance_power_map()[instance_id] = instance_power;
}

}  // namespace

int main()
{
  iemir::Logger::initInst();
  iemir::DataManager::initInst();
  iemir::GraphBuilder::initInst();

  iemir::PowerNet net;
  net.set_net_name("VDD");
  net.set_type(iemir::PowerNetType::kPower);

  // The primary lower enclosure reaches the load pin, while the Via origin
  // is outside the pin.  The upper node reaches the source through 8 ohms of
  // wire and the Via adds 2 ohms, yielding 0.8 V for a 20 mA load.
  net.get_wire_segment_list().push_back(makeWire(1, 20, 10, 100, 10));
  net.get_via_list().push_back(makeVia(0, 1, 20, 10, 2.0, {{0, 0, 10, 10}}, {{15, 5, 25, 15}}));
  net.get_pin_list().push_back(makePin(0, "VDD", 1, true, 1, 100, 10, 100, 10));
  net.get_pin_list().push_back(makePin(1, "U1:VDD", 10, false, 0, 0, 0, 10, 10));

  // Two Via contacts in one PORT are ideal equivalents, but they remain
  // separate positive-resistance Via edges.  They must not become a zero-R
  // PowerEdge.
  net.get_wire_segment_list().push_back(makeWire(4, 20, 10, 100, 10));
  net.get_via_list().push_back(makeVia(3, 4, 20, 10, 3.0, {{0, 0, 10, 10}}, {{15, 5, 25, 15}}));
  net.get_via_list().push_back(makeVia(3, 4, 40, 10, 3.0, {{15, 0, 25, 10}}, {{35, 5, 45, 15}}));
  net.get_pin_list().push_back(makePin(0, "VDD", 2, true, 4, 100, 10, 100, 10));
  net.get_pin_list().push_back(makePin(2, "U2:VDD", 20, false, 3, 0, 0, 10, 10));
  net.get_pin_list().push_back(makePin(2, "U2:VDD", 20, false, 3, 15, 0, 25, 10));

  // The union bbox of these rectangles would cover the pin below, but no
  // individual lower enclosure does.  The load must stay on its own wire.
  net.get_wire_segment_list().push_back(makeWire(6, 0, 5, 30, 5));
  net.get_wire_segment_list().push_back(makeWire(5, 12, 5, 18, 5));
  net.get_wire_segment_list().push_back(makeWire(5, 20, 5, 30, 5));
  net.get_via_list().push_back(makeVia(5, 6, 20, 5, 3.0, {{0, 0, 10, 10}, {20, 0, 30, 10}}, {{15, 0, 25, 10}}));
  // These point-only Vias retain the legacy fallback and keep the negative
  // enclosure case in a connected graph without entering the new index.
  net.get_via_list().push_back(makeVia(5, 6, 12, 5, 3.0, {}, {}));
  net.get_via_list().push_back(makeVia(5, 6, 30, 5, 3.0, {}, {}));
  net.get_pin_list().push_back(makePin(0, "VDD", 3, true, 6, 30, 5, 30, 5));
  net.get_pin_list().push_back(makePin(0, "VDD", 4, true, 5, 30, 5, 30, 5));
  net.get_pin_list().push_back(makePin(3, "U3:VDD", 30, false, 5, 12, 0, 18, 10));

  // A lower-metal enclosure at the same coordinates is not a connection to a
  // pin drawn on another routing layer.
  net.get_wire_segment_list().push_back(makeWire(6, 40, 5, 60, 5));
  net.get_wire_segment_list().push_back(makeWire(7, 0, 5, 60, 5));
  net.get_via_list().push_back(makeVia(6, 7, 50, 5, 3.0, {{0, 0, 10, 10}}, {{45, 0, 55, 10}}));
  net.get_pin_list().push_back(makePin(0, "VDD", 5, true, 6, 60, 5, 60, 5));
  net.get_pin_list().push_back(makePin(0, "VDD", 6, true, 7, 60, 5, 60, 5));
  net.get_pin_list().push_back(makePin(4, "U4:VDD", 40, false, 7, 0, 0, 10, 10));

  addInstancePower(1, 0.02);
  addInstancePower(2, 0.0);
  addInstancePower(3, 0.0);
  addInstancePower(4, 0.0);
  EMIRDM.getDatabase().get_power_net_map()["VDD"] = std::move(net);

  EMIRGB.build();
  iemir::GraphBuilder::destroyInst();

  iemir::PowerGraph& graph = EMIRDM.getDatabase().get_power_graph_map()["VDD"];
  const auto primary_bottom = nodeAt(graph, 0, 20, 10);
  const auto primary_top = nodeAt(graph, 1, 20, 10);
  const auto first_port_bottom = nodeAt(graph, 3, 20, 10);
  const auto second_port_bottom = nodeAt(graph, 3, 40, 10);
  const auto gap_bottom = nodeAt(graph, 5, 20, 5);
  const auto mismatch_via_top = nodeAt(graph, 7, 50, 5);
  const auto primary_load_nodes = graph.get_instance_node_id_list_map()[1];
  const auto gap_load_nodes = graph.get_instance_node_id_list_map()[3];
  const auto mismatch_load_nodes = graph.get_instance_node_id_list_map()[4];
  const auto canonical_node_ids = graph.node_canonical_id_list();

  const bool primary_enclosure_contact = primary_bottom && primary_top && containsNode(primary_load_nodes, *primary_bottom)
                                         && hasPositiveResistanceViaEdge(graph, *primary_bottom, *primary_top, 2.0);
  const bool same_port_equivalence = first_port_bottom && second_port_bottom && hasPair(graph, *first_port_bottom, *second_port_bottom)
                                     && canonical_node_ids[*first_port_bottom] == canonical_node_ids[*second_port_bottom]
                                     && !hasEdge(graph, *first_port_bottom, *second_port_bottom);
  const bool union_bbox_gap_is_not_contact = gap_bottom && !containsNode(gap_load_nodes, *gap_bottom);
  const bool different_layer_is_not_contact = mismatch_via_top && !containsNode(mismatch_load_nodes, *mismatch_via_top);

  iemir::IRAnalyzer::initInst();
  EMIRIA.analyze();
  iemir::IRAnalyzer::destroyInst();

  const bool has_expected_ir_solution = primary_bottom && std::abs(graph.get_node_list()[*primary_bottom].get_voltage() - 0.8) <= EMIR_ERROR
                                        && std::abs(graph.get_node_list()[*primary_bottom].get_current() + 0.02) <= EMIR_ERROR;
  const bool is_pass = graph.get_is_connected() && primary_enclosure_contact && same_port_equivalence && union_bbox_gap_is_not_contact
                       && different_layer_is_not_contact && has_expected_ir_solution;

  iemir::DataManager::destroyInst();
  iemir::Logger::destroyInst();
  return is_pass ? 0 : 1;
}
