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
#include "tech/common/TechLayerIds.h"

namespace eccdb {

enum class DesignRegionType : uint8_t
{
  kFence,
  kGuide
};

struct DesignRegion
{
  std::string name;
  DesignRegionType type = DesignRegionType::kFence;
  std::vector<GeometryShapeId> rectangles;
};

struct DesignRegionInput
{
  std::string name;
  DesignRegionType type = DesignRegionType::kFence;
  std::vector<Rect> rectangles;
};

namespace DesignGroupFlag {
constexpr uint32_t kHasRegion = 1u << 0;
}  // namespace DesignGroupFlag

struct DesignGroup
{
  std::string name;
  uint32_t flags = 0;
  DesignRegionId region;
  std::vector<DesignInstanceId> instances;
};

enum class DesignBlockageKind : uint8_t
{
  kPlacement,
  kRouting
};

namespace DesignBlockageFlag {
constexpr uint32_t kHasLayer = 1u << 0;
constexpr uint32_t kSoft = 1u << 1;
constexpr uint32_t kPushdown = 1u << 2;
constexpr uint32_t kHasPartial = 1u << 3;
constexpr uint32_t kHasComponent = 1u << 4;
constexpr uint32_t kSlots = 1u << 5;
constexpr uint32_t kFills = 1u << 6;
constexpr uint32_t kExceptPgNet = 1u << 7;
constexpr uint32_t kHasSpacing = 1u << 8;
constexpr uint32_t kHasDesignRuleWidth = 1u << 9;
constexpr uint32_t kHasMask = 1u << 10;
}  // namespace DesignBlockageFlag

struct DesignBlockage
{
  DesignBlockageKind kind = DesignBlockageKind::kPlacement;
  uint32_t flags = 0;
  TechRoutingLayerId layer;
  DesignInstanceId component;
  double partial = 0.0;
  int32_t spacing = 0;
  int32_t design_rule_width = 0;
  uint32_t mask = 0;
  std::vector<GeometryShapeId> rectangles;
  std::vector<GeometryShapeId> polygons;
};

struct DesignBlockageInput
{
  DesignBlockageKind kind = DesignBlockageKind::kPlacement;
  uint32_t flags = 0;
  TechRoutingLayerId layer;
  DesignInstanceId component;
  double partial = 0.0;
  int32_t spacing = 0;
  int32_t design_rule_width = 0;
  uint32_t mask = 0;
  std::vector<Rect> rectangles;
  std::vector<std::vector<Point>> polygons;
};

[[nodiscard]] inline GeometryOwner designRegionOwner(DesignRegionId id) noexcept
{
  return GeometryOwner::from(GeometryOwnerDomain::kDesignRegion, id);
}

[[nodiscard]] inline GeometryOwner designBlockageOwner(DesignBlockageId id) noexcept
{
  return GeometryOwner::from(GeometryOwnerDomain::kDesignBlockage, id);
}

}  // namespace eccdb
