#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "common/GeometryTypes.h"
#include "design/common/DesignTypes.h"
#include "tech/common/TechLayerIds.h"

namespace eccdb {

enum class DesignRegionType : uint8_t
{
  kFence,
  kGuide
};

struct DesignRegion
{
  std::string name;
  DesignRegionType type = DesignRegionType::kFence;
  std::vector<Rect> rectangles;
};

namespace DesignGroupFlag {
constexpr uint32_t kHasRegion = 1u << 0;
}  // namespace DesignGroupFlag

struct DesignGroup
{
  std::string name;
  uint32_t flags = 0;
  DesignRegionId region;
  std::vector<DesignInstanceId> instances;
};

enum class DesignBlockageKind : uint8_t
{
  kPlacement,
  kRouting
};

namespace DesignBlockageFlag {
constexpr uint32_t kHasLayer = 1u << 0;
constexpr uint32_t kSoft = 1u << 1;
constexpr uint32_t kPushdown = 1u << 2;
constexpr uint32_t kHasPartial = 1u << 3;
constexpr uint32_t kHasComponent = 1u << 4;
constexpr uint32_t kSlots = 1u << 5;
constexpr uint32_t kFills = 1u << 6;
constexpr uint32_t kExceptPgNet = 1u << 7;
constexpr uint32_t kHasSpacing = 1u << 8;
constexpr uint32_t kHasDesignRuleWidth = 1u << 9;
constexpr uint32_t kHasMask = 1u << 10;
}  // namespace DesignBlockageFlag

struct DesignBlockage
{
  DesignBlockageKind kind = DesignBlockageKind::kPlacement;
  uint32_t flags = 0;
  TechRoutingLayerId layer;
  DesignInstanceId component;
  double partial = 0.0;
  int32_t spacing = 0;
  int32_t design_rule_width = 0;
  uint32_t mask = 0;
  std::vector<Rect> rectangles;
  std::vector<std::vector<Point>> polygons;
};

}  // namespace eccdb
