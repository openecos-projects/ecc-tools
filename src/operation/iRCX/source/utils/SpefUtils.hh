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

#include "Types.hh"

namespace ircx {

inline Str escapeSpefIdentifier(Str name)
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

}  // namespace ircx
