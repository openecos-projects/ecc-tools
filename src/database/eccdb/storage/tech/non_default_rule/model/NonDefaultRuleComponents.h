#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "common/EnttId.h"
#include "tech/common/TechLayerTypes.h"
#include "tech/via_master/model/ViaMasterComponents.h"
#include "tech/via_rule_generate/model/ViaRuleGenerateComponents.h"

namespace eccdb {

struct TechNonDefaultRule;

using TechNonDefaultRuleId = EnttId<TechEntity, TechNonDefaultRule>;

namespace TechNonDefaultRuleFlag {
constexpr uint32_t kHardSpacing = 1u << 0;
}  // namespace TechNonDefaultRuleFlag

namespace TechNdrRoutingRuleFlag {
constexpr uint32_t kHasDiagWidth = 1u << 0;
constexpr uint32_t kHasSpacing = 1u << 1;
constexpr uint32_t kHasWireExtension = 1u << 2;
constexpr uint32_t kHasResistance = 1u << 3;
constexpr uint32_t kHasCapacitance = 1u << 4;
constexpr uint32_t kHasEdgeCapacitance = 1u << 5;
}  // namespace TechNdrRoutingRuleFlag

// LEF 5.8:
//   NONDEFAULTRULE name
//     [HARDSPACING ;]
//     {LAYER routingLayer ... END routingLayer} ...
//     [VIA viaDefinition] ...
//     [USEVIA viaName ;] ... [USEVIARULE viaRuleName ;] ...
//     [MINCUTS cutLayer count ;] ... [PROPERTY name value ;] ...
//   END name
//
// The named NDR is an entity because DEF objects can reference it. Ordinary
// clauses are value records in typed vector components on that entity. A named
// NDR VIA remains a separate TechViaMaster entity because USEVIA can reference
// a VIA defined by an earlier NDR.
struct TechNonDefaultRule
{
  std::string name;
  uint32_t flags = 0;

  [[nodiscard]] bool isHardSpacing() const noexcept { return (flags & TechNonDefaultRuleFlag::kHardSpacing) != 0u; }
};

// One "LAYER ... END layerName" clause. WIDTH is required; optional fields
// are guarded by TechNdrRoutingRuleFlag.
struct TechNdrRoutingRule
{
  TechRoutingLayerId layer;
  uint32_t flags = 0;

  int32_t width = 0;
  int32_t diag_width = 0;
  int32_t spacing = 0;
  int32_t wire_extension = 0;

  double resistance = 0.0;
  double capacitance = 0.0;
  double edge_capacitance = 0.0;
};

struct TechNdrRoutingRules
{
  std::vector<TechNdrRoutingRule> values;
};

// One "MINCUTS cutLayerName numCuts ;" clause.
struct TechNdrMinCutsRule
{
  TechCutLayerId layer;
  uint32_t cut_count = 0;
};

struct TechNdrMinCutsRules
{
  std::vector<TechNdrMinCutsRule> values;
};

// Ordered USEVIA and USEVIARULE references. Referenced VIA and VIARULE
// objects retain their own strongly typed entity IDs.
struct TechNdrUseVias
{
  std::vector<TechViaMasterId> values;
};

struct TechNdrUseViaRules
{
  std::vector<TechViaRuleGenerateId> values;
};

// One "PROPERTY name value ;" clause and its ordered collection component.
struct TechNdrProperty
{
  std::string name;
  std::string value;
};

struct TechNdrProperties
{
  std::vector<TechNdrProperty> values;
};

// Marker and owner relation on an NDR-defined VIA entity. The same entity also
// carries TechViaMaster, TechViaGeometry and optionally TechGeneratedViaMaster.
struct TechNdrViaDefinition
{
  TechNonDefaultRuleId owner;
};

struct TechNdrViaDefinitions
{
  std::vector<TechViaMasterId> values;
};

namespace TechNdrSameNetSpacingRuleFlag {
constexpr uint32_t kStack = 1u << 0;
}  // namespace TechNdrSameNetSpacingRuleFlag

// Legacy LEF NONDEFAULTRULE same-net clause:
//   SPACING layer1 layer2 distance [STACK] ;
struct TechNdrSameNetSpacingRule
{
  TechLayerId first_layer;
  TechLayerId second_layer;
  uint32_t flags = 0;
  int32_t spacing = 0;
};

struct TechNdrSameNetSpacingRules
{
  std::vector<TechNdrSameNetSpacingRule> values;
};

}  // namespace eccdb
