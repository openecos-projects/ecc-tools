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

#include <entt/entt.hpp>
#include <vector>

#include "tech/common/TechEntity.h"

namespace eccdb {

// Shared complete-object relationships in the technology registry. Every
// complete Rule is a separate entity and keeps one reverse owner reference.
struct TechRuleOwner
{
  // The parent can be a Layer, an NDR, or another Tech aggregate. The payload
  // component determines the concrete owner type.
  TechEntity owner = entt::null;
};

// Ordered one-to-many relation stored directly on the owner entity. RuleId is
// strong-typed, so one owner may carry several distinct rule families without
// conflating their IDs.
template <typename RuleId>
struct TechRuleRefs
{
  std::vector<RuleId> values;
};

template <typename RuleId>
struct TechRuleRef
{
  RuleId rule;
};

}  // namespace eccdb
