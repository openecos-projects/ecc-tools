#pragma once

#include <cstdint>

#include "tech/common/TechLayerTypes.h"

namespace eccdb {

// PROPERTY LEF58_TYPE classification for TYPE MASTERSLICE.
enum class TechMastersliceType : uint8_t
{
  kNone,
  kNWell,
  kPWell,
  kAboveDieEdge,
  kBelowDieEdge,
  kDiffusion,
  kTrimPoly,
  kTrimMetal,
  kRegion
};

// LEF 5.8 MASTERSLICE layer and represented LEF58 subtype:
//   LAYER name
//     TYPE MASTERSLICE ;
//     [PROPERTY LEF58_TYPE "TYPE subtype ;" ;]
//     [PROPERTY LEF58_TRIMMEDMETAL "TRIMMEDMETAL metal [MASK mask] ;" ;]
//   END name
// This component is attached to the same entity as TechLayerInfo.
struct TechMastersliceLayer
{
  TechMastersliceType subtype = TechMastersliceType::kNone;
};

namespace TechTrimmedMetalRuleFlag {
constexpr uint32_t kHasMask = 1u << 0;
}  // namespace TechTrimmedMetalRuleFlag

// Parsed payload of:
//   PROPERTY LEF58_TRIMMEDMETAL
//     "TRIMMEDMETAL routingLayer [MASK mask] ;" ;
// It is an optional component on the MASTERSLICE entity, not a child entity.
struct TechTrimmedMetalRule
{
  uint32_t flags = 0;
  TechRoutingLayerId metal_layer;
  uint32_t mask = 0;
};

}  // namespace eccdb
