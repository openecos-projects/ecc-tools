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
#include "TimingReporter.hpp"

#include "Logger.hpp"
#include "Monitor.hpp"
#include "ReportFormatter.hpp"
#include "ReportPathFilter.hpp"
#include "ReportPathSelector.hpp"
#include "network/Network.hpp"

namespace ista {
void TimingReporter::report()
{
  Monitor monitor;
  STALOG.info(Loc::current(), "Starting...");

  writeReports();

  STALOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void TimingReporter::writeReports()
{
  const bool output_reports = _config.output_timing_reports != 0;
  const bool output_features = _config.output_timing_features != 0;
  if (output_reports) {
    for (DelayType delay_type : getReportDelayTypeList()) {
      for (StartEndType start_end_type : getReportStartEndTypeList()) {
        _path_report_writer.write(delay_type, start_end_type);
      }
    }
  }
  if (output_reports || output_features) {
    _fanout_report_writer.write();
    _electrical_report_writer.write();
    _clock_report_writer.write();
    _qor_report_writer.write();
  }
  if (output_features) {
    _timing_json_writer.write();
  }
}
}  // namespace ista
