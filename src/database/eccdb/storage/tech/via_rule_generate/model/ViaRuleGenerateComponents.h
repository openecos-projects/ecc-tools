#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "common/EnttId.h"
#include "common/GeometryTypes.h"
#include "tech/common/TechLayerTypes.h"
#include "tech/routing_layer/model/RoutingLayerComponents.h"

namespace eccdb {

struct TechViaRuleGenerate;
struct TechViaRuleGenerateBottomLayer;
struct TechViaRuleGenerateCutLayer;
struct TechViaRuleGenerateTopLayer;

// VIARULE ... GENERATE is one complete technology object. Its three mandatory
// LAYER clauses share the rule entity rather than receiving child entity IDs.
using TechViaRuleGenerateId = EnttId<TechEntity, TechViaRuleGenerate>;

namespace TechViaRuleGenerateFlag {
constexpr uint32_t kDefault = 1u << 0;
}  // namespace TechViaRuleGenerateFlag

namespace TechViaRuleGenerateRoutingLayerFlag {
constexpr uint32_t kHasDirection = 1u << 0;
constexpr uint32_t kHasEnclosure = 1u << 1;
constexpr uint32_t kHasWidth = 1u << 2;
constexpr uint32_t kHasOverhang = 1u << 3;
constexpr uint32_t kHasMetalOverhang = 1u << 4;
}  // namespace TechViaRuleGenerateRoutingLayerFlag

namespace TechViaRuleGenerateCutLayerFlag {
constexpr uint32_t kHasRect = 1u << 0;
constexpr uint32_t kHasSpacing = 1u << 1;
constexpr uint32_t kHasResistance = 1u << 2;
}  // namespace TechViaRuleGenerateCutLayerFlag

// LEF 5.8 generated VIARULE represented by four components on one entity:
//   VIARULE name GENERATE [DEFAULT]
//     LAYER bottom-conductor ;
//       ENCLOSURE overhang1 overhang2 ; [WIDTH min TO max ;]
//     LAYER top-conductor ;
//       ENCLOSURE overhang1 overhang2 ; [WIDTH min TO max ;]
//     LAYER cut ;
//       RECT lowerLeft upperRight ; SPACING x BY y ;
//       [RESISTANCE resistancePerCut ;]
//   END name
struct TechViaRuleGenerate
{
  std::string name;
  uint32_t flags = 0;
  std::vector<TechProperty> properties;

  [[nodiscard]] bool isDefault() const noexcept { return (flags & TechViaRuleGenerateFlag::kDefault) != 0u; }
};

// Bottom and top clauses have the same fields but remain distinct component
// types so EnTT can attach both to the same VIARULE entity.
struct TechViaRuleGenerateBottomLayer
{
  TechConductorLayerRef layer;
  uint32_t flags = 0;
  TechRoutingDirection direction = TechRoutingDirection::kUnknown;
  int32_t enclosure_overhang1 = 0;
  int32_t enclosure_overhang2 = 0;
  int32_t min_width = 0;
  int32_t max_width = 0;
  int32_t overhang = 0;
  int32_t metal_overhang = 0;
};

struct TechViaRuleGenerateCutLayer
{
  TechCutLayerId layer;
  uint32_t flags = 0;
  Rect cut_rect;
  int32_t spacing_x = 0;
  int32_t spacing_y = 0;
  double resistance_per_cut = 0.0;
};

struct TechViaRuleGenerateTopLayer
{
  TechConductorLayerRef layer;
  uint32_t flags = 0;
  TechRoutingDirection direction = TechRoutingDirection::kUnknown;
  int32_t enclosure_overhang1 = 0;
  int32_t enclosure_overhang2 = 0;
  int32_t min_width = 0;
  int32_t max_width = 0;
  int32_t overhang = 0;
  int32_t metal_overhang = 0;
};

}  // namespace eccdb
