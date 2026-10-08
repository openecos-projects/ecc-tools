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
#include <string_view>

#include "design/common/DesignGeometry.h"

namespace eccdb {

class TechRegistry;

[[nodiscard]] Rect validateDesignOrthogonalBoundary(std::span<const Point> boundary, std::string_view object_name);
void validateDesignShapeSet(const DesignShapeSet& shapes, bool require_nonempty = true);
void validateDesignLayerGeometry(const TechRegistry& technology, std::span<const DesignLayerGeometry> geometry,
                                 bool require_nonempty = true);

}  // namespace eccdb
