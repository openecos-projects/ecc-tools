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
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "LVSHeader.hpp"
namespace ilvs {

enum class ViolationType
{
  kNone,
  kIO,
  kInstance,
  kNet,
  kRoutingOpen,
  kRoutingShort,
  kPowerOpenVDD,
  kPowerOpenVSS
};

class GetViolationTypeName
{
 public:
  std::string operator()(const ViolationType& violation_type) const
  {
    switch (violation_type) {
      case ViolationType::kIO:
        return "IO";
      case ViolationType::kInstance:
        return "Instance";
      case ViolationType::kNet:
        return "Net";
      case ViolationType::kRoutingOpen:
        return "RoutingOpen";
      case ViolationType::kRoutingShort:
        return "RoutingShort";
      case ViolationType::kPowerOpenVDD:
        return "PowerOpenVDD";
      case ViolationType::kPowerOpenVSS:
        return "PowerOpenVSS";
      case ViolationType::kNone:
      default:
        return "None";
    }
  }
};

}  // namespace ilvs
