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
#include "geometry/GeometryEntity.h"

namespace eccdb {

// Geometry is stored as one EnTT entity per physical primitive.  These
// components intentionally contain only the primitive payload; owner and
// source metadata live in the common GeometryShape component.
struct GeometryRectangle
{
  Rect rectangle;
};

struct GeometryPolygon
{
  std::vector<Point> points;
};

// DEF DIEAREA and the operational core boundary preserve their source order.
// Two opposite corners represent a rectangle; otherwise points form a polygon.
struct GeometryBoundary
{
  std::vector<Point> points;
};

template <>
struct GeometryPayloadTraits<GeometryRectangle>
{
  static constexpr GeometryShapeKind kind = GeometryShapeKind::kRectangle;
};

template <>
struct GeometryPayloadTraits<GeometryPolygon>
{
  static constexpr GeometryShapeKind kind = GeometryShapeKind::kPolygon;
};

template <>
struct GeometryPayloadTraits<GeometryBoundary>
{
  static constexpr GeometryShapeKind kind = GeometryShapeKind::kBoundary;
};

}  // namespace eccdb
