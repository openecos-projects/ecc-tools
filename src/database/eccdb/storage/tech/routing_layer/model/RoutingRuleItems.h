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
