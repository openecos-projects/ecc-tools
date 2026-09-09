#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "tech/common/TechLayerIds.h"

namespace eccdb {

enum class TechConductorLayerKind : uint8_t
{
  kNone,
  kRouting,
  kMasterslice
};

// Fixed VIA conductors are limited by LEF to ROUTING or MASTERSLICE layers.
struct TechConductorLayerRef
{
  TechConductorLayerKind kind = TechConductorLayerKind::kNone;
  TechEntity entity = entt::null;

  TechConductorLayerRef() noexcept = default;
  TechConductorLayerRef(TechRoutingLayerId id) noexcept : kind(TechConductorLayerKind::kRouting), entity(id.entity()) {}
  TechConductorLayerRef(TechMastersliceLayerId id) noexcept : kind(TechConductorLayerKind::kMasterslice), entity(id.entity()) {}

  [[nodiscard]] explicit operator bool() const noexcept { return entity != entt::null; }
  [[nodiscard]] TechLayerId layer() const noexcept { return TechLayerId{entity}; }
};

[[nodiscard]] inline bool operator==(TechConductorLayerRef lhs, TechConductorLayerRef rhs) noexcept
{
  return lhs.kind == rhs.kind && lhs.entity == rhs.entity;
}

[[nodiscard]] inline bool operator!=(TechConductorLayerRef lhs, TechConductorLayerRef rhs) noexcept
{
  return !(lhs == rhs);
}

// Bottom-to-top order of LEF LAYER statements, stored once on TechRoot. This is
// database metadata used for stack validation; it is not a separate LEF clause.
struct TechLayerSequence
{
  std::vector<TechLayerId> layers;
};

enum class TechLef58LayerType : uint8_t
{
  kNone,
  kPolyRouting,
  kMimCap,
  kTsv,
  kPassivation,
  kTrimPoly,
  kNWell,
  kPWell,
  kBelowDieEdge,
  kAboveDieEdge,
  kDiffusion,
  kTrimMetal,
  kMeol,
  kPadMetal,
  kTsvMetal,
  kStackedMimCap,
  kSpecialCut,
  kWellDistance,
  kCpode,
  kHighR,
  kRegion,
  kRcBlockage,
  kAbutFiller,
  kAbutLogic,
};

namespace TechLayerInfoFlag {
constexpr uint32_t kLef58Backside = 1u << 0;
}  // namespace TechLayerInfoFlag

[[nodiscard]] inline std::optional<uint32_t> techLayerPosition(const TechLayerSequence& sequence, TechEntity entity) noexcept
{
  for (uint32_t index = 0; index < sequence.layers.size(); ++index) {
    if (sequence.layers[index].entity() == entity) {
      return index;
    }
  }
  return std::nullopt;
}

[[nodiscard]] inline bool techLayerSequenceContains(const TechLayerSequence& sequence, TechEntity entity) noexcept
{
  return techLayerPosition(sequence, entity).has_value();
}

[[nodiscard]] inline bool techLayerIsBelow(const TechLayerSequence& sequence, TechEntity lower, TechEntity upper) noexcept
{
  const auto lower_position = techLayerPosition(sequence, lower);
  const auto upper_position = techLayerPosition(sequence, upper);
  return lower_position.has_value() && upper_position.has_value() && *lower_position < *upper_position;
}

// Common prefix of every LEF technology layer object:
//   LAYER name
//     TYPE layerType ;
//     [MASK maskCount ;]
//     ... type-specific clauses ...
//   END name
// The type is represented by the presence of exactly one layer component
// (TechRoutingLayer, TechCutLayer, and so on), not by a duplicated enum field.
struct TechLayerInfo
{
  std::string name;
  uint32_t mask_count = 1;
  uint32_t flags = 0;
  TechLef58LayerType lef58_type = TechLef58LayerType::kNone;
};

// Generic LEF property occurrence:
//   PROPERTY propertyName propertyValue ;
struct TechProperty
{
  std::string name;
  std::string value;
};

struct TechLayerProperties
{
  std::vector<TechProperty> values;
};

}  // namespace eccdb
