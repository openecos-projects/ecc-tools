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

enum class TimingCheckType
{
  kNone,
  kSetup,
  kHold,
  kRecovery,
  kRemoval,
  kWidth,
  kPeriod
};

struct GetTimingCheckTypeName
{
  std::string operator()(const TimingCheckType& timing_check_type) const
  {
    std::string timing_check_type_name;
    switch (timing_check_type) {
      case TimingCheckType::kNone:
        timing_check_type_name = "none";
        break;
      case TimingCheckType::kSetup:
        timing_check_type_name = "setup";
        break;
      case TimingCheckType::kHold:
        timing_check_type_name = "hold";
        break;
      case TimingCheckType::kRecovery:
        timing_check_type_name = "recovery";
        break;
      case TimingCheckType::kRemoval:
        timing_check_type_name = "removal";
        break;
      case TimingCheckType::kWidth:
        timing_check_type_name = "width";
        break;
      case TimingCheckType::kPeriod:
        timing_check_type_name = "period";
        break;
      default:
        STALOG.error(Loc::current(), "Unrecognized type!");
        break;
    }
    return timing_check_type_name;
  }
};

}  // namespace ista
