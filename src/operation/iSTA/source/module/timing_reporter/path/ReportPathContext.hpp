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

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "Database.hpp"
#include "ReportOptions.hpp"

namespace ista {
struct ReportEndpoint
{
  std::string name;
  std::string description;
};
class ReportPathContext
{
 public:
  explicit ReportPathContext(const Database& database) : _database(database) {}
  bool isMatchAnalysisType(TimingPath& timing_path, DelayType delay_type);
  bool isMatchStartEndType(TimingPath& timing_path, StartEndType start_end_type);
  bool isPort(const std::string& pin_name);
  bool isRegisterStartPoint(const std::string& pin_name);
  bool isRegisterEndPoint(const std::string& pin_name);
  bool isClockSourceStartPoint(const std::string& pin_name);
  ReportEndpoint startPoint(TimingPath& timing_path);
  ReportEndpoint endPoint(TimingPath& timing_path);
  bool shouldOutputTimingPoint(TimingPath& timing_path, TimingPathPoint& path_point);
  std::string getLaunchClockEdgeText(TimingPath& timing_path);
  std::string getClockName(TimingPath& timing_path);
  std::string_view getClockNetworkDelayLabel(TimingPath& path, bool capture);
  double getClockPeriod(const std::string& clock_name);
  std::string getStartClockPin(TimingPath& timing_path);
  std::optional<std::size_t> getPinFanout(const std::string& pin_name);
  std::string getNumberString(double value);
  std::string getPointLabel(TimingPathPoint& path_point);
  std::string getReportPinName(const std::string& pin_name);
  std::string getLibraryCheckText(TimingPath& timing_path, DelayType delay_type);
  std::string getPinLabel(const std::string& pin_name);
  std::string getSlackStatus(TimingPath& timing_path);

 private:
  bool hasClockPoint(const std::string& pin_name);
  bool isInternalStartPoint(TimingPath& timing_path);
  bool isTieDrivenConstantOutput(const Instance& instance);
  std::optional<bool> getTieDriverValue(const std::string& pin_name);
  bool isTieHighCell(const Instance& instance);
  bool isTieLowCell(const Instance& instance);
  std::string getEndPointCheckText(TimingPath& timing_path);
  const Database& _database;
};
}  // namespace ista
