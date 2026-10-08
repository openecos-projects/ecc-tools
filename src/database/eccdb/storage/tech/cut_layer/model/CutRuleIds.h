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
