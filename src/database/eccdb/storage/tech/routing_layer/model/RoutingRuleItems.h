#pragma once

#include <cstdint>
#include <string>

namespace eccdb {

// These values are rule-internal rows, not database objects. They remain
// ordinary values in the owning Rule component and therefore have no EnTT ID.

struct TechRoutingPrlSpacingTableExceptWithin
{
  uint32_t width_index = 0;
  int32_t low = 0;
  int32_t high = 0;
};

struct TechRoutingPrlSpacingTableInfluence
{
  int32_t width = 0;
  int32_t within = 0;
  int32_t spacing = 0;
};

struct TechRoutingInfluenceSpacingTableEntry
{
  int32_t width = 0;
  int32_t within = 0;
  int32_t spacing = 0;
};

struct TechRoutingTwoWidthsSpacingTableWidth
{
  int32_t width = 0;
  bool has_prl = false;
  int32_t prl = -1;
};

struct TechRoutingLef58AreaExceptMinSize
{
  int32_t min_width = 0;
  int32_t min_length = 0;
};

struct TechRoutingLef58CornerSpacingWidth
{
  int32_t width = 0;
  int32_t spacing = 0;
};

struct TechRoutingLef58MinimumCutClass
{
  std::string cutclass_name;
  int32_t num_cuts = 0;
};

struct TechRoutingLef58JogToJogWidth
{
  int32_t width = 0;
  int32_t parallel_length = 0;
  int32_t parallel_within = 0;
  int32_t long_jog_spacing = 0;
};

}  // namespace eccdb
