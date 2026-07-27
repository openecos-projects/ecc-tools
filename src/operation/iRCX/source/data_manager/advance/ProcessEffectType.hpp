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
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "Logger.hpp"

namespace ircx {

enum class ProcessEffectType
{
  kNone,
  kBoth,
  kResistance,
  kCapacitance
};

struct GetProcessEffectType
{
  ProcessEffectType operator()(const std::string& process_effect_type_name) const
  {
    if (process_effect_type_name == "RESISTIVE_ONLY") {
      return ProcessEffectType::kResistance;
    }
    if (process_effect_type_name == "CAPACITIVE_ONLY") {
      return ProcessEffectType::kCapacitance;
    }
    return ProcessEffectType::kNone;
  }
};

}  // namespace ircx
