#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "common/GeometryTypes.h"
#include "tech/common/TechLayerIds.h"
#include "tech/via_rule_generate/model/ViaRuleGenerateComponents.h"

namespace eccdb {

struct DesignViaRectangle
{
  TechLayerId layer;
  Rect rectangle;
  uint32_t mask = 0;
};

struct DesignViaPolygon
{
  TechLayerId layer;
  std::vector<Point> points;
  uint32_t mask = 0;
};

namespace DesignGeneratedViaFlag {
constexpr uint32_t kHasRowCol = 1u << 0;
constexpr uint32_t kHasOrigin = 1u << 1;
constexpr uint32_t kHasOffset = 1u << 2;
constexpr uint32_t kHasCutPattern = 1u << 3;
}  // namespace DesignGeneratedViaFlag

struct DesignGeneratedVia
{
  TechViaRuleGenerateId via_rule;
  TechRoutingLayerId bottom_layer;
  TechCutLayerId cut_layer;
  TechRoutingLayerId top_layer;
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
  Point origin;
  Point bottom_offset;
  Point top_offset;
  std::string cut_pattern;
};

namespace DesignViaFlag {
constexpr uint32_t kGenerated = 1u << 0;
constexpr uint32_t kHasPatternName = 1u << 1;
}  // namespace DesignViaFlag

// DEF VIAS fixed geometry and VIARULE-generated alternatives share one
// DesignViaId-backed EnTT component.
struct DesignVia
{
  std::string name;
  uint32_t flags = 0;
  std::string pattern_name;
  std::vector<DesignViaRectangle> rectangles;
  std::vector<DesignViaPolygon> polygons;
  DesignGeneratedVia generated;
};

}  // namespace eccdb
