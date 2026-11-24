
#include "ProcessBinding.hpp"

#include <set>
#include <stdexcept>

#include "LayoutData.hpp"
#include "Logger.hpp"

namespace ircx {
void validateProcessBindings(const std::vector<CornerData>& corners, LayerTable& layers, LayoutData& layout)
{
  std::set<std::string> external_conductors;
  for (const auto& [index, routing] : layout.get_routing_layer_map()) {
    if (const auto* name = findProcessLayerName(layers, index)) external_conductors.insert(*name);
  }
  for (const auto& corner : corners) {
    auto conductors = external_conductors;
    for (const auto& conductor : corner.get_process_conductor_list()) conductors.insert(conductor.get_layer_name());
    // A declared cut layer cannot serve as a conductor, even under an incorrect mapping.
    for (const auto& via : corner.get_process_via_list()) conductors.erase(via.get_layer_name());
    for (const auto& via : corner.get_process_via_list()) {
      // Unmapped process contacts remain available without becoming design layers.
      if (!layers.get_process_name_to_design_name_map().contains(via.get_layer_name()))
        continue;
      const auto& from = via.get_from_layer_name();
      const auto& to = via.get_to_layer_name();
      const auto context = corner.get_process_technology()->source_name + ": via " + via.get_layer_name() + ": ";
      if (!from.empty() && from == to)
        RCXLOG.error(Loc::current(), context + "connected layers must be distinct");
      for (const auto* name : {&from, &to}) {
        if (!name->empty() && !conductors.contains(*name))
          RCXLOG.error(Loc::current(), context + "unknown connected conductor " + *name);
      }
    }
  }
}
}  // namespace ircx
