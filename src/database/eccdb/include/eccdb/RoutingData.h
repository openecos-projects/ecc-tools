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
#pragma once

#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "eccdb/Types.h"

namespace eccdb {

// Routing Data types are owned snapshots. A Wire has identity, while its paths
// and path elements are aggregate values owned by that Wire.
struct ViaRectangle
{
  LayerId layer;
  Rect rectangle;
  uint32_t mask = 0;
};

struct ViaPolygon
{
  LayerId layer;
  std::vector<Point> points;
  uint32_t mask = 0;
};

struct GeneratedViaData
{
  ViaRuleId via_rule;
  RoutingLayerId bottom_layer;
  LayerId cut_layer;
  RoutingLayerId top_layer;
  int32_t cut_size_x = 0;
  int32_t cut_size_y = 0;
  int32_t cut_spacing_x = 0;
  int32_t cut_spacing_y = 0;
  int32_t bottom_enclosure_x = 0;
  int32_t bottom_enclosure_y = 0;
  int32_t top_enclosure_x = 0;
  int32_t top_enclosure_y = 0;
  std::optional<std::pair<uint32_t, uint32_t>> row_column;
  std::optional<Point> origin;
  std::optional<std::pair<Point, Point>> offsets;
  std::optional<std::string> cut_pattern;
};

struct DesignViaData
{
  std::string name;
  std::optional<std::string> pattern_name;
  std::vector<ViaRectangle> rectangles;
  std::vector<ViaPolygon> polygons;
  std::optional<GeneratedViaData> generated;
};

struct WireMetadata
{
  NetId net;
  WireStatus status = WireStatus::kRouted;
  std::string shield_net;
};

struct WirePoint
{
  Point position;
  std::optional<int32_t> extension;
  bool virtual_point = false;
};

struct WireMask
{
  uint32_t top = 0;
  uint32_t cut = 0;
  uint32_t bottom = 0;
};

// A path placement references exactly one LEF technology VIA or DEF design VIA.
using ViaDefinitionId = std::variant<TechViaId, ViaId>;

struct ViaPlacementData
{
  uint32_t point_index = 0;
  ViaDefinitionId definition;
  Orientation orientation = Orientation::kN;
  std::optional<WireMask> mask;
  std::optional<std::pair<uint32_t, uint32_t>> rows_columns;
  int32_t step_x = 0;
  int32_t step_y = 0;
};

struct WireRectangle
{
  uint32_t point_index = 0;
  Rect delta;
};

struct WirePathData
{
  RoutingLayerId layer;
  std::optional<int32_t> width;
  std::optional<uint32_t> mask;
  bool taper = false;
  std::optional<std::string> taper_rule;
  std::optional<std::string> shape;
  std::optional<int32_t> style;
  std::vector<WirePoint> points;
  std::vector<ViaPlacementData> vias;
  std::vector<WireRectangle> rectangles;
};

struct WireRoutingData
{
  std::vector<WirePathData> paths;
};

struct WireSnapshot
{
  WireId id;
  WireMetadata metadata;
  WireRoutingData routing;
};

}  // namespace eccdb
