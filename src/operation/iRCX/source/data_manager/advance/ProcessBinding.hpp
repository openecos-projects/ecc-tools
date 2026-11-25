#pragma once

#include "CornerData.hpp"
#include "LayerTable.hpp"

namespace ircx {
class LayoutData;
void validateProcessBindings(const std::vector<CornerData>& corners, LayerTable& layers, LayoutData& layout);

// Design-layer binding belongs to the database; process parsers only know names.
inline const std::string* findProcessLayerName(LayerTable& layers, int32_t design_index)
{
  const auto& names = layers.get_design_idx_to_name_map();
  const auto design = names.find(design_index);
  if (design == names.end()) return nullptr;
  const auto& mapping = layers.get_design_name_to_process_name_map();
  const auto process = mapping.find(design->second);
  return process == mapping.end() ? nullptr : &process->second;
}
inline const ProcessConductor* findProcessConductor(const CornerData& corner, LayerTable& layers, int32_t design_index)
{
  const auto* name = findProcessLayerName(layers, design_index);
  if (name) for (const auto& conductor : corner.get_process_conductor_list())
    if (conductor.get_layer_name() == *name) return &conductor;
  return nullptr;
}
inline const ProcessVia* findProcessVia(const CornerData& corner, LayerTable& layers, int32_t design_index)
{
  const auto* name = findProcessLayerName(layers, design_index);
  if (name) for (const auto& via : corner.get_process_via_list())
    if (via.get_layer_name() == *name) return &via;
  return nullptr;
}
}  // namespace ircx
