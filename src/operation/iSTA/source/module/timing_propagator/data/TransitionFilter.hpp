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
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include <string>
#include <string_view>

#include "graph/SimValues.hpp"
#include "network/Network.hpp"
namespace ista {
namespace {
inline bool allowsTransition(const Network& network, const SimValues& values, std::string_view pin_name, TransType trans_type, bool driver)
{
  const auto pin = network.pins().find(std::string(pin_name));
  const bool separate_driver = driver && pin != network.pins().end() && pin->second.get_direction() == PinDirection::kInout;
  return values.allowsTransition(pin_name, trans_type, separate_driver);
}
}  // namespace
}  // namespace ista
