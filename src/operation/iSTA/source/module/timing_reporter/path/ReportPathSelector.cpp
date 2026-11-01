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
#include "ReportPathSelector.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "ReportFormatter.hpp"
#include "ReportPathFilter.hpp"
#include "TimingTolerance.hpp"
#include "network/Network.hpp"

namespace ista {
std::vector<std::pair<std::string, TimingPath*>> ReportPathSelector::reportPaths(DelayType delay_type, StartEndType start_end_type)
{
  Database& database = _database;
  std::vector<std::pair<std::string, TimingPath*>> timing_path_list;
  for (PathGroup& timing_path_group : database.pathGroups()) {
    const std::string& group_name = timing_path_group.name();
    for (auto& [end_point, timing_path_end] : timing_path_group.endpointPaths()) {
      for (TimingPath& timing_path : timing_path_end) {
        if (_report_path_context.isMatchAnalysisType(timing_path, delay_type) && _report_path_context.isMatchStartEndType(timing_path, start_end_type)
            && isMatchReportSlack(timing_path)) {
          timing_path_list.emplace_back(group_name, &timing_path);
        }
      }
    }
  }

  return selectCandidates(std::move(timing_path_list), _config.path_report_number, _config.endpoint_path_report_number);
}

std::vector<TimingPath*> ReportPathSelector::getEndpointWorstTimingPathList(std::vector<TimingPath*>& timing_path_list)
{
  std::map<std::string, TimingPath*> end_point_timing_path_map;
  for (TimingPath* timing_path : timing_path_list) {
    std::string& end_point = timing_path->get_end_point();
    if (end_point_timing_path_map.count(end_point) == 0 || less({"", timing_path}, {"", end_point_timing_path_map.at(end_point)})) {
      end_point_timing_path_map[end_point] = timing_path;
    }
  }

  std::vector<TimingPath*> endpoint_worst_timing_path_list;
  for (std::pair<const std::string, TimingPath*>& timing_path_pair : end_point_timing_path_map) {
    endpoint_worst_timing_path_list.push_back(timing_path_pair.second);
  }
  return endpoint_worst_timing_path_list;
}

bool ReportPathSelector::isMatchReportSlack(TimingPath& timing_path)
{
  return getReportSlackFilter(_config).accepts(timing_path.get_slack());
}

std::vector<TimingPath*> ReportPathSelector::endpointWorstPaths(PathGroup& timing_path_group, DelayType delay_type)
{
  std::vector<TimingPath*> timing_path_list;
  for (auto& timing_path_end_pair : timing_path_group.endpointPaths()) {
    for (TimingPath& timing_path : timing_path_end_pair.second) {
      if (_report_path_context.isMatchAnalysisType(timing_path, delay_type)) {
        timing_path_list.push_back(&timing_path);
      }
    }
  }
  timing_path_list = getEndpointWorstTimingPathList(timing_path_list);
  std::sort(timing_path_list.begin(), timing_path_list.end(), [](TimingPath* left, TimingPath* right) { return less({"", left}, {"", right}); });
  return timing_path_list;
}
bool ReportPathSelector::less(const std::pair<std::string, TimingPath*>& left, const std::pair<std::string, TimingPath*>& right)
{
  auto key = [](const auto& entry) {
    auto& path = *entry.second;
    return std::tuple{reportSlackOrder(path.get_slack()),
                      std::string_view(entry.first),
                      std::string_view(path.get_start_point()),
                      std::string_view(path.get_end_point()),
                      std::string_view(path.get_clock_name()),
                      std::string_view(path.get_capture_clock_name()),
                      path.get_capture_clock_transition(),
                      path.get_trans_type(),
                      path.get_analysis_type(),
                      path.get_source_type()};
  };
  if (key(left) != key(right))
    return key(left) < key(right);
  auto point_less = [](auto& a, auto& b) {
    return std::tuple{std::string_view(a.get_pin_name()), a.get_trans_type(), reportSlackOrder(a.get_arrival())}
           < std::tuple{std::string_view(b.get_pin_name()), b.get_trans_type(), reportSlackOrder(b.get_arrival())};
  };
  auto& a = left.second->get_point_list();
  auto& b = right.second->get_point_list();
  return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), point_less);
}

std::vector<std::pair<std::string, TimingPath*>> ReportPathSelector::selectCandidates(std::vector<std::pair<std::string, TimingPath*>> timing_path_list,
                                                                                      int32_t path_report_number, int32_t endpoint_path_report_number)
{
  std::sort(timing_path_list.begin(), timing_path_list.end(), less);

  std::map<std::string, int32_t> endpoint_path_count_map;
  std::vector<std::pair<std::string, TimingPath*>> endpoint_limited_timing_path_list;
  for (std::pair<std::string, TimingPath*>& timing_path_pair : timing_path_list) {
    std::string& end_point = timing_path_pair.second->get_end_point();
    if (endpoint_path_count_map[end_point] >= endpoint_path_report_number) {
      continue;
    }
    endpoint_path_count_map[end_point]++;
    endpoint_limited_timing_path_list.push_back(timing_path_pair);
  }

  std::vector<std::pair<std::string, TimingPath*>> report_timing_path_list;
  for (std::pair<std::string, TimingPath*>& timing_path_pair : endpoint_limited_timing_path_list) {
    if (static_cast<int32_t>(report_timing_path_list.size()) >= path_report_number) {
      break;
    }
    report_timing_path_list.push_back(timing_path_pair);
  }
  return report_timing_path_list;
}
}  // namespace ista
