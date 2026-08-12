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
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "Logger.hpp"

namespace ista {

enum class PinDirection
{
  kNone,
  kInput,
  kOutput,
  kInout
};

struct GetPinDirectionName
{
  std::string operator()(const PinDirection& pin_direction) const
  {
    std::string pin_direction_name;
    switch (pin_direction) {
      case PinDirection::kNone:
        pin_direction_name = "none";
        break;
      case PinDirection::kInput:
        pin_direction_name = "input";
        break;
      case PinDirection::kOutput:
        pin_direction_name = "output";
        break;
      case PinDirection::kInout:
        pin_direction_name = "inout";
        break;
      default:
        STALOG.error(Loc::current(), "Unrecognized type!");
        break;
    }
    return pin_direction_name;
  }
};

}  // namespace ista
