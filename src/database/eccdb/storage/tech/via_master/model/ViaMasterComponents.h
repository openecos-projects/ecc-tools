// ***************************************************************************************
// SPDX-License-Identifier: MulanPSL-2.0
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

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "common/EnttId.h"
#include "geometry/GeometryEntity.h"
#include "tech/common/TechLayerTypes.h"
#include "tech/via_rule_generate/model/ViaRuleGenerateComponents.h"

namespace eccdb {

struct TechViaMaster;
struct TechViaGeometry;
struct TechGeneratedViaMaster;

// A concrete technology VIA definition is one entity in the shared TechRegistry.
using TechViaMasterId = EnttId<TechEntity, TechViaMaster>;

namespace TechViaMasterFlag {
constexpr uint32_t kDefault = 1u << 0;
constexpr uint32_t kTopOfStackOnly = 1u << 1;
constexpr uint32_t kHasResistance = 1u << 2;
}  // namespace TechViaMasterFlag

namespace TechGeneratedViaMasterFlag {
constexpr uint32_t kHasRowCol = 1u << 0;
constexpr uint32_t kHasOrigin = 1u << 1;
constexpr uint32_t kHasOffset = 1u << 2;
constexpr uint32_t kHasPattern = 1u << 3;
}  // namespace TechGeneratedViaMasterFlag

// LEF 5.8 VIA has two alternatives represented by components on one entity:
//
// Fixed VIA:
//   VIA name [DEFAULT]
//     [RESISTANCE value ;]
//     {LAYER layerName ; {RECT ... ; | POLYGON ... ;} ...} ...
//     [PROPERTY name value ;] ...
//   END name
//
// Generated VIA:
//   VIA name [DEFAULT]
//     VIARULE ruleName ; CUTSIZE x y ; LAYERS bottom cut top ;
//     CUTSPACING x y ; ENCLOSURE bottomX bottomY topX topY ;
//     [ROWCOL rows columns ;] [ORIGIN x y ;] [OFFSET ... ;]
//     [PATTERN pattern ;]
//   END name
// TechViaMaster is the common identity/scalar component. TechViaGeometry is
// always materialized; TechGeneratedViaMaster is present only for generated
// VIAs. Per-shape MASK and VIA PROPERTY are not represented yet.
struct TechViaMaster
{
  std::string name;
  uint32_t flags = 0;
  double resistance = 0.0;
  std::vector<TechProperty> properties;
};

// Materialized payload of the three fixed-VIA LAYER clauses. Every physical
// rectangle/polygon is an independent geometry entity. The vectors preserve
// LEF clause order while the geometry registry owns the payloads.
struct TechViaGeometry
{
  TechConductorLayerRef bottom_layer;
  std::vector<GeometryShapeId> bottom_geometry;

  TechCutLayerId cut_layer;
  std::vector<GeometryShapeId> cut_geometry;

  TechConductorLayerRef top_layer;
  std::vector<GeometryShapeId> top_geometry;

  Rect bounding_box;
};

// Parsed generated-VIA formula. Its presence, rather than a duplicated enum
// field, identifies the generated alternative. Geometry remains materialized
// for DRC/router/UI consumers.
struct TechGeneratedViaMaster
{
  TechViaRuleGenerateId via_rule_generate;

  uint32_t flags = 0;
  int32_t cut_size_x = 0;
  int32_t cut_size_y = 0;
  int32_t cut_spacing_x = 0;
  int32_t cut_spacing_y = 0;
  int32_t bottom_enclosure_x = 0;
  int32_t bottom_enclosure_y = 0;
  int32_t top_enclosure_x = 0;
  int32_t top_enclosure_y = 0;
  uint32_t row_count = 1;
  uint32_t column_count = 1;
  int32_t origin_x = 0;
  int32_t origin_y = 0;
  int32_t bottom_offset_x = 0;
  int32_t bottom_offset_y = 0;
  int32_t top_offset_x = 0;
  int32_t top_offset_y = 0;
  std::string pattern;
};

}  // namespace eccdb
