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

// These values are not database objects. They are ordinary rows inside the
// variable-length vectors owned by their complete Rule component.
struct TechCutArraySpacingItem
{
  uint32_t array_cut_count = 0;
  int32_t spacing = 0;
};

struct TechCutOrthogonalSpacingTableItem
{
  int32_t within = 0;
  int32_t spacing = 0;
};

// One nested "TOCLASS class spacing1 [spacing2]" entry of LEF58_EOLSPACING.
struct TechCutLef58EolSpacingToClass
{
  std::string cutclass_name;
  int32_t cut_spacing1 = 0;
  int32_t cut_spacing2 = 0;
};

// One row/column intersection of a LEF58_SPACINGTABLE CUTCLASS matrix.
struct TechCutLef58SpacingTableCell
{
  int32_t cut_spacing1 = 0;
  int32_t cut_spacing2 = 0;
  bool has_cut_spacing1 = false;
  bool has_cut_spacing2 = false;
};

struct TechCutLef58SpacingTableClassPair
{
  std::string from;
  std::string to;
};

struct TechCutLef58SpacingTablePrlEntry
{
  std::string from;
  std::string to;
  int32_t prl = 0;
};

}  // namespace eccdb
