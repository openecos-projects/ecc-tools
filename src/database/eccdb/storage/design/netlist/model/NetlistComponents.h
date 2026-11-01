// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <entt/container/dense_map.hpp>

#include "tech/common/AntennaRuleTypes.h"
#include "common/GeometryTypes.h"
#include "design/common/DesignTypes.h"
#include "geometry/GeometryEntity.h"
#include "index/NameHash.h"
#include "library/common/LibraryTypes.h"
#include "tech/common/TechLayerIds.h"
#include "tech/non_default_rule/model/NonDefaultRuleComponents.h"

namespace eccdb {

enum class DesignPlacementStatus : uint8_t
{
  kUnplaced,
  kPlaced,
  kFixed,
  kCover
};

enum class DesignInstanceSource : uint8_t
{
  kNone,
  kNetlist,
  kDist,
  kUser,
  kTiming
};

namespace DesignInstanceFlag {
constexpr uint32_t kHasWeight = 1u << 0;
constexpr uint32_t kHasRegion = 1u << 1;
constexpr uint32_t kHasRegionBounds = 1u << 2;
constexpr uint32_t kHasHalo = 1u << 3;
constexpr uint32_t kHaloSoft = 1u << 4;
constexpr uint32_t kHasRouteHalo = 1u << 5;
}  // namespace DesignInstanceFlag

struct DesignInstanceHalo
{
  int32_t left = 0;
  int32_t bottom = 0;
  int32_t right = 0;
  int32_t top = 0;
};

struct DesignInstanceRouteHalo
{
  int32_t distance = 0;
  TechRoutingLayerId min_layer;
  TechRoutingLayerId max_layer;
};

// DEF COMPONENT / OpenDB dbInst. Instance terminals are materialized from the
// referenced LibraryCellMaster terms and are tracked in DesignInstancePins.
struct DesignInstance
{
  // Instance terminals cache this address; do not sort or compact this storage.
  static constexpr bool in_place_delete = true;

  std::string name;
  LibraryCellMasterId master;
  Point origin;
  DesignOrientation orientation = DesignOrientation::kN;
  DesignPlacementStatus placement_status = DesignPlacementStatus::kUnplaced;
  DesignInstanceSource source = DesignInstanceSource::kNone;
  uint32_t flags = 0;
  int32_t weight = 0;
  DesignRegionId region;
  // DEF COMPONENT inline REGION bounds. The owner relation preserves DEF
  // order; each rectangle itself lives in GeometryRegistry.
  std::vector<GeometryShapeId> region_bounds;
  DesignInstanceHalo halo;
  DesignInstanceRouteHalo route_halo;
};

// DEF (component pin) / OpenDB dbITerm. Every instance term is a distinct
// DesignRegistry entity. Its name, direction and use remain canonical in the
// referenced LibraryMasterTerm (dbMTerm).
struct DesignInstanceTerm
{
  // Callers retain pointers to term components while other instances are
  // edited. Stable storage keeps those pointers valid until this term itself
  // is destroyed.
  static constexpr bool in_place_delete = true;

