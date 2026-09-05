#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "common/GeometryTypes.h"
#include "design/common/DesignGeometry.h"
#include "design/common/DesignTypes.h"
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

// DEF COMPONENT. InstancePin entities are materialized from the referenced
// LibraryCellMaster terms and owned through DesignInstancePins.
struct DesignInstance
{
  std::string name;
  LibraryCellMasterId master;
  Point origin;
  DesignOrientation orientation = DesignOrientation::kN;
  DesignPlacementStatus placement_status = DesignPlacementStatus::kUnplaced;
  DesignInstanceSource source = DesignInstanceSource::kNone;
  uint32_t flags = 0;
  int32_t weight = 0;
  DesignRegionId region;
  std::vector<Rect> region_bounds;
  DesignInstanceHalo halo;
  DesignInstanceRouteHalo route_halo;
};

struct DesignInstancePins
{
  std::vector<DesignInstancePinId> values;
};

// Physical instance terminal. Its name, direction and use remain canonical in
// the referenced LibraryMasterTerm.
struct DesignInstancePin
{
  DesignInstanceId instance;
  LibraryMasterTermId master_term;
  DesignNetId net;
  DesignNetId special_net;
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

namespace DesignIoPinPortFlag {
constexpr uint32_t kExplicit = 1u << 0;
constexpr uint32_t kHasPlacement = 1u << 1;
}  // namespace DesignIoPinPortFlag

// One DEF PIN geometry group. An explicit group begins with "+ PORT"; legacy
// top-level PIN geometry is represented by one group without kExplicit.
struct DesignIoPinPort
{
  uint32_t flags = 0;
  DesignPlacementStatus placement_status = DesignPlacementStatus::kUnplaced;
  Point origin;
  DesignOrientation orientation = DesignOrientation::kN;
  std::vector<DesignPinRectangle> rectangles;
  std::vector<DesignPinPolygon> polygons;
  std::vector<DesignPinVia> vias;
};

// DEF PIN. Ports are nested values because they are owned exclusively by one
// logical pin and do not need an independent Design Entity identity.
struct DesignIoPin
{
  std::string name;
  DesignNetId net;
  DesignNetId special_net;
  DesignIoPinDirection direction = DesignIoPinDirection::kNone;
  DesignSignalUse use = DesignSignalUse::kNone;
  uint32_t flags = 0;
  std::vector<DesignIoPinPort> ports;
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
  std::vector<DesignInstancePinId> values;
};

struct DesignNetIoPins
{
  std::vector<DesignIoPinId> values;
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

}  // namespace eccdb
