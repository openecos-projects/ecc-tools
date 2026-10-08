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
#include "LVSInterface.hpp"

#include "DefData.hpp"
#include "IdbDesign.h"
#include "IdbInstance.h"
#include "IdbNet.h"
#include "IdbPins.h"
#include "IdbSpecialNet.h"
#include "IdbVias.h"
#include "RoutingShape.hpp"
#include "Utility.hpp"

namespace ilvs {

void LVSInterface::wrapDefRoutingData(idb::IdbDesign* design, DefData& def_data)
{
  if (design == nullptr) {
    return;
  }

  PhysicalGraph& physical_graph = def_data.get_physical_graph();
  std::vector<std::string> net_name_list;
  if (idb::IdbNetList* net_list = design->get_net_list(); net_list != nullptr) {
    net_name_list.reserve(net_list->get_net_list().size());
    for (idb::IdbNet* net : net_list->get_net_list()) {
      if (net != nullptr) {
        net_name_list.push_back(net->get_net_name());
      }
    }
  }
  if (idb::IdbSpecialNetList* special_net_list = design->get_special_net_list(); special_net_list != nullptr) {
    for (idb::IdbSpecialNet* net : special_net_list->get_net_list()) {
      if (net != nullptr) {
        net_name_list.push_back(net->get_net_name());
      }
    }
  }
  net_name_list = LVSUTIL.getSortedUniqueList(std::move(net_name_list));
  physical_graph.reserveNetNum(net_name_list.size());
  for (const std::string& net_name : net_name_list) {
    physical_graph.getOrCreateNetId(net_name);
  }

  wrapNetTerminalData(design, def_data);
  wrapSpecialNetTerminalData(design, def_data);
  for (NetRoutingGraph& routing_graph : physical_graph.get_net_routing_graph_list()) {
    routing_graph.set_terminal_routing_shape_num(static_cast<int32_t>(routing_graph.get_routing_shape_list().size()));
  }
  wrapNetWireData(design, def_data);
  wrapSpecialNetWireData(design, def_data);
  wrapNetViaData(design, def_data);
  wrapSpecialNetViaData(design, def_data);
}

void LVSInterface::wrapNetTerminalData(idb::IdbDesign* design, DefData& def_data)
{
  if (design == nullptr) {
    return;
  }

  PhysicalGraph& physical_graph = def_data.get_physical_graph();
  if (idb::IdbNetList* net_list = design->get_net_list(); net_list != nullptr) {
    for (idb::IdbNet* idb_net : net_list->get_net_list()) {
      if (idb_net == nullptr) {
        continue;
      }
      std::string net_name = idb_net->get_net_name();
      int32_t net_id = physical_graph.getNetId(net_name);
      NetRoutingGraph* routing_graph = physical_graph.getNetRoutingGraph(net_id);
      if (routing_graph == nullptr) {
        continue;
      }
      if (idb_net->get_pin_number() > 0) {
        if (idb::IdbPin* driving_pin = idb_net->get_driving_pin(); driving_pin != nullptr) {
          routing_graph->set_driver_terminal_name(getTerminalName(driving_pin));
        }
      }
      std::vector<std::pair<std::string, idb::IdbPin*>> pin_list;
      if (idb::IdbPins* io_pins = idb_net->get_io_pins(); io_pins != nullptr) {
        for (idb::IdbPin* pin : io_pins->get_pin_list()) {
          pin_list.emplace_back(getTerminalName(pin), pin);
        }
      }
      if (idb::IdbPins* instance_pins = idb_net->get_instance_pin_list(); instance_pins != nullptr) {
        for (idb::IdbPin* pin : instance_pins->get_pin_list()) {
          pin_list.emplace_back(getTerminalName(pin), pin);
        }
      }
      std::sort(pin_list.begin(), pin_list.end(), [](const auto& first, const auto& second) { return first.first < second.first; });
      for (auto& [terminal_name, pin] : pin_list) {
        wrapRoutingDataPin(net_name, net_id, terminal_name, pin, false, false, def_data);
      }
    }
  }
}

void LVSInterface::wrapNetWireData(idb::IdbDesign* design, DefData& def_data)
{
  if (design == nullptr) {
    return;
  }
  PhysicalGraph& physical_graph = def_data.get_physical_graph();
  if (idb::IdbNetList* net_list = design->get_net_list(); net_list != nullptr) {
    for (idb::IdbNet* idb_net : net_list->get_net_list()) {
      if (idb_net == nullptr) {
        continue;
      }
      NetRoutingGraph* routing_graph = physical_graph.getNetRoutingGraph(idb_net->get_net_name());
      if (routing_graph == nullptr) {
        continue;
      }
      if (idb::IdbRegularWireList* wire_list = idb_net->get_wire_list(); wire_list != nullptr) {
        for (idb::IdbRegularWire* wire : wire_list->get_wire_list()) {
          if (wire == nullptr) {
            continue;
          }
          for (idb::IdbRegularWireSegment* segment : wire->get_segment_list()) {
            if (segment == nullptr) {
              continue;
            }
            if (segment->get_layer() != nullptr && segment->get_layer()->is_routing() && (segment->is_wire() || segment->is_rect())) {
              appendRoutingShape(*routing_graph, wrapRoutingDataShape(segment->get_layer(), getPhysicalSegmentRect(segment)));
            }
          }
        }
      }
    }
  }
}

void LVSInterface::wrapNetViaData(idb::IdbDesign* design, DefData& def_data)
{
  if (design == nullptr) {
    return;
  }
  PhysicalGraph& physical_graph = def_data.get_physical_graph();
  if (idb::IdbNetList* net_list = design->get_net_list(); net_list != nullptr) {
    for (idb::IdbNet* idb_net : net_list->get_net_list()) {
      if (idb_net == nullptr) {
        continue;
      }
      NetRoutingGraph* routing_graph = physical_graph.getNetRoutingGraph(idb_net->get_net_name());
      if (routing_graph == nullptr) {
        continue;
      }
      if (idb::IdbRegularWireList* wire_list = idb_net->get_wire_list(); wire_list != nullptr) {
        for (idb::IdbRegularWire* wire : wire_list->get_wire_list()) {
          if (wire == nullptr) {
            continue;
          }
          for (idb::IdbRegularWireSegment* segment : wire->get_segment_list()) {
            if (segment == nullptr || !segment->is_via()) {
              continue;
            }
            for (idb::IdbVia* via : segment->get_via_list()) {
              wrapRoutingDataVia(via, *routing_graph);
            }
          }
        }
      }
    }
  }
}

void LVSInterface::wrapRoutingDataPin(const std::string& net_name, const int32_t net_id, const std::string& terminal_name, idb::IdbPin* pin,
                                      const bool is_power_net, const bool is_ground_net, DefData& def_data)
{
  if (pin == nullptr) {
    return;
  }

  PhysicalGraph& physical_graph = def_data.get_physical_graph();
  NetRoutingGraph* routing_graph = physical_graph.getNetRoutingGraph(net_id);
  if (routing_graph == nullptr) {
    return;
  }
  if (terminal_name.empty()) {
    return;
  }
  if (is_power_net) {
    def_data.get_terminal_connect_type_map()[terminal_name] = ConnectType::kPower;
  } else if (is_ground_net) {
    def_data.get_terminal_connect_type_map()[terminal_name] = ConnectType::kGround;
  }
  if (!pin->is_io_pin()) {
    if (is_power_net) {
      physical_graph.get_power_instance_pin_net_map()[terminal_name] = net_name;
    } else if (is_ground_net) {
      physical_graph.get_ground_instance_pin_net_map()[terminal_name] = net_name;
    }
  }

  if (routing_graph->get_terminal_shape_idx_map().find(terminal_name) != routing_graph->get_terminal_shape_idx_map().end()) {
    return;
  }
  std::vector<int32_t> terminal_shape_idx_list;
  for (idb::IdbLayerShape* layer_shape : pin->get_port_box_list()) {
    if (layer_shape == nullptr || layer_shape->get_layer() == nullptr || !layer_shape->get_layer()->is_routing()) {
      continue;
    }
    for (idb::IdbRect* rect : layer_shape->get_rect_list()) {
      if (rect == nullptr) {
        continue;
      }
      terminal_shape_idx_list.push_back(appendRoutingShape(*routing_graph, wrapRoutingDataShape(layer_shape->get_layer(), *rect)));
    }
  }
  if (!terminal_shape_idx_list.empty()) {
    routing_graph->get_terminal_shape_idx_map()[terminal_name] = std::move(terminal_shape_idx_list);
  }
}

RoutingShape LVSInterface::wrapRoutingDataShape(idb::IdbLayer* layer, const idb::IdbRect& rect)
{
  RoutingShape routing_shape;
  routing_shape.set_shape(wrapShape(layer->get_id(), rect));
  routing_shape.set_layer_order(layer->get_order());
  return routing_shape;
}

int32_t LVSInterface::appendRoutingShape(NetRoutingGraph& net_routing_graph, RoutingShape routing_shape)
{
  net_routing_graph.get_routing_shape_list().push_back(std::move(routing_shape));
  return static_cast<int32_t>(net_routing_graph.get_routing_shape_list().size()) - 1;
}

void LVSInterface::wrapRoutingDataVia(idb::IdbVia* via, NetRoutingGraph& net_routing_graph)
{
  if (via == nullptr) {
    return;
  }

  idb::IdbLayerShape bottom_shape = via->get_bottom_layer_shape();
  idb::IdbLayerShape top_shape = via->get_top_layer_shape();
  if (bottom_shape.get_layer() == nullptr || top_shape.get_layer() == nullptr || !bottom_shape.get_layer()->is_routing()
      || !top_shape.get_layer()->is_routing()) {
    return;
  }

  int32_t bottom_shape_idx = appendRoutingShape(net_routing_graph, wrapRoutingDataShape(bottom_shape.get_layer(), bottom_shape.get_bounding_box()));
  int32_t top_shape_idx = appendRoutingShape(net_routing_graph, wrapRoutingDataShape(top_shape.get_layer(), top_shape.get_bounding_box()));
  net_routing_graph.get_via_shape_idx_pair_list().emplace_back(bottom_shape_idx, top_shape_idx);
}

void LVSInterface::wrapSpecialNetTerminalData(idb::IdbDesign* design, DefData& def_data)
{
  if (design == nullptr) {
    return;
  }

  PhysicalGraph& physical_graph = def_data.get_physical_graph();
  if (idb::IdbSpecialNetList* special_net_list = design->get_special_net_list(); special_net_list != nullptr) {
    if (design->get_instance_list() != nullptr) {
      size_t instance_num = design->get_instance_list()->get_instance_list().size();
      physical_graph.get_power_instance_pin_net_map().reserve(instance_num);
      physical_graph.get_ground_instance_pin_net_map().reserve(instance_num);
    }
    for (idb::IdbSpecialNet* special_net : special_net_list->get_net_list()) {
      if (special_net == nullptr) {
        continue;
      }
      std::string net_name = special_net->get_net_name();
      int32_t net_id = physical_graph.getNetId(net_name);
      bool is_power_net = special_net->is_vdd();
      bool is_ground_net = special_net->is_vss();
      if (is_power_net) {
        physical_graph.get_power_net_name_set().insert(net_name);
      } else if (is_ground_net) {
        physical_graph.get_ground_net_name_set().insert(net_name);
      }

      std::unordered_set<idb::IdbPin*> special_pin_set;
      std::vector<std::pair<std::string, idb::IdbPin*>> pin_list;
      if (idb::IdbPins* io_pin_list = special_net->get_io_pins(); io_pin_list != nullptr) {
        for (idb::IdbPin* pin : io_pin_list->get_pin_list()) {
          if (pin != nullptr && special_pin_set.insert(pin).second) {
            pin_list.emplace_back(getTerminalName(pin), pin);
          }
        }
      }
      if (idb::IdbPins* instance_pin_list = special_net->get_instance_pin_list(); instance_pin_list != nullptr) {
        for (idb::IdbPin* pin : instance_pin_list->get_pin_list()) {
          if (pin != nullptr && special_pin_set.insert(pin).second) {
            pin_list.emplace_back(getTerminalName(pin), pin);
          }
        }
      }
      if (special_net->has_wildcard_instance_pins() && design->get_instance_list() != nullptr) {
        for (idb::IdbInstance* instance : design->get_instance_list()->get_instance_list()) {
          if (instance == nullptr || instance->get_pin_list() == nullptr) {
            continue;
          }
          for (idb::IdbPin* pin : instance->get_pin_list()->get_pin_list()) {
            if (pin == nullptr || design->findSpecialNetForInstancePin(pin) != special_net || !special_pin_set.insert(pin).second) {
              continue;
            }
            pin_list.emplace_back(getTerminalName(pin), pin);
          }
        }
      }
      std::sort(pin_list.begin(), pin_list.end(), [](const auto& first, const auto& second) { return first.first < second.first; });
      for (auto& [terminal_name, pin] : pin_list) {
        wrapRoutingDataPin(net_name, net_id, terminal_name, pin, is_power_net, is_ground_net, def_data);
      }
    }
  }
}

void LVSInterface::wrapSpecialNetWireData(idb::IdbDesign* design, DefData& def_data)
{
  if (design == nullptr) {
    return;
  }
  PhysicalGraph& physical_graph = def_data.get_physical_graph();
  if (idb::IdbSpecialNetList* special_net_list = design->get_special_net_list(); special_net_list != nullptr) {
    for (idb::IdbSpecialNet* special_net : special_net_list->get_net_list()) {
      if (special_net == nullptr) {
        continue;
      }
      NetRoutingGraph* routing_graph = physical_graph.getNetRoutingGraph(special_net->get_net_name());
      if (routing_graph == nullptr) {
        continue;
      }
      if (idb::IdbSpecialWireList* wire_list = special_net->get_wire_list(); wire_list != nullptr) {
        for (idb::IdbSpecialWire* wire : wire_list->get_wire_list()) {
          if (wire == nullptr) {
            continue;
          }
          for (idb::IdbSpecialWireSegment* segment : wire->get_segment_list()) {
            if (segment == nullptr) {
              continue;
            }
            if (segment->get_layer() != nullptr && segment->get_layer()->is_routing() && segment->is_line()) {
              appendRoutingShape(*routing_graph, wrapRoutingDataShape(
                                                     segment->get_layer(), idb::IdbRect(segment->get_point_start(), segment->get_point_second(),
                                                                                      segment->get_route_width())));
            } else if (segment->get_layer() != nullptr && segment->get_layer()->is_routing() && segment->is_rect() && segment->get_delta_rect() != nullptr) {
              appendRoutingShape(*routing_graph, wrapRoutingDataShape(segment->get_layer(), *segment->get_delta_rect()));
            }
          }
        }
      }
    }
  }
}

void LVSInterface::wrapSpecialNetViaData(idb::IdbDesign* design, DefData& def_data)
{
  if (design == nullptr) {
    return;
  }
  PhysicalGraph& physical_graph = def_data.get_physical_graph();
  if (idb::IdbSpecialNetList* special_net_list = design->get_special_net_list(); special_net_list != nullptr) {
    for (idb::IdbSpecialNet* special_net : special_net_list->get_net_list()) {
      if (special_net == nullptr) {
        continue;
      }
      NetRoutingGraph* routing_graph = physical_graph.getNetRoutingGraph(special_net->get_net_name());
      if (routing_graph == nullptr) {
        continue;
      }
      if (idb::IdbSpecialWireList* wire_list = special_net->get_wire_list(); wire_list != nullptr) {
        for (idb::IdbSpecialWire* wire : wire_list->get_wire_list()) {
          if (wire == nullptr) {
            continue;
          }
          for (idb::IdbSpecialWireSegment* segment : wire->get_segment_list()) {
            if (segment != nullptr && segment->is_via()) {
              wrapRoutingDataVia(segment->get_via(), *routing_graph);
            }
          }
        }
      }
    }
  }
}

Shape LVSInterface::wrapShape(const int32_t layer_idx, idb::IdbRect idb_rect)
{
  Shape shape;
  shape.set_layer_idx(layer_idx);
  shape.set_ll_x(idb_rect.get_low_x());
  shape.set_ll_y(idb_rect.get_low_y());
  shape.set_ur_x(idb_rect.get_high_x());
  shape.set_ur_y(idb_rect.get_high_y());
  return shape;
}

idb::IdbRect LVSInterface::getPhysicalSegmentRect(idb::IdbRegularWireSegment* idb_segment)
{
  // DEF path RECT coordinates are offsets from the preceding path point, resolved by get_segment_rect().
  return idb_segment->get_segment_rect();
}

std::string LVSInterface::getTerminalName(idb::IdbPin* pin)
{
  if (pin == nullptr) {
    return "";
  }
  if (pin->is_io_pin()) {
    return LVSUTIL.getIOName(pin->get_pin_name());
  }
  if (idb::IdbInstance* instance = pin->get_instance(); instance != nullptr) {
    return LVSUTIL.getInstancePinName(instance->get_name(), pin->get_pin_name());
  }
  return LVSUTIL.getIOName(pin->get_pin_name());
}

}  // namespace ilvs
