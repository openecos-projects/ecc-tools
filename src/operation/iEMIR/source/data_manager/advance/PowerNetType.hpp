// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of the Mulan PSL v2 at:
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

namespace iemir {

enum class PowerNetType
{
  kNone,
  kPower,
  kGround
};

struct GetPowerNetTypeName
{
  std::string operator()(const PowerNetType& power_net_type) const
  {
    std::string power_net_type_name;
    switch (power_net_type) {
      case PowerNetType::kNone:
        power_net_type_name = "none";
        break;
      case PowerNetType::kPower:
        power_net_type_name = "power";
        break;
      case PowerNetType::kGround:
        power_net_type_name = "ground";
        break;
      default:
        EMIRLOG.error(Loc::current(), "Unrecognized type!");
        break;
    }
    return power_net_type_name;
  }
};

}  // namespace iemir