  DesignInstanceId instance;
  // Runtime-only; DesignInstance uses in-place deletion for address stability.
  const DesignInstance* instance_ptr = nullptr;
  LibraryMasterTermId master_term;
  // Runtime-only; Library terms must not be removed or reordered while referenced.
  const LibraryMasterTerm* master_term_ptr = nullptr;
  DesignNetId net;
  DesignNetId special_net;
};

struct DesignInstanceTerms
{
  // Ordered dbITerm IDs owned by this dbInst, preserving LibraryMaster term
  // order for existing span APIs.
  std::vector<DesignInstanceTermId> values;
  // Per-instance dbMTerm-name lookup. Values reference the independent term
  // entities above; this relation deliberately stores no duplicate payload.
  entt::dense_map<std::string, DesignInstanceTermId, index::TransparentStringHash, std::equal_to<>> names;
};

enum class DesignSignalUse : uint8_t
{
  kNone,
  kSignal,
  kAnalog,
  kPower,
  kGround,
  kClock,
  kTieOff,
  kScan,
  kReset
};

enum class DesignIoPinDirection : uint8_t
{
  kNone,
  kInput,
  kOutput,
  kInOut,
  kFeedThru
};

namespace DesignIoPinFlag {
constexpr uint32_t kSpecial = 1u << 0;
}  // namespace DesignIoPinFlag

namespace DesignPinShapeFlag {
constexpr uint32_t kHasMask = 1u << 0;
constexpr uint32_t kHasSpacing = 1u << 1;
constexpr uint32_t kHasDesignRuleWidth = 1u << 2;
}  // namespace DesignPinShapeFlag

struct DesignPinRectangle
{
  TechLayerId layer;
  Rect rectangle;
  uint32_t flags = 0;
  uint32_t mask = 0;
  int32_t spacing = 0;
  int32_t design_rule_width = 0;
};

struct DesignPinPolygon
{
  TechLayerId layer;
  std::vector<Point> points;
  uint32_t flags = 0;
  uint32_t mask = 0;
  int32_t spacing = 0;
  int32_t design_rule_width = 0;
};

namespace DesignPinViaFlag {
constexpr uint32_t kHasMask = 1u << 0;
}  // namespace DesignPinViaFlag

struct DesignPinVia
{
  TechViaMasterId tech_via;
  DesignViaId design_via;
  Point origin;
  uint32_t flags = 0;
  uint32_t top_mask = 0;
  uint32_t cut_mask = 0;
  uint32_t bottom_mask = 0;
};

template <>
struct GeometryPayloadTraits<DesignPinRectangle>
{
  static constexpr GeometryShapeKind kind = GeometryShapeKind::kRectangle;
};

template <>
struct GeometryPayloadTraits<DesignPinPolygon>
{
  static constexpr GeometryShapeKind kind = GeometryShapeKind::kPolygon;
};

template <>
struct GeometryPayloadTraits<DesignPinVia>
{
  static constexpr GeometryShapeKind kind = GeometryShapeKind::kPinVia;
};

// Parser/API write-side value. A block-pin owns ordered geometry entities in
// canonical storage, while this aggregate carries the physical payload before
// it has an owner ID.
struct DesignBlockPinInput
{
  uint32_t flags = 0;
  DesignPlacementStatus placement_status = DesignPlacementStatus::kUnplaced;
  Point origin;
  DesignOrientation orientation = DesignOrientation::kN;
  std::vector<DesignPinRectangle> rectangles;
  std::vector<DesignPinPolygon> polygons;
  std::vector<DesignPinVia> vias;
};

namespace DesignIoPinPortFlag {
constexpr uint32_t kExplicit = 1u << 0;
constexpr uint32_t kHasPlacement = 1u << 1;
}  // namespace DesignIoPinPortFlag

// One DEF PIN geometry group / OpenDB dbBPin semantic unit. An explicit group
// begins with "+ PORT"; legacy top-level PIN geometry is represented by one
// group without kExplicit.
struct DesignBlockPin
{
  uint32_t flags = 0;
  DesignPlacementStatus placement_status = DesignPlacementStatus::kUnplaced;
  Point origin;
  DesignOrientation orientation = DesignOrientation::kN;
  std::vector<GeometryShapeId> rectangles;
  std::vector<GeometryShapeId> polygons;
  std::vector<GeometryShapeId> vias;
  // Owner dbBTerm. Storage assigns this relation; callers cannot forge it
  // through the block-term child vector.
  DesignBlockTermId term;
};

[[nodiscard]] inline GeometryOwner designBlockPinOwner(DesignBlockPinId id) noexcept
{
  return GeometryOwner::from(GeometryOwnerDomain::kDesignBlockPin, id);
}

[[nodiscard]] inline GeometryOwner designInstanceOwner(DesignInstanceId id) noexcept
{
  return GeometryOwner::from(GeometryOwnerDomain::kDesignInstance, id);
}

// DEF PIN / OpenDB dbBTerm. Physical dbBPins are independent entities so a
// port's geometry, pin-access data and identity can be edited independently.
struct DesignBlockTerm
{
  std::string name;
  DesignNetId net;
  DesignNetId special_net;
  DesignIoPinDirection direction = DesignIoPinDirection::kNone;
  DesignSignalUse use = DesignSignalUse::kNone;
  uint32_t flags = 0;
  std::vector<DesignBlockPinId> pins;
};

enum class DesignNetSource : uint8_t
{
  kNone,
  kNetlist,
  kDist,
  kUser,
  kTiming,
  kTest
};

namespace DesignNetFlag {
constexpr uint32_t kHasWeight = 1u << 0;
constexpr uint32_t kFixedBump = 1u << 1;
constexpr uint32_t kHasNonDefaultRule = 1u << 2;
constexpr uint32_t kMustJoin = 1u << 3;
constexpr uint32_t kHasJumpers = 1u << 4;
}  // namespace DesignNetFlag

struct DesignNet
{
  // MUSTJOIN has no DEF netName. Importers assign a private unique name so the
  // entity still participates in the same lookup and persistence machinery.
  std::string name;
  DesignSignalUse use = DesignSignalUse::kNone;
  DesignNetSource source = DesignNetSource::kNone;
  uint32_t flags = 0;
  int32_t weight = 0;
  TechNonDefaultRuleId non_default_rule;
  DesignNonDefaultRuleId design_non_default_rule;
};

// Optional reverse connectivity components attached to a DesignNet entity.
// Pin-side net references and these vectors are updated together by Storage;
// keeping the two pin kinds separate avoids an ID variant in hot traversals.
struct DesignNetInstancePins
{
  std::vector<DesignInstanceTermId> values;
};

struct DesignNetIoPins
{
  std::vector<DesignBlockTermId> values;
};

// Presence distinguishes DEF SPECIALNETS from ordinary NETS while preserving
// one common DesignNet component and ID type.
struct DesignSpecialNet
{
};

enum class DesignNetPattern : uint8_t
{
  kNone,
  kBalanced,
  kSteiner,
  kTrunk,
  kWiredLogic
};

namespace DesignNetOptionsFlag {
constexpr uint32_t kHasOriginal = 1u << 0;
constexpr uint32_t kHasPattern = 1u << 1;
constexpr uint32_t kHasEstimatedCapacitance = 1u << 2;
constexpr uint32_t kHasFrequency = 1u << 3;
constexpr uint32_t kHasXTalk = 1u << 4;
constexpr uint32_t kHasStyle = 1u << 5;
constexpr uint32_t kHasVoltage = 1u << 6;
}  // namespace DesignNetOptionsFlag

namespace DesignNetSpacingRuleFlag {
constexpr uint32_t kHasRange = 1u << 0;
}  // namespace DesignNetSpacingRuleFlag

// DEF SPECIALNETS: + SPACING layer spacing [RANGE left right].
struct DesignNetSpacingRule
{
  TechRoutingLayerId layer;
  int32_t spacing = 0;
  uint32_t flags = 0;
  int32_t range_left = 0;
  int32_t range_right = 0;
};

// Optional Component attached to a DesignNet entity. Keeping these uncommon
// fields separate avoids two strings and a vector in every ordinary net row.
struct DesignNetOptions
{
  uint32_t flags = 0;
  std::string original;
  DesignNetPattern pattern = DesignNetPattern::kNone;
  double estimated_capacitance = 0.0;
  double frequency = 0.0;
  int32_t xtalk = 0;
  int32_t style = 0;
  int32_t voltage = 0;
  std::vector<DesignNetSpacingRule> spacing_rules;
};

// Optional Verilog provenance metadata. Names retain their original spelling
// and order; aliases are not part of the DesignNet name indexes.
struct DesignNetAliases
{
  std::vector<std::string> names;
};

namespace DesignBlockTermAntennaFlag {
constexpr uint32_t kHasValues = 1u << 0;
constexpr uint32_t kHasLayerValues = 1u << 1;
constexpr uint32_t kHasModels = 1u << 2;
}  // namespace DesignBlockTermAntennaFlag

// Optional DEF top-level PIN antenna metadata owned by a DesignBlockTerm.
struct DesignBlockTermAntenna
{
  uint32_t flags = 0;
  std::vector<AntennaValue> values;
  std::vector<AntennaLayerValue> layer_values;
  std::vector<AntennaModel> models;
};

}  // namespace eccdb
