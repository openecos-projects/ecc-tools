// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
//
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

enum class IOPlacerMode
{
  kIOPin,
  kIOCell
};

struct GetIOPlacerModeName
{
  std::string operator()(IOPlacerMode mode) const
  {
    switch (mode) {
      case IOPlacerMode::kIOPin:
        return "io_pin";
      case IOPlacerMode::kIOCell:
        return "io_cell";
    }
    return "io_pin";
  }
};

struct GetIOPlacerModeByName
{
  IOPlacerMode operator()(const std::string& name) const
  {
    if (name == "io_cell") {
      return IOPlacerMode::kIOCell;
    }
    if (name == "io_pin") {
      return IOPlacerMode::kIOPin;
    }
    FPLOG.error(Loc::current(), "Unrecognized io_placer.mode: ", name, ". Expected 'io_pin' or 'io_cell'.");
    return IOPlacerMode::kIOPin;
  }
};

}  // namespace ifp
