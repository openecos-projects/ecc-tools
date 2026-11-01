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
#include "FanoutReportWriter.hpp"

#include <fstream>
#include <iomanip>
#include <ios>
#include <string>
#include <vector>

#include "ReportFormatter.hpp"
#include "ReportPathFilter.hpp"
#include "ReportPathSelector.hpp"
#include "Utility.hpp"
#include "network/Network.hpp"

namespace ista {
void FanoutReportWriter::write()
{
  const std::vector<FanoutCheck>& checks = _database.fanoutChecks();
  const std::string directory = _config.tr_temp_directory_path;
  if (_config.output_timing_reports != 0) {
    auto report = STAUTIL.openOutputFile(directory + "max_fanout.rpt");
    report << "Maximum fanout checks (library fanout units)\n"
           << "Driver  Net  Fanout_load  Limit  Slack  Status\n";
    for (const FanoutCheck& check : checks) {
      report << check.get_driver_pin() << "  " << check.get_net_name() << "  " << std::defaultfloat << std::setprecision(9) << check.get_fanout_load() << "  "
             << check.get_limit() << "  " << check.get_slack() << "  " << (check.violated() ? "VIOLATED" : "MET") << "\n";
    }
    report << "Checked drivers: " << checks.size() << "; violations: " << _qor_summary_builder.getFanoutViolationNum() << "\n";
    STAUTIL.finishOutputFile(report);
  }
  if (_config.output_timing_features != 0) {
    auto json = STAUTIL.openOutputFile(directory + "max_fanout.json");
    json << "{\"violation_count\":" << _qor_summary_builder.getFanoutViolationNum() << ",\"checks\":[";
    bool first = true;
    for (const FanoutCheck& check : checks) {
      json << (first ? "" : ",") << "{\"driver\":\"" << escapeJsonString(check.get_driver_pin()) << "\",\"net\":\"" << escapeJsonString(check.get_net_name())
           << "\",\"fanout_load\":";
      outputJsonNumber(json, check.get_fanout_load());
      json << ",\"limit\":";
      outputJsonNumber(json, check.get_limit());
      json << ",\"slack\":";
      outputJsonNumber(json, check.get_slack());
      json << "}";
      first = false;
    }
    json << "]}\n";
    STAUTIL.finishOutputFile(json);
  }
}
}  // namespace ista
