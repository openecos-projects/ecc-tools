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

enum class LibrarySiteClass : uint8_t
{
  kNone = 0,
  kCore,
  kPad,
  kCorner,
  kVirtual
};

// LEF 5.8 SITE subset represented by this component:
//   SITE name
//     CLASS {CORE | PAD | VIRTUAL} ;
//     [SYMMETRY {X | Y | R90} ... ;]
//     SIZE width BY height ;
//   END name
// Row orientation belongs to the design-side Row object. ROWPATTERN is not
// represented yet; kCorner and overlap preserve iDB extensions.
struct LibrarySite
{
  std::string name;
  int32_t width = -1;
  int32_t height = -1;
  bool overlap = false;
  LibrarySiteClass site_class = LibrarySiteClass::kNone;
  bool symmetry_x = false;
  bool symmetry_y = false;
  bool symmetry_r90 = false;
};

}  // namespace eccdb
