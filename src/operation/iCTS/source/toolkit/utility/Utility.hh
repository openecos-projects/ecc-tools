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
 * @file Utility.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-07-30
 * @brief Common timing and formatting utility helpers.
 */

#pragma once

#include <chrono>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

namespace icts {

struct MemoryReleaseStats
{
  bool supported = false;
  double rss_before_mb = 0.0;
  double rss_after_mb = 0.0;
};

class Utility final
{
 public:
  Utility() = delete;

  static auto getElapsedSeconds(std::chrono::steady_clock::time_point start_time) -> double;
  static auto currentRssMb() -> std::optional<double>;
  static auto releaseMemory() -> MemoryReleaseStats;

  template <typename... Args>
  static auto getString(Args&&... args) -> std::string
  {
    std::ostringstream stream;
    (stream << ... << std::forward<Args>(args));
    return stream.str();
  }

  static auto formatFixed(double value, int precision = 4) -> std::string
  {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(precision) << value;
    return stream.str();
  }

  template <typename T, typename U>
  static auto getPercentage(T numerator, U denominator, int precision = 2) -> std::string
  {
    const double ratio = denominator == 0 ? 0.0 : static_cast<double>(numerator) / static_cast<double>(denominator);
    return getString(formatFixed(ratio * 100.0, precision), " %");
  }
};

}  // namespace icts
