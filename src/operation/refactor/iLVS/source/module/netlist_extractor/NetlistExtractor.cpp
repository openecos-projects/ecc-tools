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
#include "NetlistExtractor.hpp"

#include <algorithm>
#include <limits>
#include <numeric>
#include <unordered_set>

#include "IdbDesign.h"
#include "IdbInstance.h"
#include "IdbNet.h"
#include "IdbPins.h"
#include "IdbSpecialNet.h"
#include "IdbVias.h"

namespace ilvs {

// public

void NetlistExtractor::initInst()
{
  if (_ne_instance == nullptr) {
    _ne_instance = new NetlistExtractor();
  }
}

NetlistExtractor& NetlistExtractor::getInst()
{
  if (_ne_instance == nullptr) {
    LVSLOG.error(Loc::current(), "The instance not initialized!");
  }
  return *_ne_instance;
}

void NetlistExtractor::destroyInst()
{
  if (_ne_instance != nullptr) {
    delete _ne_instance;
    _ne_instance = nullptr;
  }
}

// private

NetlistExtractor* NetlistExtractor::_ne_instance = nullptr;

namespace {

struct GraphNode
{
  std::string net_name;
  idb::IdbRect rect;
  int32_t layer_id = -1;
  int32_t layer_order = -1;
  bool is_terminal = false;
  bool is_io_terminal = false;
  bool is_power_terminal = false;
  bool is_ground_terminal = false;
  bool is_supply_route_shape = false;
  uint64_t routing_shape_idx = std::numeric_limits<uint64_t>::max();
};

class DisjointSet
{
 public:
  explicit DisjointSet(size_t size) : _parent(size), _rank(size, 0) { std::iota(_parent.begin(), _parent.end(), 0); }

  size_t find(size_t node)
  {
    if (_parent[node] != node) {
      _parent[node] = find(_parent[node]);
    }
    return _parent[node];
  }
  bool unite(size_t first, size_t second)
  {
    first = find(first);
    second = find(second);
    if (first == second) {
      return false;
    }
    if (_rank[first] < _rank[second]) {
      std::swap(first, second);
    }
    _parent[second] = first;
    if (_rank[first] == _rank[second]) {
      _rank[first]++;
    }
    return true;
  }

