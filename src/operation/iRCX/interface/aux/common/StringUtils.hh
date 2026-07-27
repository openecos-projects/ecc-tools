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

#include <charconv>

#include "Types.hh"
#include "Utility.hpp"
#include "log/Log.hh"

namespace ircx::string {

inline std::string trim(std::string_view value)
{
  return RCXUTIL.getTrimmedString(std::string(value));
}

inline std::string_view trimView(std::string_view value)
{
  size_t first_idx = value.find_first_not_of(" \t\n\r\f\v");
  if (first_idx == std::string_view::npos) {
    return {};
  }
  size_t last_idx = value.find_last_not_of(" \t\n\r\f\v");
  return value.substr(first_idx, last_idx - first_idx + 1);
}

inline bool startsWith(std::string_view value, std::string_view prefix)
{
  return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

inline bool equalsIgnoreCase(std::string_view lhs, std::string_view rhs)
{
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (size_t value_idx = 0; value_idx < lhs.size(); ++value_idx) {
    if (std::tolower(static_cast<unsigned char>(lhs[value_idx])) != std::tolower(static_cast<unsigned char>(rhs[value_idx]))) {
      return false;
    }
  }
  return true;
}

inline std::string_view takeToken(std::string_view& value)
{
  value = trimView(value);
  if (value.empty()) {
    return {};
  }
  size_t end_idx = value.find_first_of(" \t\n\r\f\v");
  if (end_idx == std::string_view::npos) {
    std::string_view token = value;
    value = {};
    return token;
  }
  std::string_view token = value.substr(0, end_idx);
  value = value.substr(end_idx + 1);
  return token;
}

inline bool contains(std::string_view value, std::string_view pattern)
{
  return value.find(pattern) != std::string_view::npos;
}

inline std::string toLower(std::string value)
{
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return value;
}

inline std::string identifier(std::string_view value, std::string_view fallback = "id")
{
  std::string text;
  text.reserve(value.size());
  for (char character : value) {
    if (std::isalnum(static_cast<unsigned char>(character)) || character == '_' || character == '$') {
      text.push_back(character);
    } else {
      text.push_back('_');
    }
  }
  if (text.empty()) {
    text = std::string(fallback);
  }
  if (std::isdigit(static_cast<unsigned char>(text.front()))) {
    text.insert(text.begin(), 'n');
  }
  return text;
}

inline std::optional<std::string_view> afterPrefix(std::string_view value, std::string_view prefix)
{
  if (!startsWith(value, prefix)) {
    return std::nullopt;
  }
  return value.substr(prefix.size());
}

template <typename T>
inline std::optional<T> parseNumber(std::string_view value)
{
  value = trimView(value);
  if (value.empty()) {
    return std::nullopt;
  }
  if constexpr (std::is_integral_v<T>) {
    T number = 0;
    const char* begin = value.data();
    const char* end = begin + value.size();
    std::from_chars_result result = std::from_chars(begin, end, number);
    return result.ec == std::errc() && result.ptr == end ? std::optional<T>(number) : std::nullopt;
  } else {
    std::string text(value);
    char* end_ptr = nullptr;
    double number = std::strtod(text.c_str(), &end_ptr);
    return end_ptr == text.c_str() + text.size() ? std::optional<T>(static_cast<T>(number)) : std::nullopt;
  }
}

template <typename T = int>
inline std::optional<T> parsePrefixedIndex(std::string_view value, char prefix = '*')
{
  if (value.size() < 2 || value.front() != prefix || !std::isdigit(static_cast<unsigned char>(value[1]))) {
    return std::nullopt;
  }
  return parseNumber<T>(value.substr(1));
}

template <typename T>
inline std::optional<T> parseAfterPrefix(std::string_view value, std::string_view prefix)
{
  std::optional<std::string_view> token_value = afterPrefix(value, prefix);
  if (!token_value.has_value()) {
    return std::nullopt;
  }
  return parseNumber<T>(*token_value);
}

inline std::optional<int> parseIntAfterPrefix(std::string_view value, std::string_view prefix)
{
  return parseAfterPrefix<int>(value, prefix);
}

inline std::optional<F64> parseDoubleAfterPrefix(std::string_view value, std::string_view prefix)
{
  return parseAfterPrefix<F64>(value, prefix);
}

inline bool requireNonEmpty(std::string_view value, std::string_view field_name)
{
  if (!value.empty()) {
    return true;
  }
  LOG_ERROR << "RCX field is empty: " << field_name;
  return false;
}

}  // namespace ircx::string
