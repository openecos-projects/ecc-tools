// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#pragma once

#include <vector>

#include "geometry/GeometryInput.h"
#include "library/common/LibraryGeometry.h"
#include "library/master_port/model/MasterPortComponents.h"

namespace eccdb {

struct LibraryPortLayerGeometryInput
{
  TechLayerId layer;
  GeometryInput geometry;
};

struct LibraryMasterPortInput
{
  LibraryMasterPortClass port_class = LibraryMasterPortClass::kNone;
  std::vector<LibraryPortLayerGeometryInput> layer_clauses;
  std::vector<LibraryViaPlacement> vias;
};

}  // namespace eccdb