 private:
  std::vector<size_t> _parent;
  std::vector<uint8_t> _rank;
};

idb::IdbRect getPhysicalSegmentRect(idb::IdbRegularWireSegment* segment)
{
  idb::IdbRect rect = segment->get_segment_rect();
  if (!segment->is_rect()) {
    return rect;
  }
  // DEF path RECT coordinates are offsets from the preceding path point.
  if (idb::IdbCoordinate<int32_t>* point = segment->get_point_start(); point != nullptr) {
    rect.moveByStep(point->get_x(), point->get_y());
  }
  return rect;
}

}  // namespace

#if 1  // extract

Netlist NetlistExtractor::extractNetlist(idb::IdbDesign* design, bool build_logical_graph, bool build_physical_graph)
{
  Netlist netlist;
  if (design == nullptr) {
    return netlist;
  }

  netlist.design_name = design->get_design_name();
  const auto normalize_name_list = [](std::vector<std::string>& name_list) {
    std::sort(name_list.begin(), name_list.end());
    name_list.erase(std::unique(name_list.begin(), name_list.end()), name_list.end());
  };
  const auto add_io_pin_list = [&normalize_name_list](idb::IdbPins* pin_list, std::vector<std::string>& io_pin_list) {
    if (pin_list == nullptr) {
      return;
    }
    for (idb::IdbPin* pin : pin_list->get_pin_list()) {
      if (pin != nullptr) {
        io_pin_list.push_back("PIN/" + pin->get_pin_name());
      }
    }
    normalize_name_list(io_pin_list);
  };
  const auto add_instance_map = [design](std::unordered_map<std::string, Instance>& instance_map) {
    idb::IdbInstanceList* instance_list = design->get_instance_list();
    if (instance_list == nullptr) {
      return;
    }
    for (idb::IdbInstance* instance : instance_list->get_instance_list()) {
      if (instance == nullptr) {
        continue;
      }
      Instance& instance_node = instance_map[instance->get_name()];
      instance_node.name = instance->get_name();
      if (idb::IdbCellMaster* master = instance->get_cell_master(); master != nullptr) {
        instance_node.master_name = master->get_name();
      }
    }
  };
  if (build_logical_graph) {
    add_io_pin_list(design->get_io_pin_list(), netlist.logical_graph.io_pin_list);
    add_instance_map(netlist.logical_graph.instance_map);
  }
  if (build_physical_graph) {
    add_io_pin_list(design->get_io_pin_list(), netlist.physical_graph.io_pin_list);
    add_instance_map(netlist.physical_graph.instance_map);
  }

  if (design->get_net_list() != nullptr) {
    for (idb::IdbNet* idb_net : design->get_net_list()->get_net_list()) {
      if (idb_net == nullptr) {
        continue;
      }
      Net net;
      net.name = idb_net->get_net_name();
      net.wire_segment_num = idb_net->get_segment_wire_num();
      net.via_num = idb_net->get_via_num();

      auto add_terminal_list = [this, &net, &netlist, build_logical_graph](idb::IdbPins* pin_list) {
        if (pin_list == nullptr) {
          return;
        }
        for (idb::IdbPin* pin : pin_list->get_pin_list()) {
          if (pin != nullptr) {
            net.terminal_list.push_back(getTerminalName(pin));
            if (build_logical_graph) {
              if (idb::IdbInstance* instance = pin->get_instance(); instance != nullptr) {
                Instance& instance_node = netlist.logical_graph.instance_map[instance->get_name()];
                instance_node.name = instance->get_name();
                if (idb::IdbCellMaster* master = instance->get_cell_master(); master != nullptr) {
                  instance_node.master_name = master->get_name();
                }
                instance_node.pin_list.push_back(pin->get_pin_name());
              }
            }
          }
        }
      };
      add_terminal_list(idb_net->get_io_pins());
      add_terminal_list(idb_net->get_instance_pin_list());
      normalize_name_list(net.terminal_list);
      netlist.net_map.emplace(net.name, std::move(net));
    }
  }
  if (build_logical_graph) {
    for (auto& [instance_name, instance_node] : netlist.logical_graph.instance_map) {
      (void) instance_name;
      normalize_name_list(instance_node.pin_list);
    }
    for (const auto& [net_name, net] : netlist.net_map) {
      (void) net_name;
      netlist.logical_graph.net_edge_num += net.terminal_list.size();
    }
  }
  if (!build_physical_graph || design->get_net_list() == nullptr) {
    return netlist;
  }
  std::vector<GraphNode> graph_node_list;
  std::vector<std::pair<size_t, size_t>> via_node_pair_list;
  std::unordered_map<std::string, std::vector<std::vector<size_t>>> terminal_node_map;
  std::unordered_map<std::string, std::vector<std::string>> terminal_name_map;
  const auto add_shape = [&graph_node_list, &netlist](const std::string& net_name, idb::IdbLayer* layer, const idb::IdbRect& rect,
                                                       bool is_terminal = false, bool is_io_terminal = false, bool is_power_terminal = false,
                                                       bool is_ground_terminal = false, bool is_supply_route_shape = false) {
    if (layer == nullptr || !layer->is_routing()) {
      return static_cast<size_t>(-1);
    }
    uint64_t routing_shape_idx = std::numeric_limits<uint64_t>::max();
    auto routing_graph_iter = netlist.physical_graph.net_routing_graph_map.find(net_name);
    if (routing_graph_iter != netlist.physical_graph.net_routing_graph_map.end()) {
      idb::IdbRect shape_rect = rect;
      NetRoutingGraph& routing_graph = routing_graph_iter->second;
      routing_shape_idx = routing_graph.shape_list.size();
      routing_graph.shape_list.push_back(
          {layer->get_id(), shape_rect.get_low_x(), shape_rect.get_low_y(), shape_rect.get_high_x(), shape_rect.get_high_y()});
    }
    graph_node_list.push_back({net_name, rect, layer->get_id(), layer->get_order(), is_terminal, is_io_terminal, is_power_terminal,
                               is_ground_terminal, is_supply_route_shape, routing_shape_idx});
    return graph_node_list.size() - 1;
  };
  const auto add_pin = [this, &add_shape, &graph_node_list, &netlist, &terminal_node_map, &terminal_name_map](const std::string& net_name,
                                                                                                                   idb::IdbPin* pin, bool is_power_net,
                                                                                                                   bool is_ground_net) {
    const std::string terminal_name = getTerminalName(pin);
    if (!pin->is_io_pin()) {
      if (is_power_net) {
        netlist.physical_graph.power_instance_pin_net_map[terminal_name] = net_name;
      } else if (is_ground_net) {
        netlist.physical_graph.ground_instance_pin_net_map[terminal_name] = net_name;
      }
    }
    std::vector<size_t> pin_node_list;
    for (idb::IdbLayerShape* layer_shape : pin->get_port_box_list()) {
      if (layer_shape == nullptr) {
        continue;
      }
      for (idb::IdbRect* rect : layer_shape->get_rect_list()) {
        if (rect != nullptr) {
          size_t node = add_shape(net_name, layer_shape->get_layer(), *rect, true, pin->is_io_pin(), is_power_net, is_ground_net);
          if (node != static_cast<size_t>(-1)) {
            pin_node_list.push_back(node);
          }
        }
      }
    }
    if (!pin_node_list.empty()) {
      terminal_node_map[net_name].push_back(std::move(pin_node_list));
      terminal_name_map[net_name].push_back(terminal_name);
      auto routing_graph_iter = netlist.physical_graph.net_routing_graph_map.find(net_name);
      if (routing_graph_iter != netlist.physical_graph.net_routing_graph_map.end()) {
        std::vector<uint64_t>& shape_index_list = routing_graph_iter->second.terminal_shape_map[terminal_name];
        for (size_t node_idx : terminal_node_map[net_name].back()) {
          const uint64_t routing_shape_idx = graph_node_list[node_idx].routing_shape_idx;
          if (routing_shape_idx != std::numeric_limits<uint64_t>::max()) {
            shape_index_list.push_back(routing_shape_idx);
          }
        }
      }
    }
  };
  const auto add_via = [&add_shape, &graph_node_list, &netlist, &via_node_pair_list](const std::string& net_name, idb::IdbVia* via) {
    if (via == nullptr) {
      return;
    }
    idb::IdbLayerShape bottom_shape = via->get_bottom_layer_shape();
    idb::IdbLayerShape top_shape = via->get_top_layer_shape();
    size_t bottom_node = add_shape(net_name, bottom_shape.get_layer(), bottom_shape.get_bounding_box());
    size_t top_node = add_shape(net_name, top_shape.get_layer(), top_shape.get_bounding_box());
    if (bottom_node != static_cast<size_t>(-1) && top_node != static_cast<size_t>(-1)) {
      via_node_pair_list.emplace_back(bottom_node, top_node);
      auto routing_graph_iter = netlist.physical_graph.net_routing_graph_map.find(net_name);
      if (routing_graph_iter != netlist.physical_graph.net_routing_graph_map.end()) {
        const uint64_t bottom_shape_idx = graph_node_list[bottom_node].routing_shape_idx;
        const uint64_t top_shape_idx = graph_node_list[top_node].routing_shape_idx;
        if (bottom_shape_idx != std::numeric_limits<uint64_t>::max() && top_shape_idx != std::numeric_limits<uint64_t>::max()) {
          routing_graph_iter->second.via_shape_pair_list.emplace_back(bottom_shape_idx, top_shape_idx);
        }
      }
    }
  };

  for (idb::IdbNet* idb_net : design->get_net_list()->get_net_list()) {
    if (idb_net == nullptr) {
      continue;
    }
    const std::string net_name = idb_net->get_net_name();
    NetRoutingGraph& routing_graph = netlist.physical_graph.net_routing_graph_map[net_name];
    if (idb_net->get_pin_number() > 0) {
      if (idb::IdbPin* driving_pin = idb_net->get_driving_pin(); driving_pin != nullptr) {
        routing_graph.driver_terminal_name = getTerminalName(driving_pin);
      }
    }
    for (idb::IdbPin* pin : idb_net->get_io_pins()->get_pin_list()) {
      if (pin != nullptr) {
        add_pin(net_name, pin, false, false);
      }
    }
    for (idb::IdbPin* pin : idb_net->get_instance_pin_list()->get_pin_list()) {
      if (pin != nullptr) {
        add_pin(net_name, pin, false, false);
      }
    }
    for (idb::IdbRegularWire* wire : idb_net->get_wire_list()->get_wire_list()) {
      for (idb::IdbRegularWireSegment* segment : wire->get_segment_list()) {
        if (segment == nullptr) {
          continue;
        }
        if (segment->is_via()) {
          for (idb::IdbVia* via : segment->get_via_list()) {
            add_via(net_name, via);
          }
        } else if (segment->get_layer() != nullptr && (segment->is_wire() || segment->is_rect())) {
          add_shape(net_name, segment->get_layer(), getPhysicalSegmentRect(segment));
        }
      }
    }
  }
  for (idb::IdbSpecialNet* special_net : design->get_special_net_list()->get_net_list()) {
    if (special_net == nullptr) {
      continue;
    }
    const std::string net_name = special_net->get_net_name();
    const bool is_power_net = special_net->is_vdd();
    const bool is_ground_net = special_net->is_vss();
    if (is_power_net) {
      netlist.physical_graph.power_net_set.insert(net_name);
    } else if (is_ground_net) {
      netlist.physical_graph.ground_net_set.insert(net_name);
    }
    std::unordered_set<idb::IdbPin*> special_pin_set;
    const auto add_special_pin = [&add_pin, &special_pin_set, &net_name, is_power_net, is_ground_net](idb::IdbPin* pin) {
      if (pin != nullptr && special_pin_set.insert(pin).second) {
        add_pin(net_name, pin, is_power_net, is_ground_net);
      }
    };
    for (idb::IdbPin* pin : special_net->get_io_pins()->get_pin_list()) {
      add_special_pin(pin);
    }
    for (idb::IdbPin* pin : special_net->get_instance_pin_list()->get_pin_list()) {
      add_special_pin(pin);
    }
    if (special_net->has_wildcard_instance_pins() && design->get_instance_list() != nullptr) {
      for (idb::IdbInstance* instance : design->get_instance_list()->get_instance_list()) {
        if (instance == nullptr || instance->get_pin_list() == nullptr) {
          continue;
        }
        for (idb::IdbPin* pin : instance->get_pin_list()->get_pin_list()) {
          // DEF `(* term)` connections are intentionally lazy in iDB. Expand
          // them for this snapshot without mutating the shared design database.
          if (design->findSpecialNetForInstancePin(pin) == special_net) {
            add_special_pin(pin);
          }
        }
      }
    }
    for (idb::IdbSpecialWire* wire : special_net->get_wire_list()->get_wire_list()) {
      for (idb::IdbSpecialWireSegment* segment : wire->get_segment_list()) {
        if (segment == nullptr) {
          continue;
        }
        if (segment->is_via()) {
          add_via(net_name, segment->get_via());
        } else if (segment->get_layer() != nullptr && segment->is_line()) {
          add_shape(net_name, segment->get_layer(), idb::IdbRect(segment->get_point_start(), segment->get_point_second(), segment->get_route_width()),
                    false, false, false, false, is_power_net || is_ground_net);
        } else if (segment->get_layer() != nullptr && segment->is_rect() && segment->get_delta_rect() != nullptr) {
          add_shape(net_name, segment->get_layer(), *segment->get_delta_rect(), false, false, false, false, is_power_net || is_ground_net);
        }
      }
    }
  }

  DisjointSet graph(graph_node_list.size());
  uint64_t edge_num = 0;
  uint64_t candidate_pair_num = 0;
  uint64_t max_active_shape_num = 0;
  std::unordered_map<int32_t, std::vector<size_t>> layer_node_map;
  for (size_t node_idx = 0; node_idx < graph_node_list.size(); node_idx++) {
    layer_node_map[graph_node_list[node_idx].layer_id].push_back(node_idx);
  }
  for (auto& [layer_id, node_list] : layer_node_map) {
    (void) layer_id;
    std::sort(node_list.begin(), node_list.end(), [&graph_node_list](size_t first, size_t second) {
      return graph_node_list[first].rect.get_low_x() < graph_node_list[second].rect.get_low_x();
    });
    std::vector<size_t> active_node_list;
    for (size_t node_idx : node_list) {
      idb::IdbRect& node_rect = graph_node_list[node_idx].rect;
      active_node_list.erase(std::remove_if(active_node_list.begin(), active_node_list.end(), [&graph_node_list, &node_rect](size_t active_node_idx) {
                               return graph_node_list[active_node_idx].rect.get_high_x() < node_rect.get_low_x();
                             }),
                             active_node_list.end());
      for (size_t active_node_idx : active_node_list) {
        candidate_pair_num++;
        if (graph_node_list[active_node_idx].rect.isIntersection(node_rect) && graph.unite(active_node_idx, node_idx)) {
          edge_num++;
        }
      }
      active_node_list.push_back(node_idx);
      max_active_shape_num = std::max(max_active_shape_num, static_cast<uint64_t>(active_node_list.size()));
    }
  }
  for (const auto& [bottom_node, top_node] : via_node_pair_list) {
    if (graph.unite(bottom_node, top_node)) {
      edge_num++;
    }
  }
  for (const auto& [net_name, pin_node_list] : terminal_node_map) {
    (void) net_name;
    for (const std::vector<size_t>& nodes : pin_node_list) {
      for (size_t node_idx = 1; node_idx < nodes.size(); node_idx++) {
        if (graph.unite(nodes.front(), nodes[node_idx])) {
          edge_num++;
        }
      }
    }
  }

  std::unordered_map<size_t, std::unordered_set<std::string>> component_net_map;
  std::unordered_map<size_t, bool> component_metal_map;
  for (size_t node_idx = 0; node_idx < graph_node_list.size(); node_idx++) {
    size_t root = graph.find(node_idx);
    component_net_map[root].insert(graph_node_list[node_idx].net_name);
    component_metal_map[root] = component_metal_map[root] || !graph_node_list[node_idx].is_terminal;
  }
  netlist.physical_graph.node_num = graph_node_list.size();
  netlist.physical_graph.edge_num = edge_num;
  netlist.physical_graph.candidate_pair_num = candidate_pair_num;
  netlist.physical_graph.max_active_shape_num = max_active_shape_num;
  netlist.physical_graph.component_num = component_net_map.size();
  std::unordered_map<size_t, uint64_t> component_id_map;
  uint64_t component_id = 0;
  for (const auto& [root, net_name_set] : component_net_map) {
    component_id_map[root] = component_id;
    netlist.physical_graph.component_net_map[component_id] = {net_name_set.begin(), net_name_set.end()};
    if (net_name_set.size() > 1) {
      netlist.physical_graph.short_component_num++;
    }
    component_id++;
  }
  for (size_t node_idx = 0; node_idx < graph_node_list.size(); node_idx++) {
    GraphNode& node = graph_node_list[node_idx];
    const uint64_t component_id = component_id_map[graph.find(node_idx)];
    netlist.physical_graph.component_shape_map[component_id].push_back(
        {node.layer_id, node.rect.get_low_x(), node.rect.get_low_y(), node.rect.get_high_x(), node.rect.get_high_y()});
    if (node.is_supply_route_shape) {
      netlist.physical_graph.supply_route_shape_list.push_back(
          {node.net_name, component_id, node.layer_order,
           {node.layer_id, node.rect.get_low_x(), node.rect.get_low_y(), node.rect.get_high_x(), node.rect.get_high_y()}});
    }
  }
  for (const auto& [net_name, pin_node_list] : terminal_node_map) {
    std::unordered_set<size_t> terminal_component_set;
    uint64_t floating_terminal_num = 0;
    for (size_t pin_idx = 0; pin_idx < pin_node_list.size(); pin_idx++) {
      const std::vector<size_t>& nodes = pin_node_list[pin_idx];
      size_t root = graph.find(nodes.front());
      terminal_component_set.insert(root);
      netlist.physical_graph.component_terminal_map[component_id_map[root]].push_back(terminal_name_map[net_name][pin_idx]);
      netlist.physical_graph.terminal_component_map[terminal_name_map[net_name][pin_idx]] = component_id_map[root];
      if (!component_metal_map[root]) {
        floating_terminal_num++;
      }
      const GraphNode& node = graph_node_list[nodes.front()];
      if (node.is_power_terminal) {
        if (node.is_io_terminal) {
          netlist.physical_graph.power_port_num++;
          netlist.physical_graph.floating_power_port_num += !component_metal_map[root];
          if (!component_metal_map[root]) netlist.physical_graph.floating_power_port_list.push_back(terminal_name_map[net_name][pin_idx]);
        } else {
          netlist.physical_graph.power_pin_num++;
          netlist.physical_graph.floating_power_pin_num += !component_metal_map[root];
          if (!component_metal_map[root]) netlist.physical_graph.floating_power_pin_list.push_back(terminal_name_map[net_name][pin_idx]);
        }
      } else if (node.is_ground_terminal) {
        if (node.is_io_terminal) {
          netlist.physical_graph.ground_port_num++;
          netlist.physical_graph.floating_ground_port_num += !component_metal_map[root];
          if (!component_metal_map[root]) netlist.physical_graph.floating_ground_port_list.push_back(terminal_name_map[net_name][pin_idx]);
        } else {
          netlist.physical_graph.ground_pin_num++;
          netlist.physical_graph.floating_ground_pin_num += !component_metal_map[root];
          if (!component_metal_map[root]) netlist.physical_graph.floating_ground_pin_list.push_back(terminal_name_map[net_name][pin_idx]);
        }
      }
    }
    auto net_iter = netlist.net_map.find(net_name);
    if (net_iter != netlist.net_map.end()) {
      net_iter->second.terminal_component_num = terminal_component_set.size();
      net_iter->second.floating_terminal_num = floating_terminal_num;
    }
  }
  return netlist;
}

Netlist NetlistExtractor::extract(idb::IdbDesign* design)
{
  return extractNetlist(design, true, true);
}

Netlist NetlistExtractor::extractLogical(idb::IdbDesign* design)
{
  return extractNetlist(design, true, false);
}

Netlist NetlistExtractor::extractPhysical(idb::IdbDesign* design)
{
  return extractNetlist(design, false, true);
}

std::string NetlistExtractor::getTerminalName(idb::IdbPin* pin)
{
  if (pin->is_io_pin()) {
    return "PIN/" + pin->get_pin_name();
  }
  idb::IdbInstance* instance = pin->get_instance();
  if (instance == nullptr) {
    return "PIN/" + pin->get_pin_name();
  }
  return instance->get_name() + "/" + pin->get_pin_name();
}

#endif

}  // namespace ilvs
