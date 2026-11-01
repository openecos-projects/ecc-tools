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
#include "ClockReportWriter.hpp"
#include "Config.hpp"
#include "Database.hpp"
#include "ElectricalReportWriter.hpp"
#include "FanoutReportWriter.hpp"
#include "PathReportWriter.hpp"
#include "QorReportWriter.hpp"
#include "ReportOptions.hpp"
#include "TimingJsonWriter.hpp"

namespace ista {
class TimingReporter
{
 public:
  explicit TimingReporter(Database& database, const Config& config)
      : _config(config),
        _fanout_report_writer(database, config),
        _electrical_report_writer(database, config),
        _clock_report_writer(database, config),
        _timing_json_writer(database, config),
        _path_report_writer(database, config),
        _qor_report_writer(database, config)
  {
  }
  void report();

 private:
  void writeReports();
  const Config& _config;
  FanoutReportWriter _fanout_report_writer;
  ElectricalReportWriter _electrical_report_writer;
  ClockReportWriter _clock_report_writer;
  TimingJsonWriter _timing_json_writer;
  PathReportWriter _path_report_writer;
  QorReportWriter _qor_report_writer;
};
}  // namespace ista
