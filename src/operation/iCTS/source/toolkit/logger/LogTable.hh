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
 * @file LogTable.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-07-30
 * @brief Stateless ASCII table rendering and CTS Logger emission.
 */

#pragma once

#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "Logger.hh"

namespace icts {

using LogTableRow = std::vector<std::string>;
using LogTableRows = std::vector<LogTableRow>;

template <typename Value>
auto ToLogTableCell(const Value& value) -> std::string
{
  std::ostringstream stream;
  stream << std::boolalpha << value;
  return stream.str();
}

auto RenderLogTable(std::string_view title, const std::vector<std::string>& headers, const LogTableRows& rows) -> std::string;
auto EmitLogTableText(Loc location, std::string_view table_text) -> void;
auto EmitLogTable(Loc location, std::string_view title, const std::vector<std::string>& headers, const LogTableRows& rows) -> void;

}  // namespace icts
