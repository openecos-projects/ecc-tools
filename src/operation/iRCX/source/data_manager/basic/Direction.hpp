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

namespace ircx {

enum class Direction
{
  kNone,
  kInput,
  kOutput,
  kInOut
};

struct GetDirectionName
{
  std::string operator()(const Direction& direction) const
  {
    std::string direction_name;
    switch (direction) {
      case Direction::kNone:
        direction_name = "none";
        break;
      case Direction::kInput:
        direction_name = "input";
        break;
      case Direction::kOutput:
        direction_name = "output";
        break;
      case Direction::kInOut:
        direction_name = "inout";
        break;
      default:
        RCXLOG.error(Loc::current(), "Unrecognized type!");
        break;
    }
    return direction_name;
  }
};

}  // namespace ircx
