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
#include "STAHeader.hpp"

namespace ista {

enum class PowerGroupType
{
  kNone,
  kIOPad,
  kMemory,
  kBlackBox,
  kClockNetwork,
  kRegister,
  kSequential,
  kCombinational
};

struct GetPowerGroupTypeName
{
  std::string operator()(const PowerGroupType& power_group_type) const
  {
    std::string power_group_type_name;
    switch (power_group_type) {
      case PowerGroupType::kNone:
        power_group_type_name = "none";
        break;
      case PowerGroupType::kIOPad:
        power_group_type_name = "io_pad";
        break;
      case PowerGroupType::kMemory:
        power_group_type_name = "memory";
        break;
      case PowerGroupType::kBlackBox:
        power_group_type_name = "black_box";
        break;
      case PowerGroupType::kClockNetwork:
        power_group_type_name = "clock_network";
        break;
      case PowerGroupType::kRegister:
        power_group_type_name = "register";
        break;
      case PowerGroupType::kSequential:
        power_group_type_name = "sequential";
        break;
      case PowerGroupType::kCombinational:
        power_group_type_name = "combinational";
        break;
      default:
        STALOG.error(Loc::current(), "Unrecognized type!");
        break;
    }
    return power_group_type_name;
  }
};

struct GetPowerGroupTypeList
{
  std::vector<PowerGroupType> operator()() const
  {
    return {PowerGroupType::kIOPad, PowerGroupType::kMemory, PowerGroupType::kBlackBox, PowerGroupType::kClockNetwork,
            PowerGroupType::kRegister, PowerGroupType::kSequential, PowerGroupType::kCombinational};
  }
};

}  // namespace ista
