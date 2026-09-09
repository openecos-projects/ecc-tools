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
#include "api/internal/StorageConversions.h"

#include <stdexcept>
#include <utility>

namespace eccdb::detail {

DesignNet toStorage(const NetData& value)
{
  DesignNet result;
  applyToStorage(value, result);
  return result;
}

void applyToStorage(const NetData& value, DesignNet& result)
{
  if (value.tech_non_default_rule && value.design_non_default_rule) {
    throw std::invalid_argument("a net cannot reference both technology and design non-default rules");
  }

  result.name = value.name;
  result.use = static_cast<DesignSignalUse>(value.use);
  result.source = static_cast<DesignNetSource>(value.source);
  result.flags &= ~(DesignNetFlag::kHasWeight | DesignNetFlag::kHasNonDefaultRule);
  result.weight = 0;
  result.non_default_rule = {};
  result.design_non_default_rule = {};
  if (value.weight) {
    result.flags |= DesignNetFlag::kHasWeight;
    result.weight = *value.weight;
  }
  if (value.tech_non_default_rule) {
    result.flags |= DesignNetFlag::kHasNonDefaultRule;
    result.non_default_rule = toStorageId<TechNonDefaultRuleId>(value.tech_non_default_rule);
  }
  if (value.design_non_default_rule) {
    result.flags |= DesignNetFlag::kHasNonDefaultRule;
    result.design_non_default_rule = toStorageId<DesignNonDefaultRuleId>(value.design_non_default_rule);
  }
}

NetData toApi(const DesignNet& value)
{
  NetData result{.name = value.name,
                 .use = static_cast<SignalUse>(value.use),
                 .source = static_cast<NetSource>(value.source)};
  if ((value.flags & DesignNetFlag::kHasWeight) != 0u) {
    result.weight = value.weight;
  }
  result.tech_non_default_rule = toApiId<TechRuleId>(value.non_default_rule);
  result.design_non_default_rule = toApiId<DesignRuleId>(value.design_non_default_rule);
  return result;
}

DesignNetOptions toStorage(const NetOptions& value)
{
  DesignNetOptions result;
  if (value.original) {
    result.flags |= DesignNetOptionsFlag::kHasOriginal;
    result.original = *value.original;
  }
  if (value.pattern) {
    result.flags |= DesignNetOptionsFlag::kHasPattern;
    result.pattern = static_cast<DesignNetPattern>(*value.pattern);
  }
  if (value.estimated_capacitance) {
    result.flags |= DesignNetOptionsFlag::kHasEstimatedCapacitance;
    result.estimated_capacitance = *value.estimated_capacitance;
  }
  if (value.frequency) {
    result.flags |= DesignNetOptionsFlag::kHasFrequency;
    result.frequency = *value.frequency;
  }
  if (value.xtalk) {
    result.flags |= DesignNetOptionsFlag::kHasXTalk;
    result.xtalk = *value.xtalk;
  }
  if (value.style) {
    result.flags |= DesignNetOptionsFlag::kHasStyle;
    result.style = *value.style;
  }
  if (value.voltage) {
    result.flags |= DesignNetOptionsFlag::kHasVoltage;
    result.voltage = *value.voltage;
  }
  result.spacing_rules.reserve(value.spacing_rules.size());
  for (const auto& source : value.spacing_rules) {
    DesignNetSpacingRule rule{.layer = toStorageId<TechRoutingLayerId>(source.layer), .spacing = source.spacing};
    if (source.range) {
      rule.flags |= DesignNetSpacingRuleFlag::kHasRange;
      rule.range_left = source.range->first;
      rule.range_right = source.range->second;
    }
    result.spacing_rules.push_back(std::move(rule));
  }
  return result;
}

NetOptions toApi(const DesignNetOptions& value)
{
  NetOptions result;
  if ((value.flags & DesignNetOptionsFlag::kHasOriginal) != 0u) result.original = value.original;
  if ((value.flags & DesignNetOptionsFlag::kHasPattern) != 0u) result.pattern = static_cast<NetPattern>(value.pattern);
  if ((value.flags & DesignNetOptionsFlag::kHasEstimatedCapacitance) != 0u) {
    result.estimated_capacitance = value.estimated_capacitance;
  }
  if ((value.flags & DesignNetOptionsFlag::kHasFrequency) != 0u) result.frequency = value.frequency;
  if ((value.flags & DesignNetOptionsFlag::kHasXTalk) != 0u) result.xtalk = value.xtalk;
  if ((value.flags & DesignNetOptionsFlag::kHasStyle) != 0u) result.style = value.style;
  if ((value.flags & DesignNetOptionsFlag::kHasVoltage) != 0u) result.voltage = value.voltage;
  result.spacing_rules.reserve(value.spacing_rules.size());
  for (const auto& source : value.spacing_rules) {
    NetSpacingRule rule{.layer = toApiId<RoutingLayerId>(source.layer), .spacing = source.spacing};
    if ((source.flags & DesignNetSpacingRuleFlag::kHasRange) != 0u) {
      rule.range = std::pair{source.range_left, source.range_right};
    }
    result.spacing_rules.push_back(std::move(rule));
  }
  return result;
}

DesignInstance toStorage(const InstanceData& value)
{
  DesignInstance result;
  applyToStorage(value, result);
  return result;
}

void applyToStorage(const InstanceData& value, DesignInstance& result)
{
  result.name = value.name;
  result.master = toStorageId<LibraryCellMasterId>(value.master);
  result.origin = value.origin;
  result.orientation = static_cast<DesignOrientation>(value.orientation);
  result.placement_status = static_cast<DesignPlacementStatus>(value.placement_status);
  result.source = static_cast<DesignInstanceSource>(value.source);
}

InstanceData toApi(const DesignInstance& value)
{
  return InstanceData{.name = value.name,
                      .master = toApiId<CellMasterId>(value.master),
                      .origin = value.origin,
                      .orientation = static_cast<Orientation>(value.orientation),
                      .placement_status = static_cast<PlacementStatus>(value.placement_status),
                      .source = static_cast<InstanceSource>(value.source)};
}

InstancePinData toApi(const DesignInstancePin& value)
{
  return InstancePinData{.instance = toApiId<InstanceId>(value.instance),
                         .master_term = toApiId<MasterTermId>(value.master_term),
                         .net = toApiId<NetId>(value.net),
                         .special_net = toApiId<NetId>(value.special_net)};
}

DesignIoPin toStorage(const IoPinData& value)
{
  DesignIoPin result;
  applyToStorage(value, result);
  return result;
}

void applyToStorage(const IoPinData& value, DesignIoPin& result)
{
  result.name = value.name;
  result.direction = static_cast<DesignIoPinDirection>(value.direction);
  result.use = static_cast<DesignSignalUse>(value.use);
  result.net = toStorageId<DesignNetId>(value.net);
  result.special_net = toStorageId<DesignNetId>(value.special_net);
}

IoPinData toApi(const DesignIoPin& value)
{
  return IoPinData{.name = value.name,
                   .direction = static_cast<IoDirection>(value.direction),
                   .use = static_cast<SignalUse>(value.use),
                   .net = toApiId<NetId>(value.net),
                   .special_net = toApiId<NetId>(value.special_net)};
}

DesignVia toStorage(const DesignViaData& value)
{
  DesignVia result{.name = value.name};
  if (value.pattern_name) {
    result.flags |= DesignViaFlag::kHasPatternName;
    result.pattern_name = *value.pattern_name;
  }
  result.rectangles.reserve(value.rectangles.size());
  for (const auto& source : value.rectangles) {
    result.rectangles.push_back(
        {.layer = toStorageId<TechLayerId>(source.layer), .rectangle = source.rectangle, .mask = source.mask});
  }
  result.polygons.reserve(value.polygons.size());
  for (const auto& source : value.polygons) {
    result.polygons.push_back(
        {.layer = toStorageId<TechLayerId>(source.layer), .points = source.points, .mask = source.mask});
  }
  if (value.generated) {
    result.flags |= DesignViaFlag::kGenerated;
    const auto& source = *value.generated;
    auto& generated = result.generated;
    generated.via_rule = toStorageId<TechViaRuleGenerateId>(source.via_rule);
    generated.bottom_layer = toStorageId<TechRoutingLayerId>(source.bottom_layer);
    generated.cut_layer = toStorageId<TechCutLayerId>(source.cut_layer);
    generated.top_layer = toStorageId<TechRoutingLayerId>(source.top_layer);
    generated.cut_size_x = source.cut_size_x;
    generated.cut_size_y = source.cut_size_y;
    generated.cut_spacing_x = source.cut_spacing_x;
    generated.cut_spacing_y = source.cut_spacing_y;
    generated.bottom_enclosure_x = source.bottom_enclosure_x;
    generated.bottom_enclosure_y = source.bottom_enclosure_y;
    generated.top_enclosure_x = source.top_enclosure_x;
    generated.top_enclosure_y = source.top_enclosure_y;
    if (source.row_column) {
      generated.flags |= DesignGeneratedViaFlag::kHasRowCol;
      generated.row_count = source.row_column->first;
      generated.column_count = source.row_column->second;
    }
    if (source.origin) {
      generated.flags |= DesignGeneratedViaFlag::kHasOrigin;
      generated.origin = *source.origin;
    }
    if (source.offsets) {
      generated.flags |= DesignGeneratedViaFlag::kHasOffset;
      generated.bottom_offset = source.offsets->first;
      generated.top_offset = source.offsets->second;
    }
    if (source.cut_pattern) {
      generated.flags |= DesignGeneratedViaFlag::kHasCutPattern;
      generated.cut_pattern = *source.cut_pattern;
    }
  }
  return result;
}

DesignViaData toApi(const DesignVia& value)
{
  DesignViaData result{.name = value.name};
  if ((value.flags & DesignViaFlag::kHasPatternName) != 0u) result.pattern_name = value.pattern_name;
  result.rectangles.reserve(value.rectangles.size());
  for (const auto& source : value.rectangles) {
    result.rectangles.push_back(
        {.layer = toApiId<LayerId>(source.layer), .rectangle = source.rectangle, .mask = source.mask});
  }
  result.polygons.reserve(value.polygons.size());
  for (const auto& source : value.polygons) {
    result.polygons.push_back({.layer = toApiId<LayerId>(source.layer), .points = source.points, .mask = source.mask});
  }
  if ((value.flags & DesignViaFlag::kGenerated) != 0u) {
    const auto& source = value.generated;
    GeneratedViaData generated{.via_rule = toApiId<ViaRuleId>(source.via_rule),
                           .bottom_layer = toApiId<RoutingLayerId>(source.bottom_layer),
                           .cut_layer = toApiId<LayerId>(source.cut_layer),
                           .top_layer = toApiId<RoutingLayerId>(source.top_layer),
                           .cut_size_x = source.cut_size_x,
                           .cut_size_y = source.cut_size_y,
                           .cut_spacing_x = source.cut_spacing_x,
                           .cut_spacing_y = source.cut_spacing_y,
                           .bottom_enclosure_x = source.bottom_enclosure_x,
                           .bottom_enclosure_y = source.bottom_enclosure_y,
                           .top_enclosure_x = source.top_enclosure_x,
                           .top_enclosure_y = source.top_enclosure_y};
    if ((source.flags & DesignGeneratedViaFlag::kHasRowCol) != 0u) {
      generated.row_column = std::pair{source.row_count, source.column_count};
    }
    if ((source.flags & DesignGeneratedViaFlag::kHasOrigin) != 0u) generated.origin = source.origin;
    if ((source.flags & DesignGeneratedViaFlag::kHasOffset) != 0u) {
      generated.offsets = std::pair{source.bottom_offset, source.top_offset};
    }
    if ((source.flags & DesignGeneratedViaFlag::kHasCutPattern) != 0u) generated.cut_pattern = source.cut_pattern;
    result.generated = std::move(generated);
  }
  return result;
}

DesignWire toStorage(const WireMetadata& value)
{
  return DesignWire{.net = toStorageId<DesignNetId>(value.net),
                    .status = static_cast<DesignWireStatus>(value.status),
                    .shield_net = value.shield_net};
}

WireMetadata toApi(const DesignWire& value)
{
  return WireMetadata{.net = toApiId<NetId>(value.net),
                      .status = static_cast<WireStatus>(value.status),
                      .shield_net = value.shield_net};
}

DesignWireRoutingInput toStorage(const WireRoutingData& value)
{
  DesignWireRoutingInput result;
  result.reservePaths(value.paths.size());
  for (const auto& source : value.paths) {
    DesignWirePath path{.layer = toStorageId<TechRoutingLayerId>(source.layer)};
    if (source.width) {
      path.flags |= DesignWirePathFlag::kHasWidth;
      path.width = *source.width;
    }
    if (source.mask) {
      path.flags |= DesignWirePathFlag::kHasMask;
      path.mask = *source.mask;
    }
    if (source.taper) path.flags |= DesignWirePathFlag::kTaper;
    if (source.taper_rule) {
      path.flags |= DesignWirePathFlag::kHasTaperRule;
      path.taper_rule = *source.taper_rule;
    }
    if (source.shape) {
      path.flags |= DesignWirePathFlag::kHasShape;
      path.shape = *source.shape;
    }
    if (source.style) {
      path.flags |= DesignWirePathFlag::kHasStyle;
      path.style = *source.style;
    }
    path.points.reserve(source.points.size());
    for (const auto& point : source.points) {
      DesignWirePoint target{.position = point.position};
      if (point.extension) {
        target.flags |= DesignWirePointFlag::kHasExtension;
        target.extension = *point.extension;
      }
      if (point.virtual_point) target.flags |= DesignWirePointFlag::kVirtual;
      path.points.push_back(target);
    }
    path.vias.reserve(source.vias.size());
    for (const auto& via : source.vias) {
      DesignWireVia target{.point_index = via.point_index,
                           .orientation = static_cast<DesignOrientation>(via.orientation),
                           .step_x = via.step_x,
                           .step_y = via.step_y};
      if (const auto* tech_via = std::get_if<TechViaId>(&via.definition)) {
        if (!*tech_via) {
          throw std::invalid_argument("wire path references an invalid technology via");
        }
        target.tech_via = toStorageId<TechViaMasterId>(*tech_via);
      } else {
        const auto design_via = std::get<ViaId>(via.definition);
        if (!design_via) {
          throw std::invalid_argument("wire path references an invalid design via");
        }
        target.design_via = toStorageId<DesignViaId>(design_via);
      }
      if (via.mask) {
        target.flags |= DesignWireViaFlag::kHasMask;
        target.top_mask = via.mask->top;
        target.cut_mask = via.mask->cut;
        target.bottom_mask = via.mask->bottom;
      }
      if (via.rows_columns) {
        target.flags |= DesignWireViaFlag::kHasArray;
        target.rows = via.rows_columns->first;
        target.columns = via.rows_columns->second;
      }
      path.vias.push_back(target);
    }
    path.rectangles.reserve(source.rectangles.size());
    for (const auto& rectangle : source.rectangles) {
      path.rectangles.push_back({.point_index = rectangle.point_index, .delta = rectangle.delta});
    }
    result.appendPath(std::move(path));
  }
  return result;
}

WirePathData toApi(const DesignWirePathView& value)
{
  WirePathData result{.layer = toApiId<RoutingLayerId>(value.layer())};
  if ((value.flags() & DesignWirePathFlag::kHasWidth) != 0u) result.width = value.width();
  if ((value.flags() & DesignWirePathFlag::kHasMask) != 0u) result.mask = value.mask();
  result.taper = (value.flags() & DesignWirePathFlag::kTaper) != 0u;
  if ((value.flags() & DesignWirePathFlag::kHasTaperRule) != 0u) result.taper_rule = value.taperRule();
  if ((value.flags() & DesignWirePathFlag::kHasShape) != 0u) result.shape = value.shape();
  if ((value.flags() & DesignWirePathFlag::kHasStyle) != 0u) result.style = value.style();
  for (const auto& point : value.points()) {
    WirePoint target{.position = point.position,
                     .virtual_point = (point.flags & DesignWirePointFlag::kVirtual) != 0u};
    if ((point.flags & DesignWirePointFlag::kHasExtension) != 0u) target.extension = point.extension;
    result.points.push_back(target);
  }
  for (const auto& via : value.vias()) {
    const bool has_tech_via = static_cast<bool>(via.tech_via);
    const bool has_design_via = static_cast<bool>(via.design_via);
    if (has_tech_via == has_design_via) {
      throw std::logic_error("stored wire path must reference exactly one via definition");
    }
    ViaPlacementData target{.point_index = via.point_index,
                            .definition = has_tech_via
                                              ? ViaDefinitionId{toApiId<TechViaId>(via.tech_via)}
                                              : ViaDefinitionId{toApiId<ViaId>(via.design_via)},
                            .orientation = static_cast<Orientation>(via.orientation),
                            .step_x = via.step_x,
                            .step_y = via.step_y};
    if ((via.flags & DesignWireViaFlag::kHasMask) != 0u) {
      target.mask = WireMask{.top = via.top_mask, .cut = via.cut_mask, .bottom = via.bottom_mask};
    }
    if ((via.flags & DesignWireViaFlag::kHasArray) != 0u) {
      target.rows_columns = std::pair{via.rows, via.columns};
    }
    result.vias.push_back(target);
  }
  for (const auto& rectangle : value.rectangles()) {
    result.rectangles.push_back({.point_index = rectangle.point_index, .delta = rectangle.delta});
  }
  return result;
}

}  // namespace eccdb::detail
