// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#pragma once

#include "geometry/GeometryInput.h"
#include "tech/common/TechLayerTypes.h"

namespace eccdb {

// Write-side geometry for one fixed or generated VIA. Storage validates the
// layer roles and materializes each primitive as an independent GeometryShapeId.
struct TechViaMasterShapeInput
{
  TechConductorLayerRef bottom_layer;
  GeometryInput bottom_geometry;

  TechCutLayerId cut_layer;
  GeometryInput cut_geometry;

  TechConductorLayerRef top_layer;
  GeometryInput top_geometry;
};

}  // namespace eccdb
