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
/**
 * @file FastSTACondition.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-09-05
 * @brief Exact Liberty Boolean conditions under partial case assignments.
 */

#pragma once

#include <functional>
#include <optional>
#include <string>

namespace icts {

enum class FastStaLogicValue
{
  kZero,
  kOne,
  kUnknown,
  kInvalid
};

class FastStaCondition
{
 public:
  using PinValueLookup = std::function<std::optional<bool>(const std::string&)>;

  static auto evaluate(const std::string& expression, const PinValueLookup& lookup) -> FastStaLogicValue;
  static auto isSensitized(const std::string& expression, const std::string& switching_pin, const PinValueLookup& lookup) -> FastStaLogicValue;
};

}  // namespace icts
