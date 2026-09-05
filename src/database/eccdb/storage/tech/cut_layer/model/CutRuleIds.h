#pragma once

#include "common/EnttId.h"
#include "tech/common/TechEntity.h"

namespace eccdb {

struct TechCutSpacingRule;
struct TechCutEnclosureRule;
struct TechCutArraySpacingRule;
struct TechCutOrthogonalSpacingTableRule;
struct TechCutLef58CutClassRule;
struct TechCutLef58EnclosureRule;
struct TechCutLef58EnclosureEdgeRule;
struct TechCutLef58EolEnclosureRule;
struct TechCutLef58EolSpacingRule;
struct TechCutLef58SpacingTableRule;
struct TechCutCurrentDensityRule;

using TechCutSpacingRuleId = EnttId<TechEntity, TechCutSpacingRule>;
using TechCutEnclosureRuleId = EnttId<TechEntity, TechCutEnclosureRule>;
using TechCutArraySpacingRuleId = EnttId<TechEntity, TechCutArraySpacingRule>;
using TechCutOrthogonalSpacingTableRuleId = EnttId<TechEntity, TechCutOrthogonalSpacingTableRule>;
using TechCutLef58CutClassRuleId = EnttId<TechEntity, TechCutLef58CutClassRule>;
using TechCutLef58EnclosureRuleId = EnttId<TechEntity, TechCutLef58EnclosureRule>;
using TechCutLef58EnclosureEdgeRuleId = EnttId<TechEntity, TechCutLef58EnclosureEdgeRule>;
using TechCutLef58EolEnclosureRuleId = EnttId<TechEntity, TechCutLef58EolEnclosureRule>;
using TechCutLef58EolSpacingRuleId = EnttId<TechEntity, TechCutLef58EolSpacingRule>;
using TechCutLef58SpacingTableRuleId = EnttId<TechEntity, TechCutLef58SpacingTableRule>;
using TechCutCurrentDensityRuleId = EnttId<TechEntity, TechCutCurrentDensityRule>;

}  // namespace eccdb
