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

enum class ArcType
{
  kNone,
  kCell,
  kNet
};

struct GetArcTypeName
{
  std::string operator()(const ArcType& arc_type) const
  {
    std::string arc_type_name;
    switch (arc_type) {
      case ArcType::kNone:
        arc_type_name = "none";
        break;
      case ArcType::kCell:
        arc_type_name = "cell";
        break;
      case ArcType::kNet:
        arc_type_name = "net";
        break;
      default:
        STALOG.error(Loc::current(), "Unrecognized type!");
        break;
    }
    return arc_type_name;
  }
};

}  // namespace ista
