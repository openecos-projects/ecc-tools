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

#include <string_view>

#include "Types.hh"
#include "log/Log.hh"

namespace ircx {

inline Str trimCopy(std::string_view value)
{
  const auto first = value.find_first_not_of(" \t\n\r\f\v");
  if (first == std::string_view::npos) {
    return "";
  }

  const auto last = value.find_last_not_of(" \t\n\r\f\v");
  return Str(value.substr(first, last - first + 1));
}

inline bool ensureNonEmpty(std::string_view value, std::string_view field_name)
{
  if (!value.empty()) {
    return true;
  }

  LOG_ERROR << "RCX field is empty: " << field_name;
  return false;
}

}  // namespace ircx
