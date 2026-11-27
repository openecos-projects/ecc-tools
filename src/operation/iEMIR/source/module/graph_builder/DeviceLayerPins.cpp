// SPDX-License-Identifier: MulanPSL-2.0
#include "DeviceLayerPins.hpp"

namespace iemir {

void connectDeviceLayerPins(PowerGraph& graph, PowerNet& net)
{
  for (const auto& [key, has_geometry] : net.get_pin_has_geometry()) {
    if (has_geometry) {
      continue;  // Never repair a disconnected metal pin by aliasing it.
    }
    auto contacts = graph.get_instance_node_id_list_map().find(key.first);
    if (contacts == graph.get_instance_node_id_list_map().end()) {
      continue;
    }
    // A geometry-free device-layer pin is served through this instance's
    // physical contacts only when DEF explicitly connects both to THIS net.
    graph.get_instance_pin_node_id_list_map()[key] = contacts->second;
    graph.get_instance_pin_node_weight_map()[key] = graph.get_instance_node_weight_map()[key.first];
  }
}

}  // namespace iemir
