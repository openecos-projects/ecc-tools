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

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "DataManager.hpp"
#include "GraphBuilder.hpp"
#include "IRAnalyzer.hpp"
#include "InstancePower.hpp"
#include "Logger.hpp"
#include "PowerGraph.hpp"
#include "PowerNet.hpp"
#include "PowerNetType.hpp"
#include "PowerNode.hpp"
#include "PowerPin.hpp"
#include "PowerWireSegment.hpp"

namespace {

iemir::PowerPin makePin(const uint64_t instance_id, const std::string& name, const uint64_t port_id, const bool is_source, const int32_t ll_x,
                         const int32_t ll_y, const int32_t ur_x, const int32_t ur_y)
{
  iemir::PowerPin pin;
  pin.set_instance_id(instance_id);
  pin.set_pin_name(name);
  pin.set_port_id(port_id);
  pin.set_is_source(is_source);
  pin.set_layer_idx(0);
  pin.set_bounds(ll_x, ll_y, ur_x, ur_y);
  return pin;
}

std::optional<std::size_t> nodeAt(iemir::PowerGraph& graph, const int32_t x, const int32_t y)
{
  for (iemir::PowerNode& node : graph.get_node_list()) {
    if (node.get_x() == x && node.get_y() == y && node.get_layer_idx() == 0) {
      return node.get_node_id();
    }
  }
  return std::nullopt;
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

}  // namespace

int main()
{
  iemir::Logger::initInst();
  iemir::DataManager::initInst();
  iemir::GraphBuilder::initInst();

  iemir::PowerNet net;
  net.set_net_name("VDD");
  net.set_type(iemir::PowerNetType::kPower);

  iemir::PowerWireSegment wire;
  wire.set_layer_idx(0);
  wire.set_first_x(0);
  wire.set_first_y(10);
  wire.set_second_x(100);
  wire.set_second_y(10);
  wire.set_width(4);
  wire.set_resistance_per_square(1.0);
  net.get_wire_segment_list().push_back(wire);

  // The second rectangle of each PORT meets the lower edge of the physical
  // wire, while its center is below the wire and would fail the old test.
  net.get_pin_list().push_back(makePin(0, "VDD", 1, true, 0, 8, 4, 12));
  net.get_pin_list().push_back(makePin(1, "U1:VDD", 10, false, 20, 8, 40, 12));
  net.get_pin_list().push_back(makePin(1, "U1:VDD", 10, false, 30, 5, 35, 9));
  net.get_pin_list().push_back(makePin(1, "U1:VDD", 11, false, 60, 8, 80, 12));
  net.get_pin_list().push_back(makePin(1, "U1:VDD", 11, false, 70, 5, 75, 9));
  iemir::PowerPin legacy_point_pin;
  legacy_point_pin.set_instance_id(2);
  legacy_point_pin.set_pin_name("U2:VDD");
  legacy_point_pin.set_layer_idx(0);
  legacy_point_pin.set_x(90);
  legacy_point_pin.set_y(10);
  net.get_pin_list().push_back(std::move(legacy_point_pin));

  iemir::InstancePower instance_power;
  instance_power.set_instance_id(1);
  instance_power.set_voltage(1.0);
  instance_power.set_internal_power(0.02);
  EMIRDM.getDatabase().get_instance_power_map()[1] = instance_power;
  EMIRDM.getDatabase().get_power_net_map()["VDD"] = std::move(net);

  EMIRGB.build();
  iemir::GraphBuilder::destroyInst();

  iemir::PowerGraph& graph = EMIRDM.getDatabase().get_power_graph_map()["VDD"];
  const auto first_port_main = nodeAt(graph, 30, 10);
  const auto first_port_edge = nodeAt(graph, 32, 10);
  const auto second_port_main = nodeAt(graph, 70, 10);
  const auto second_port_edge = nodeAt(graph, 72, 10);
  const auto legacy_point = nodeAt(graph, 90, 10);
  const bool has_expected_contacts = first_port_main && first_port_edge && second_port_main && second_port_edge && legacy_point;
  const auto canonical_node_ids = graph.node_canonical_id_list();
  const bool ports_remain_distinct = has_expected_contacts && canonical_node_ids[*first_port_edge] != canonical_node_ids[*second_port_edge];
  const bool first_port_is_strongly_connected = has_expected_contacts && hasPair(graph, *first_port_main, *first_port_edge);
  const bool second_port_is_strongly_connected = has_expected_contacts && hasPair(graph, *second_port_main, *second_port_edge);
  const bool legacy_point_input_is_preserved = has_expected_contacts
                                               && graph.get_instance_node_id_list_map()[2].size() == 1u
                                               && graph.get_instance_node_id_list_map()[2].front() == *legacy_point;

  iemir::IRAnalyzer::initInst();
  EMIRIA.analyze();
  iemir::IRAnalyzer::destroyInst();

  const bool equivalent_nodes_share_voltage = has_expected_contacts
                                               && std::abs(graph.get_node_list()[*first_port_main].get_voltage()
                                                           - graph.get_node_list()[*first_port_edge].get_voltage())
                                                      <= EMIR_ERROR
                                               && std::abs(graph.get_node_list()[*second_port_main].get_voltage()
                                                           - graph.get_node_list()[*second_port_edge].get_voltage())
                                                      <= EMIR_ERROR;
  // The two logical PORT loads receive 0.01 A each. With R(source,A)=7
  // and R(A,B)=9.5, the quotient graph has VA=0.86 V and VB=0.765 V.
  const bool has_expected_ir_solution = has_expected_contacts && std::abs(graph.get_node_list()[*first_port_main].get_voltage() - 0.86) <= EMIR_ERROR
                                        && std::abs(graph.get_node_list()[*second_port_main].get_voltage() - 0.765) <= EMIR_ERROR
                                        && std::abs(graph.get_node_list()[*first_port_main].get_current() + 0.01) <= EMIR_ERROR;
  const bool is_pass = graph.get_is_connected() && has_expected_contacts && ports_remain_distinct && first_port_is_strongly_connected
                       && second_port_is_strongly_connected && legacy_point_input_is_preserved && equivalent_nodes_share_voltage
                       && has_expected_ir_solution;

  iemir::DataManager::destroyInst();
  iemir::Logger::destroyInst();
  return is_pass ? 0 : 1;
}
