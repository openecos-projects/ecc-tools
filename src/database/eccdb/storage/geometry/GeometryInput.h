// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#pragma once

#include <vector>

#include "common/GeometryTypes.h"

namespace eccdb {

// Write-side polygon value. Domain geometry storage materializes it as one
// typed EnTT geometry entity while preserving the point sequence.
struct GeometryPolygonInput
{
  std::vector<Point> points;
};

// Shared write-side geometry accepted by database storage facades. It is a
// transient import value; canonical payloads live in typed geometry entities.
struct GeometryInput
{
  std::vector<Rect> rects;
  std::vector<GeometryPolygonInput> polygons;

  [[nodiscard]] bool empty() const noexcept { return rects.empty() && polygons.empty(); }
};

}  // namespace eccdb
