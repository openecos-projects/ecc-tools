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
 * @file LogTable.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-07-30
 * @brief Stateless ASCII table rendering and CTS Logger emission.
 */

#include "LogTable.hh"

#include <algorithm>
#include <cstddef>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace icts {
namespace {

constexpr std::size_t kMinimumTitleWidth = 72U;

auto normalizeCell(std::string value) -> std::string
{
  std::ranges::replace(value, '\n', ' ');
  std::ranges::replace(value, '\r', ' ');
  return value;
}

auto padRight(const std::string& value, std::size_t width) -> std::string
{
  return value.size() >= width ? value : value + std::string(width - value.size(), ' ');
}

auto makeBorder(const std::vector<std::size_t>& widths) -> std::string
{
  std::string border = "+";
  for (const auto width : widths) {
    border += std::string(width + 2U, '-');
    border += "+";
  }
  return border;
}

auto makeRow(const LogTableRow& row, const std::vector<std::size_t>& widths) -> std::string
{
  std::ostringstream stream;
  stream << "|";
  for (std::size_t index = 0U; index < widths.size(); ++index) {
    stream << " " << padRight(index < row.size() ? normalizeCell(row[index]) : std::string{}, widths[index]) << " |";
  }
  return stream.str();
}

auto makeTitle(std::string_view title, std::size_t width) -> std::string
{
  const auto centered_title = " " + std::string(title) + " ";
  const auto left_fill = centered_title.size() < width ? (width - centered_title.size()) / 2U : 0U;
  const auto right_fill = centered_title.size() < width ? width - centered_title.size() - left_fill : 0U;
  return std::string(left_fill, '=') + centered_title + std::string(right_fill, '=');
}

}  // namespace

auto RenderLogTable(std::string_view title, const std::vector<std::string>& headers, const LogTableRows& rows) -> std::string
{
  std::size_t column_count = headers.size();
  for (const auto& row : rows) {
    column_count = std::max(column_count, row.size());
  }
  if (column_count == 0U) {
    return makeTitle(title, kMinimumTitleWidth);
  }

  std::vector<std::size_t> widths(column_count, 0U);
  for (std::size_t index = 0U; index < headers.size(); ++index) {
    widths[index] = normalizeCell(headers[index]).size();
  }
  for (const auto& row : rows) {
    for (std::size_t index = 0U; index < row.size(); ++index) {
      widths[index] = std::max(widths[index], normalizeCell(row[index]).size());
    }
  }

  const auto border = makeBorder(widths);
  std::ostringstream stream;
  stream << makeTitle(title, std::max(kMinimumTitleWidth, border.size())) << '\n';
  stream << border << '\n' << makeRow(headers, widths) << '\n' << border << '\n';
  for (const auto& row : rows) {
    stream << makeRow(row, widths) << '\n';
  }
  stream << border;
  return stream.str();
}

auto EmitLogTableText(Loc location, std::string_view table_text) -> void
{
  std::istringstream stream{std::string(table_text)};
  std::string line;
  while (std::getline(stream, line)) {
    CTSLOG.info(location, line);
  }
}

auto EmitLogTable(Loc location, std::string_view title, const std::vector<std::string>& headers, const LogTableRows& rows) -> void
{
  EmitLogTableText(location, RenderLogTable(title, headers, rows));
}

}  // namespace icts
