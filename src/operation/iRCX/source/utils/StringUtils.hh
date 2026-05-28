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
namespace string {

inline auto trim(std::string_view value) -> Str
{
  const auto first = value.find_first_not_of(" \t\n\r\f\v");
  if (first == std::string_view::npos) {
    return "";
  }

  const auto last = value.find_last_not_of(" \t\n\r\f\v");
  return Str(value.substr(first, last - first + 1));
}

inline auto ensure_non_empty(std::string_view value, std::string_view field_name) -> bool
{
  if (!value.empty()) {
    return true;
  }

  LOG_ERROR << "RCX field is empty: " << field_name;
  return false;
}

inline auto spef_escape_identifier(Str name) -> Str
{
  if (name.find('.') == Str::npos) {
    return name;
  }

  Str escaped_name;
  escaped_name.reserve(name.size());
  for (Size idx = 0; idx < name.size(); ++idx) {
    const char current_char = name[idx];
    const bool needs_escape =
        current_char == '.' || current_char == '[' || current_char == ']';
    if (needs_escape && (idx == 0 || name[idx - 1] != '\\')) {
      escaped_name.push_back('\\');
    }
    escaped_name.push_back(current_char);
  }

  return escaped_name;
}

}  // namespace string
}  // namespace ircx
