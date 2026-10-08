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

#include <span>
#include <vector>

#include "common/GeometryTypes.h"

namespace eccdb {

// Converts one LEF POLYGON with the same Boost.Polygon polygon_90 algorithm
// used by legacy iDB. This is a rectangle-only storage compatibility path;
// LEF-permitted 45-degree edges become Boost's deterministic staircase cover.
// Use native Polygon storage when the original boundary must be preserved.
[[nodiscard]] std::vector<Rect> decomposePolygonToRectangles(std::span<const Point> points);

}  // namespace eccdb
