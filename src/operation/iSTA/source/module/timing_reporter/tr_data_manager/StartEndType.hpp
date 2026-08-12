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

enum class StartEndType
{
  kNone,
  kInToOut,
  kInToReg,
  kRegToOut,
  kRegToReg
};

struct GetStartEndTypeName
{
  std::string operator()(const StartEndType& start_end_type) const
  {
    std::string start_end_type_name;
    switch (start_end_type) {
      case StartEndType::kNone:
        start_end_type_name = "none";
        break;
      case StartEndType::kInToOut:
        start_end_type_name = "in_to_out";
        break;
      case StartEndType::kInToReg:
        start_end_type_name = "in_to_reg";
        break;
      case StartEndType::kRegToOut:
        start_end_type_name = "reg_to_out";
        break;
      case StartEndType::kRegToReg:
        start_end_type_name = "reg_to_reg";
        break;
      default:
        STALOG.error(Loc::current(), "Unrecognized type!");
        break;
    }
    return start_end_type_name;
  }
};

struct GetStartEndTypeReportName
{
  std::string operator()(const StartEndType& start_end_type) const
  {
    std::string start_end_type_report_name;
    switch (start_end_type) {
      case StartEndType::kNone:
        start_end_type_report_name = "none";
        break;
      case StartEndType::kInToOut:
        start_end_type_report_name = "in2out";
        break;
      case StartEndType::kInToReg:
        start_end_type_report_name = "in2reg";
        break;
      case StartEndType::kRegToOut:
        start_end_type_report_name = "reg2out";
        break;
      case StartEndType::kRegToReg:
        start_end_type_report_name = "reg2reg";
        break;
      default:
        STALOG.error(Loc::current(), "Unrecognized type!");
        break;
    }
    return start_end_type_report_name;
  }
};

}  // namespace ista
