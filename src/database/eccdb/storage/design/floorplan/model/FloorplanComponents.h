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
#include <vector>

#include "common/GeometryTypes.h"
#include "design/common/DesignTypes.h"
#include "geometry/GeometryEntity.h"
#include "library/common/LibraryTypes.h"
#include "tech/common/TechLayerIds.h"

namespace eccdb {

// Optional operational Core boundary. DEF has no COREAREA statement; when
// absent, DesignFloorplanStorage derives the Core from non-PAD Rows.
struct DesignCoreArea
{
  GeometryShapeId boundary;
};

struct DesignCoreAreaInput
{
  std::vector<Point> boundary;
};

[[nodiscard]] inline GeometryOwner designCoreAreaOwner(DesignRootId id) noexcept
{
  return GeometryOwner::from(GeometryOwnerDomain::kDesignCoreArea, id);
}

namespace DesignRowFlag {
constexpr uint32_t kHasDo = 1u << 0;
constexpr uint32_t kHasStep = 1u << 1;
}  // namespace DesignRowFlag

// DEF ROW rowName siteName origin orientation DO/BY/STEP.
struct DesignRow
{
  std::string name;
  LibrarySiteId site;
  Point origin;
  DesignOrientation orientation = DesignOrientation::kN;
  uint32_t repeat_count_x = 1;
  uint32_t repeat_count_y = 1;
  int32_t step_x = 0;
  int32_t step_y = 0;
  uint32_t flags = 0;
  std::vector<DesignProperty> properties;
};

namespace DesignTrackGridFlag {
constexpr uint32_t kHasMask = 1u << 0;
constexpr uint32_t kSameMask = 1u << 1;
}  // namespace DesignTrackGridFlag

// DEF TRACKS. One entity represents one TRACKS statement and may reference
// multiple routing layers.
struct DesignTrackGrid
{
  DesignAxis axis = DesignAxis::kX;
  int32_t start = 0;
  uint32_t track_count = 0;
  int32_t step = 0;
  uint32_t flags = 0;
  uint32_t mask = 0;
  std::vector<TechRoutingLayerId> layers;
};

// DEF GCELLGRID. X and Y clauses remain separate entities.
struct DesignGCellGrid
{
  DesignAxis axis = DesignAxis::kX;
  int32_t start = 0;
  uint32_t line_count = 0;
  int32_t step = 0;
};

}  // namespace eccdb
