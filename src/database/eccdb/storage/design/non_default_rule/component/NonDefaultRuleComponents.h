#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "design/common/DesignGeometry.h"
#include "design/common/DesignTypes.h"
#include "tech/common/TechLayerIds.h"
#include "tech/via_master/model/ViaMasterComponents.h"
#include "tech/via_rule_generate/model/ViaRuleGenerateComponents.h"

namespace eccdb {

namespace DesignNdrLayerRuleFlag {
constexpr uint32_t kHasDiagWidth = 1u << 0;
constexpr uint32_t kHasSpacing = 1u << 1;
constexpr uint32_t kHasWireExtension = 1u << 2;
}  // namespace DesignNdrLayerRuleFlag

struct DesignNdrLayerRule
{
  TechLayerId layer;
  uint32_t flags = 0;
  int32_t width = 0;
  int32_t diag_width = 0;
  int32_t spacing = 0;
  int32_t wire_extension = 0;
};

struct DesignNdrMinCutsRule
{
  TechCutLayerId layer;
  uint32_t cut_count = 0;
};

// Exactly one of tech_via and design_via is populated.
struct DesignNdrViaRef
{
  TechViaMasterId tech_via;
  DesignViaId design_via;
};

namespace DesignNonDefaultRuleFlag {
constexpr uint32_t kHardSpacing = 1u << 0;
}  // namespace DesignNonDefaultRuleFlag

// DEF 5.8 block-scope NONDEFAULTRULE. OpenDB likewise keeps these rules on
// dbBlock, separate from technology-scope LEF NONDEFAULTRULE objects.
struct DesignNonDefaultRule
{
  std::string name;
  uint32_t flags = 0;
  std::vector<DesignNdrLayerRule> layer_rules;
  std::vector<DesignNdrViaRef> vias;
  std::vector<TechViaRuleGenerateId> via_rules;
  std::vector<DesignNdrMinCutsRule> min_cuts;
  std::vector<DesignProperty> properties;
};

}  // namespace eccdb
