// ***************************************************************************************
// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************

#pragma once

#include "common/EnttId.h"
#include "tech/common/TechEntity.h"

namespace eccdb {

struct TechRoutingSpacingRule;
struct TechRoutingEndOfLineSpacingRule;
struct TechRoutingMinEncloseAreaRule;
struct TechRoutingMinStepRule;
struct TechRoutingMinimumCutRule;
struct TechRoutingSpacingNotchLengthRule;
struct TechRoutingPrlSpacingTableRule;
struct TechRoutingInfluenceSpacingTableRule;
struct TechRoutingTwoWidthsSpacingTableRule;
struct TechRoutingCurrentDensityRule;
struct TechRoutingLef58AreaRule;
struct TechRoutingLef58CornerFillSpacingRule;
struct TechRoutingLef58CornerSpacingRule;
struct TechRoutingLef58MinimumCutRule;
struct TechRoutingLef58MinStepRule;
struct TechRoutingLef58WidthTableRule;
struct TechRoutingLef58SpacingEolRule;
struct TechRoutingLef58SpacingNotchLengthRule;
struct TechRoutingLef58SpacingTableJogToJogRule;

using TechRoutingSpacingRuleId = EnttId<TechEntity, TechRoutingSpacingRule>;
using TechRoutingEndOfLineSpacingRuleId = EnttId<TechEntity, TechRoutingEndOfLineSpacingRule>;
using TechRoutingMinEncloseAreaRuleId = EnttId<TechEntity, TechRoutingMinEncloseAreaRule>;
using TechRoutingMinStepRuleId = EnttId<TechEntity, TechRoutingMinStepRule>;
using TechRoutingMinimumCutRuleId = EnttId<TechEntity, TechRoutingMinimumCutRule>;
using TechRoutingSpacingNotchLengthRuleId = EnttId<TechEntity, TechRoutingSpacingNotchLengthRule>;
using TechRoutingPrlSpacingTableRuleId = EnttId<TechEntity, TechRoutingPrlSpacingTableRule>;
using TechRoutingInfluenceSpacingTableRuleId = EnttId<TechEntity, TechRoutingInfluenceSpacingTableRule>;
using TechRoutingTwoWidthsSpacingTableRuleId = EnttId<TechEntity, TechRoutingTwoWidthsSpacingTableRule>;
using TechRoutingCurrentDensityRuleId = EnttId<TechEntity, TechRoutingCurrentDensityRule>;
using TechRoutingLef58AreaRuleId = EnttId<TechEntity, TechRoutingLef58AreaRule>;
using TechRoutingLef58CornerFillSpacingRuleId = EnttId<TechEntity, TechRoutingLef58CornerFillSpacingRule>;
using TechRoutingLef58CornerSpacingRuleId = EnttId<TechEntity, TechRoutingLef58CornerSpacingRule>;
using TechRoutingLef58MinimumCutRuleId = EnttId<TechEntity, TechRoutingLef58MinimumCutRule>;
using TechRoutingLef58MinStepRuleId = EnttId<TechEntity, TechRoutingLef58MinStepRule>;
using TechRoutingLef58WidthTableRuleId = EnttId<TechEntity, TechRoutingLef58WidthTableRule>;
using TechRoutingLef58SpacingEolRuleId = EnttId<TechEntity, TechRoutingLef58SpacingEolRule>;
using TechRoutingLef58SpacingNotchLengthRuleId = EnttId<TechEntity, TechRoutingLef58SpacingNotchLengthRule>;
using TechRoutingLef58SpacingTableJogToJogRuleId = EnttId<TechEntity, TechRoutingLef58SpacingTableJogToJogRule>;

}  // namespace eccdb
