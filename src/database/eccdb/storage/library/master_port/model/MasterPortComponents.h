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
#include <vector>

#include "geometry/GeometryEntity.h"
#include "library/common/LibraryGeometry.h"
#include "library/common/LibraryTypes.h"
#include "tech/common/TechLayerTypes.h"

namespace eccdb {

// One LEF PORT is a physical connection group below a logical master term:
//   PORT
//     [CLASS {NONE | CORE | BUMP} ;]
//     {LAYER layerName ; [WIDTH width ;] RECT ... ; ... | VIA ... ;} ...
//   END
enum class LibraryMasterPortClass : uint8_t
{
  kNone = 0,
  kCore,
  kBump
};

// Persistent payload for one LEF PORT LAYER clause. Geometry primitives are
// independent entities; this vector preserves the source order of RECT and
// POLYGON values without duplicating their payload in the PORT component.
struct LibraryPortLayerGeometry
{
  TechLayerId layer;
  std::vector<GeometryShapeId> geometry;
};

// One complete LEF PORT and the only payload component on its entity. The
// owner term is maintained by LibraryMasterPortStorage.
struct LibraryMasterPin
{
  LibraryMasterPortClass port_class = LibraryMasterPortClass::kNone;
  std::vector<LibraryPortLayerGeometry> layer_clauses;
  std::vector<GeometryShapeId> vias;

  // Storage-managed reverse link to the logical PIN that owns this PORT.
  LibraryMasterTermId term;
};

}  // namespace eccdb
