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

#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <ios>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "Config.hpp"
#include "ReportOptions.hpp"
#include "ReportPathFilter.hpp"
#include "TimingTolerance.hpp"
namespace ista {
namespace {

constexpr int kTimingFanoutWidth = 8;
constexpr int kTimingValueWidth = 16;
constexpr int kTimingTableExtraWidth = 2 + kTimingFanoutWidth + 2 * kTimingValueWidth + 2;

inline std::string escapeJsonString(const std::string& value)
{
  static constexpr char kHexDigits[] = "0123456789abcdef";

  std::string escaped;
  escaped.reserve(value.size());
  for (unsigned char character : value) {
    switch (character) {
      case '"':
        escaped += "\\\"";
        break;
      case '\\':
        escaped += "\\\\";
        break;
      case '\b':
        escaped += "\\b";
        break;
      case '\f':
        escaped += "\\f";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        if (character < 0x20) {
          escaped += "\\u00";
          escaped += kHexDigits[character >> 4];
          escaped += kHexDigits[character & 0x0f];
        } else {
          escaped += static_cast<char>(character);
        }
    }
  }
  return escaped;
}

inline void outputJsonNumber(std::ostream& json_file, double value)
{
  if (!std::isfinite(value)) {
    json_file << "null";
    return;
  }
  // Machine-readable values must survive a double round trip. Fixed decimal
  // precision and timing tolerances can hide electrical violations in pF.
  json_file << std::defaultfloat << std::setprecision(std::numeric_limits<double>::max_digits10) << (value == 0.0 ? 0.0 : value);
}

inline std::string stableTimingPathId(const std::string& value)
{
  uint64_t hash = 14695981039346656037ULL;
  for (unsigned char character : value) {
    hash ^= character;
    hash *= 1099511628211ULL;
  }
  std::ostringstream stream;
  stream << "timing_path_" << std::hex << hash;
  return stream.str();
}

inline std::vector<DelayType> getReportDelayTypeList()
{
  return {DelayType::kMax, DelayType::kMin};
}

inline std::vector<StartEndType> getReportStartEndTypeList()
{
  return {StartEndType::kInToOut, StartEndType::kInToReg, StartEndType::kRegToOut, StartEndType::kRegToReg};
}

inline ReportSlackFilter getReportSlackFilter(const Config& config)
{
  return {config.has_timing_report_slack_lesser_than, config.timing_report_slack_lesser_than, config.has_timing_report_slack_greater_than,
          config.timing_report_slack_greater_than,
          !config.has_timing_report_slack_lesser_than && !config.has_timing_report_slack_greater_than
              && (config.path_report_number > 1 || config.endpoint_path_report_number > 1)};
}

}  // namespace
}  // namespace ista
