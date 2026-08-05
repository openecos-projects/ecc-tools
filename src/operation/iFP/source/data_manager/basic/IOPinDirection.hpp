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

enum class IOPinDirection
{
  kNone = 0,
  kInput = 1,
  kOutput = 2,
  kOutputTriState = 3,
  kInOut = 4,
  kFeedThru = 5
};

struct GetIOPinDirectionName
{
  std::string operator()(IOPinDirection io_pin_direction) const
  {
    std::string io_pin_direction_name;
    switch (io_pin_direction) {
      case IOPinDirection::kNone:
        io_pin_direction_name = "none";
        break;
      case IOPinDirection::kInput:
        io_pin_direction_name = "INPUT";
        break;
      case IOPinDirection::kOutput:
        io_pin_direction_name = "OUTPUT";
        break;
      case IOPinDirection::kOutputTriState:
        io_pin_direction_name = "OUTPUT TRISTATE";
        break;
      case IOPinDirection::kInOut:
        io_pin_direction_name = "INOUT";
        break;
      case IOPinDirection::kFeedThru:
        io_pin_direction_name = "FEEDTHRU";
        break;
      default:
        FPLOG.error(Loc::current(), "Unrecognized type!");
        break;
    }
    return io_pin_direction_name;
  }
};

struct GetIOPinDirectionByName
{
  IOPinDirection operator()(std::string io_pin_direction_name) const
  {
    if (io_pin_direction_name == "INPUT") {
      return IOPinDirection::kInput;
    }
    if (io_pin_direction_name == "OUTPUT") {
      return IOPinDirection::kOutput;
    }
    if (io_pin_direction_name == "OUTPUT TRISTATE") {
      return IOPinDirection::kOutputTriState;
    }
    if (io_pin_direction_name == "INOUT") {
      return IOPinDirection::kInOut;
    }
    if (io_pin_direction_name == "FEEDTHRU") {
      return IOPinDirection::kFeedThru;
    }
    return IOPinDirection::kNone;
  }
};

}  // namespace ifp
