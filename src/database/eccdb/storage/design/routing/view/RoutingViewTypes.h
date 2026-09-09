#pragma once

#include <cstdint>
#include <string>

#include "common/GeometryTypes.h"
#include "design/common/DesignTypes.h"
#include "tech/common/TechLayerIds.h"
#include "tech/via_master/model/ViaMasterComponents.h"

namespace eccdb {

namespace DesignWirePointFlag {
constexpr uint32_t kHasExtension = 1u << 0;
constexpr uint32_t kVirtual = 1u << 1;
}  // namespace DesignWirePointFlag

struct DesignWirePoint
{
  Point position;
  uint32_t flags = 0;
  int32_t extension = 0;
};

// A path via is anchored to one concrete point. Exactly one of tech_via and
// design_via is populated.
struct DesignWireVia
{
  uint32_t point_index = 0;
  TechViaMasterId tech_via;
  DesignViaId design_via;
  DesignOrientation orientation = DesignOrientation::kN;
  uint32_t flags = 0;
  uint32_t top_mask = 0;
  uint32_t cut_mask = 0;
  uint32_t bottom_mask = 0;
  uint32_t rows = 1;
  uint32_t columns = 1;
  int32_t step_x = 0;
  int32_t step_y = 0;
};

namespace DesignWireViaFlag {
constexpr uint32_t kHasMask = 1u << 0;
constexpr uint32_t kHasArray = 1u << 1;
}  // namespace DesignWireViaFlag

// DEF path RECT is relative to one path point and is independent of a VIA.
struct DesignWireRectangle
{
  uint32_t point_index = 0;
  Rect delta;
};

namespace DesignWirePathFlag {
constexpr uint32_t kHasWidth = 1u << 0;
constexpr uint32_t kHasMask = 1u << 1;
constexpr uint32_t kTaper = 1u << 2;
constexpr uint32_t kHasTaperRule = 1u << 3;
constexpr uint32_t kHasShape = 1u << 4;
constexpr uint32_t kHasStyle = 1u << 5;
}  // namespace DesignWirePathFlag

// Value-bearing optional path fields are cold relative to layer, flags, and
// primitive ranges. TAPER itself is flag-only and needs no extra record.
struct DesignWirePathExtra
{
  int32_t width = 0;
  uint32_t mask = 0;
  std::string taper_rule;
  std::string shape;
  int32_t style = 0;
};

}  // namespace eccdb
