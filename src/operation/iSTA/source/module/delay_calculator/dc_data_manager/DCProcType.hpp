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
// WHETHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "Logger.hpp"

namespace ista {

enum class DCProcType
{
  kNone,
  kInitialize,
  kCalculate
};

struct GetDCProcTypeName
{
  std::string operator()(const DCProcType& proc_type) const
  {
    std::string proc_type_name;
    switch (proc_type) {
      case DCProcType::kNone:
        proc_type_name = "none";
        break;
      case DCProcType::kInitialize:
        proc_type_name = "initialize";
        break;
      case DCProcType::kCalculate:
        proc_type_name = "calculate";
        break;
      default:
        STALOG.error(Loc::current(), "Unrecognized type!");
        break;
    }
    return proc_type_name;
  }
};

}  // namespace ista
