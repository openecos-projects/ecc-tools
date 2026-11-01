// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#pragma once

#include <vector>

#include "common/GeometryTypes.h"
#include "geometry/GeometryEntity.h"
#include "tech/via_master/model/ViaMasterComponents.h"

namespace eccdb {

class TechRegistry;

// LEF VIA placement inside a PIN/PORT or OBS:
//   VIA [MASK ...] x y viaName ;
// The master VIA remains owned by Tech; MASK is not represented yet.
struct LibraryViaPlacement
{
  TechViaMasterId via;
  Point origin;
};

template <>
struct GeometryPayloadTraits<LibraryViaPlacement>
{
  static constexpr GeometryShapeKind kind = GeometryShapeKind::kLibraryVia;
};

void validateLibraryViaPlacements(const TechRegistry& tech_registry, const std::vector<LibraryViaPlacement>& placements);

}  // namespace eccdb
