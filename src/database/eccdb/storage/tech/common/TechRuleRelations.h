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
