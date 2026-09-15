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

namespace ifp {

enum class DieMode
{
  kNone,
  kDieUtil,
  kDieSize
};

struct GetDieModeName
{
  std::string operator()(const DieMode& die_mode) const
  {
    std::string die_mode_name;
    switch (die_mode) {
      case DieMode::kNone:
        die_mode_name = "none";
        break;
      case DieMode::kDieUtil:
        die_mode_name = "die_util";
        break;
      case DieMode::kDieSize:
        die_mode_name = "die_size";
        break;
      default:
        FPLOG.error(Loc::current(), "Unrecognized type!");
        break;
    }
    return die_mode_name;
  }
};

struct GetDieModeByName
{
  DieMode operator()(const std::string& die_mode_name) const
  {
    DieMode die_mode = DieMode::kNone;
    if (die_mode_name == "die_util") {
      die_mode = DieMode::kDieUtil;
    } else if (die_mode_name == "die_size") {
      die_mode = DieMode::kDieSize;
    } else {
      FPLOG.error(Loc::current(), "Unrecognized die mode: ", die_mode_name);
    }
    return die_mode;
  }
};

}  // namespace ifp
