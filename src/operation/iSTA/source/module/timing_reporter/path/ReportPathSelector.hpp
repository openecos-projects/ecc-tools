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
#include <string>
#include <utility>
#include <vector>

#include "Config.hpp"
#include "Database.hpp"
#include "ReportOptions.hpp"
#include "ReportPathContext.hpp"

namespace ista {
// Exact numeric ordering; NaNs follow all numeric values, including infinities.
// Timing tolerances belong to selection/reduction, never to a sort comparator.
inline std::pair<bool, double> reportSlackOrder(double value)
{
  return {std::isnan(value), std::isnan(value) ? 0.0 : value};
}

class ReportPathSelector
{
 public:
  explicit ReportPathSelector(Database& database, const Config& config) : _database(database), _config(config), _report_path_context(database) {}
  std::vector<std::pair<std::string, TimingPath*>> reportPaths(DelayType delay_type, StartEndType start_end_type);
  // Candidates are already filtered and in original traversal order.
  // Borrowed paths must remain valid through output; selection never modifies them.
  static std::vector<std::pair<std::string, TimingPath*>> selectCandidates(std::vector<std::pair<std::string, TimingPath*>> candidates, int32_t max_paths,
                                                                           int32_t paths_per_endpoint);
  static bool less(const std::pair<std::string, TimingPath*>& left, const std::pair<std::string, TimingPath*>& right);
  std::vector<TimingPath*> endpointWorstPaths(PathGroup& timing_path_group, DelayType delay_type);

 private:
  std::vector<TimingPath*> getEndpointWorstTimingPathList(std::vector<TimingPath*>& timing_path_list);
  bool isMatchReportSlack(TimingPath& timing_path);
  Database& _database;
  const Config& _config;
  ReportPathContext _report_path_context;
};
}  // namespace ista
