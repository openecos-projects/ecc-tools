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
 * @file StringUtils.hh
 * @brief String trimming, parsing, and SPEF escaping helpers.
 */
#pragma once

#include <cerrno>
#include <charconv>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <optional>
#include <system_error>
#include <type_traits>

#include "Types.hh"
#include "log/Log.hh"

namespace ircx {
namespace string {

inline auto trim(std::string_view value) -> std::string
{
  const auto first = value.find_first_not_of(" \t\n\r\f\v");
  if (first == std::string_view::npos) {
    return "";
  }

  const auto last = value.find_last_not_of(" \t\n\r\f\v");
  return std::string(value.substr(first, last - first + 1));
}

inline auto trimView(std::string_view value) -> std::string_view
{
  const auto first = value.find_first_not_of(" \t\n\r\f\v");
  if (first == std::string_view::npos) {
    return {};
  }

  const auto last = value.find_last_not_of(" \t\n\r\f\v");
  return value.substr(first, last - first + 1);
}

inline auto startsWith(std::string_view value,
                        std::string_view prefix) -> bool
{
  return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

inline auto equalsIgnoreCase(std::string_view lhs,
                               std::string_view rhs) -> bool
{
  if (lhs.size() != rhs.size()) {
    return false;
  }

  for (Size idx = 0; idx < lhs.size(); ++idx) {
    const auto lhs_ch = static_cast<unsigned char>(lhs[idx]);
    const auto rhs_ch = static_cast<unsigned char>(rhs[idx]);
    if (std::tolower(lhs_ch) != std::tolower(rhs_ch)) {
      return false;
    }
  }
  return true;
}

inline auto takeToken(std::string_view& value) -> std::string_view
{
  value = trimView(value);
  if (value.empty()) {
    return {};
  }

  const auto end = value.find_first_of(" \t\n\r\f\v");
  if (end == std::string_view::npos) {
    const auto token = value;
    value = {};
    return token;
  }

  const auto token = value.substr(0, end);
  value = value.substr(end + 1);
  return token;
}

inline auto contains(std::string_view value,
                     std::string_view pattern) -> bool
{
  return value.find(pattern) != std::string_view::npos;
}

inline auto toLower(std::string value) -> std::string
{
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value;
}

inline auto identifier(std::string_view value,
                       std::string_view fallback = "id") -> std::string
{
  std::string text;
  text.reserve(value.size());
  for (char ch : value) {
    if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '$') {
      text.push_back(ch);
    } else {
      text.push_back('_');
    }
  }
  if (text.empty()) {
    text = fallback.empty() ? "id" : std::string(fallback);
  }
  if (std::isdigit(static_cast<unsigned char>(text.front()))) {
    text.insert(text.begin(), 'n');
  }
  return text;
}

inline auto afterPrefix(std::string_view value,
                         std::string_view prefix) -> std::optional<std::string_view>
{
  if (!startsWith(value, prefix)) {
    return std::nullopt;
  }
  return value.substr(prefix.size());
}

template <typename T>
inline auto parseNumber(std::string_view value) -> std::optional<T>
{
  value = trimView(value);
  if (value.empty()) {
    return std::nullopt;
  }

  if constexpr (std::is_integral_v<T>) {
    T number{};
    const auto* begin = value.data();
    const auto* end = begin + value.size();
    const auto [ptr, error] = std::from_chars(begin, end, number);
    if (error != std::errc{} || ptr != end) {
      return std::nullopt;
    }
    return number;
  } else if constexpr (std::is_floating_point_v<T>) {
    const std::string number_text(value);
    char* parse_end = nullptr;
    errno = 0;

    const auto number = std::strtod(number_text.c_str(), &parse_end);
    if (errno == ERANGE || parse_end != number_text.c_str() + number_text.size()) {
      return std::nullopt;
    }
    return static_cast<T>(number);
  } else {
    static_assert(std::is_arithmetic_v<T>, "parse_number only supports arithmetic types");
    return std::nullopt;
  }
}

template <typename T>
inline auto parseAfterPrefix(std::string_view value,
                               std::string_view prefix) -> std::optional<T>
{
  const auto token_value = afterPrefix(value, prefix);
  if (!token_value.has_value()) {
    return std::nullopt;
  }
  return parseNumber<T>(*token_value);
}

inline auto parseIntAfterPrefix(std::string_view value,
                                   std::string_view prefix) -> std::optional<int>
{
  return parseAfterPrefix<int>(value, prefix);
}

inline auto parseDoubleAfterPrefix(std::string_view value,
                                      std::string_view prefix) -> std::optional<F64>
{
  return parseAfterPrefix<F64>(value, prefix);
}

template <typename T = int>
inline auto parsePrefixedIndex(std::string_view value,
                                 char prefix = '*') -> std::optional<T>
{
  static_assert(std::is_integral_v<T>, "parse_prefixed_index requires an integral result type");
  if (value.size() < 2
      || value.front() != prefix
      || !std::isdigit(static_cast<unsigned char>(value[1]))) {
    return std::nullopt;
  }
  return parseNumber<T>(value.substr(1));
}

inline auto requireNonEmpty(std::string_view value,
                              std::string_view field_name) -> bool
{
  if (!value.empty()) {
    return true;
  }

  LOG_ERROR << "RCX field is empty: " << field_name;
  return false;
}

inline auto escapeSpefName(std::string name) -> std::string
{
  if (name.find('.') == std::string::npos) {
    return name;
  }

  std::string escaped_name;
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
