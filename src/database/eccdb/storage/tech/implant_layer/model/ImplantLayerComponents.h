#pragma once

#include <cstdint>
#include <vector>

#include "tech/common/TechLayerTypes.h"

namespace eccdb {

namespace TechImplantLayerFlag {
constexpr uint32_t kHasMinWidth = 1u << 0;
}  // namespace TechImplantLayerFlag

// LEF 5.8 IMPLANT layer:
//   LAYER name
//     TYPE IMPLANT ;
//     [WIDTH minWidth ;]
//     [SPACING minSpacing [LAYER otherImplant] ;] ...
//   END name
// Repeated SPACING clauses are value records in TechImplantSpacingRules on the
// same layer entity.
struct TechImplantLayer
{
  uint32_t flags = 0;
  int32_t min_width = 0;
};

namespace TechImplantSpacingRuleFlag {
constexpr uint32_t kHasOtherLayer = 1u << 0;
}  // namespace TechImplantSpacingRuleFlag

// One clause from the IMPLANT layer grammar:
//   SPACING minSpacing [LAYER otherImplant] ;
struct TechImplantSpacingRule
{
  uint32_t flags = 0;
  int32_t min_spacing = 0;
  TechImplantLayerId other_layer;
};

struct TechImplantSpacingRules
{
  std::vector<TechImplantSpacingRule> values;
};

}  // namespace eccdb
