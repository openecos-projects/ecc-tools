#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "common/EnttId.h"
#include "tech/common/RoutingTypes.h"
#include "tech/common/TechLayerTypes.h"
#include "tech/via_master/model/ViaMasterComponents.h"

namespace eccdb {

struct TechViaRule;
struct TechViaRuleLowerLayer;
struct TechViaRuleUpperLayer;
struct TechViaRuleCandidates;
struct TechViaRuleProperties;

// A non-GENERATE LEF VIARULE is one entity in the shared TechRegistry.
using TechViaRuleId = EnttId<TechEntity, TechViaRule>;

namespace TechViaRuleLayerFlag {
constexpr uint32_t kHasWidth = 1u << 0;
}  // namespace TechViaRuleLayerFlag

// LEF 5.8 ordinary (non-GENERATE) VIARULE subset:
//   VIARULE name
//     LAYER lower-routing ; DIRECTION ... ; [WIDTH ... ;]
//     LAYER upper-routing ; DIRECTION ... ; [WIDTH ... ;]
//     VIA fixed-via-name ; ...
//     [PROPERTY name value ;] ...
//   END name
struct TechViaRule
{
  std::string name;
};

// The two LAYER clauses are fixed-cardinality components on the rule entity;
// their DIRECTION is required and WIDTH is optional.
struct TechViaRuleLowerLayer
{
  TechRoutingLayerId layer;
  uint32_t flags = 0;
  RoutingDirection direction = RoutingDirection::kUnknown;
  int32_t min_width = 0;
  int32_t max_width = 0;
};

struct TechViaRuleUpperLayer
{
  TechRoutingLayerId layer;
  uint32_t flags = 0;
  RoutingDirection direction = RoutingDirection::kUnknown;
  int32_t min_width = 0;
  int32_t max_width = 0;
};

// Payload of repeated "VIA fixed-via-name ;" clauses. Candidate VIA objects
// have their own IDs; list entries need no separate entity IDs.
struct TechViaRuleCandidates
{
  std::vector<TechViaMasterId> values;
};

// Payload of one "PROPERTY name value ;" clause. It is a nested list value,
// not an independently addressable technology object.
struct TechViaRuleProperty
{
  std::string name;
  std::string value;
};

struct TechViaRuleProperties
{
  std::vector<TechViaRuleProperty> values;
};

}  // namespace eccdb
