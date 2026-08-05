// ***************************************************************************************
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
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "Logger.hpp"

namespace ifp {

enum class PlacementOrientation
{
  kNone = 0,
  kN = 1,
  kW = 2,
  kS = 3,
  kE = 4,
  kFN = 5,
  kFE = 6,
  kFS = 7,
  kFW = 8
};

struct GetPlacementOrientationName
{
  std::string operator()(PlacementOrientation placement_orientation) const
  {
    std::string placement_orientation_name;
    switch (placement_orientation) {
      case PlacementOrientation::kNone:
        placement_orientation_name = "none";
        break;
      case PlacementOrientation::kN:
        placement_orientation_name = "N";
        break;
      case PlacementOrientation::kW:
        placement_orientation_name = "W";
        break;
      case PlacementOrientation::kS:
        placement_orientation_name = "S";
        break;
      case PlacementOrientation::kE:
        placement_orientation_name = "E";
        break;
      case PlacementOrientation::kFN:
        placement_orientation_name = "FN";
        break;
      case PlacementOrientation::kFE:
        placement_orientation_name = "FE";
        break;
      case PlacementOrientation::kFS:
        placement_orientation_name = "FS";
        break;
      case PlacementOrientation::kFW:
        placement_orientation_name = "FW";
        break;
      default:
        FPLOG.error(Loc::current(), "Unrecognized type!");
        break;
    }
    return placement_orientation_name;
  }
};

struct GetPlacementOrientationByName
{
  PlacementOrientation operator()(std::string placement_orientation_name) const
  {
    if (placement_orientation_name == "N") {
      return PlacementOrientation::kN;
    }
    if (placement_orientation_name == "W") {
      return PlacementOrientation::kW;
    }
    if (placement_orientation_name == "S") {
      return PlacementOrientation::kS;
    }
    if (placement_orientation_name == "E") {
      return PlacementOrientation::kE;
    }
    if (placement_orientation_name == "FN") {
      return PlacementOrientation::kFN;
    }
    if (placement_orientation_name == "FE") {
      return PlacementOrientation::kFE;
    }
    if (placement_orientation_name == "FS") {
      return PlacementOrientation::kFS;
    }
    if (placement_orientation_name == "FW") {
      return PlacementOrientation::kFW;
    }
    return PlacementOrientation::kNone;
  }
};

}  // namespace ifp
