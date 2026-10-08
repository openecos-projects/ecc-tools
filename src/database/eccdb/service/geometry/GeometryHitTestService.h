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

#include "geometry/GeometryPayloadService.h"

namespace eccdb {

// BBox is only the broad phase. This predicate uses the same detached typed
// primitives as the renderer, including expanded VIA cuts/enclosures.
[[nodiscard]] std::optional<uint64_t> hitTestGeometry(const GeometryPayloadBatch& batch,
                                                     Point point, int32_t tolerance,
                                                     std::span<const uint64_t> layer_order = {});
[[nodiscard]] std::vector<uint64_t> intersectGeometry(const GeometryPayloadBatch& batch, Rect box);

}  // namespace eccdb
