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

enum class CapTableType
{
  kNone,
  kA,
  kB
};

struct GetCapTableTypeName
{
  std::string operator()(const CapTableType& cap_table_type) const
  {
    std::string cap_table_type_name;
    switch (cap_table_type) {
      case CapTableType::kNone:
        cap_table_type_name = "none";
        break;
      case CapTableType::kA:
        cap_table_type_name = "A";
        break;
      case CapTableType::kB:
        cap_table_type_name = "B";
        break;
      default:
        RCXLOG.error(Loc::current(), "Unrecognized type!");
        break;
    }
    return cap_table_type_name;
  }
};

struct GetCapTableType
{
  CapTableType operator()(const std::string& cap_table_type_name) const
  {
    if (cap_table_type_name == "A") {
      return CapTableType::kA;
    }
    if (cap_table_type_name == "B") {
      return CapTableType::kB;
    }
    RCXLOG.error(Loc::current(), "Unrecognized type!");
    return CapTableType::kNone;
  }
};

}  // namespace ircx
