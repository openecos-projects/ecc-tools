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

#include "common/GeometryTypes.h"
#include "tech/common/TechLayerIds.h"
#include "geometry/GeometryEntity.h"

namespace eccdb {

namespace DesignFillFlag {
constexpr uint32_t kOpc = 1u << 0;
constexpr uint32_t kHasMask = 1u << 1;
}  // namespace DesignFillFlag

// DEF 5.8 rectangular layer fill:
//   FILLS count ;
//     - LAYER layerName [ + MASK mask ] [ + OPC ]
//       RECT pt pt ... ;
//   END FILLS
//
// Polygon and VIA fill alternatives are intentionally outside the current
// model. Rectangles share the layer, mask and OPC options of one DEF entry.
struct DesignFill
{
  TechLayerId layer;
  uint32_t flags = 0;
  uint32_t mask = 0;
  std::vector<GeometryShapeId> rectangles;
};

struct DesignFillInput
{
  TechLayerId layer;
  uint32_t flags = 0;
  uint32_t mask = 0;
  std::vector<Rect> rectangles;
};

[[nodiscard]] inline GeometryOwner designFillOwner(DesignFillId id) noexcept
{
  return GeometryOwner::from(GeometryOwnerDomain::kDesignFill, id);
}

}  // namespace eccdb
