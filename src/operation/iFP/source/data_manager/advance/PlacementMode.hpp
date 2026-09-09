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
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "Logger.hpp"

namespace ifp {

enum class PlacementMode
{
  kAuto,
  kFile
};

struct GetPlacementModeName
{
  std::string operator()(PlacementMode placement_mode) const
  {
    switch (placement_mode) {
      case PlacementMode::kAuto:
        return "auto";
      case PlacementMode::kFile:
        return "file";
      default:
        FPLOG.error(Loc::current(), "Unrecognized placement mode!");
    }
    return "";
  }
};

struct GetPlacementModeByName
{
  PlacementMode operator()(const std::string& placement_mode_name) const
  {
    if (placement_mode_name == "auto") {
      return PlacementMode::kAuto;
    }
    if (placement_mode_name == "file") {
      return PlacementMode::kFile;
    }
    FPLOG.error(Loc::current(), "Unrecognized placement mode: ", placement_mode_name, ". Expected 'auto' or 'file'.");
    return PlacementMode::kAuto;
  }
};

}  // namespace ifp
