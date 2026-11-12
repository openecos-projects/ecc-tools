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
#include "QorSummaryBuilder.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "ReportFormatter.hpp"
#include "ReportPathFilter.hpp"
#include "ReportPathSelector.hpp"
#include "TimingTolerance.hpp"
#include "network/Network.hpp"

namespace ista {
int32_t QorSummaryBuilder::getFanoutViolationNum()
{
  const std::vector<FanoutCheck>& checks = _database.fanoutChecks();
  return static_cast<int32_t>(std::count_if(checks.begin(), checks.end(), [](const FanoutCheck& check) { return check.violated(); }));
}

std::vector<std::string> QorSummaryBuilder::getQorSortedGroupList(std::map<std::string, double>& value_map)
{
  std::vector<std::string> group_list;
  for (std::pair<const std::string, double>& value_pair : value_map) {
    group_list.push_back(value_pair.first);
  }
  std::sort(group_list.begin(), group_list.end(), [&value_map](std::string& left, std::string& right) {
    if (reportSlackOrder(value_map.at(left)) != reportSlackOrder(value_map.at(right))) {
      return reportSlackOrder(value_map.at(left)) < reportSlackOrder(value_map.at(right));
    }
    return left < right;
  });
  return group_list;
}

double QorSummaryBuilder::getQorFrequency(TimingPath& timing_path)
{
  std::string clock_name = _report_path_context.getClockName(timing_path);
  double clock_period = _report_path_context.getClockPeriod(clock_name);
  double effective_period = clock_period - timing_path.get_slack();
  if (effective_period <= ista::kTimingTolerance) {
    return 0.0;
  }
  return 1000.0 / effective_period;
}

int32_t QorSummaryBuilder::getQorCellArea()
{
  Database& database = _database;
  double cell_area = 0.0;
  for (std::pair<const std::string, Instance>& instance_pair : database.network().instances()) {
    Instance& instance = database.network().instances()[instance_pair.first];
    std::map<std::string, LibertyCell>& timing_cell_map = database.liberty().get_cell_map();
    if (timing_cell_map.count(instance.get_cell_name()) > 0) {
      cell_area += timing_cell_map[instance.get_cell_name()].get_area();
    }
  }
  return static_cast<int32_t>(cell_area);
}

int32_t QorSummaryBuilder::getQorLeafCellK()
{
  return static_cast<int32_t>(_database.network().instances().size() / 1000);
}
QorSummary QorSummaryBuilder::build(ReportPathSelector& selector)
{
  QorSummary summary;
  Database& database = _database;

  for (PathGroup& timing_path_group : database.pathGroups()) {
    const std::string& group_name = timing_path_group.name();
    std::vector<TimingPath*> setup_timing_path_list = selector.endpointWorstPaths(timing_path_group, DelayType::kMax);
    if (!setup_timing_path_list.empty()) {
      summary.setup_wns_map[group_name] = setup_timing_path_list.front()->get_slack();
      summary.setup_tns_map[group_name] = 0.0;
      summary.setup_nvp_map[group_name] = 0;
      summary.setup_frequency_map[group_name] = getQorFrequency(*setup_timing_path_list.front());
      for (TimingPath* timing_path : setup_timing_path_list) {
        if (timing_path->get_slack() < 0.0) {
          summary.setup_tns_map[group_name] += timing_path->get_slack();
          summary.setup_nvp_map[group_name]++;
        }
      }
    }

    std::vector<TimingPath*> hold_timing_path_list = selector.endpointWorstPaths(timing_path_group, DelayType::kMin);
    if (!hold_timing_path_list.empty()) {
      summary.hold_wns_map[group_name] = hold_timing_path_list.front()->get_slack();
      summary.hold_tns_map[group_name] = 0.0;
      summary.hold_nvp_map[group_name] = 0;
      for (TimingPath* timing_path : hold_timing_path_list) {
        if (timing_path->get_slack() < 0.0) {
          summary.hold_tns_map[group_name] += timing_path->get_slack();
          summary.hold_nvp_map[group_name]++;
        }
      }
    }
  }

  for (std::pair<const std::string, double>& setup_wns_pair : summary.setup_wns_map) {
    summary.group_set.insert(setup_wns_pair.first);
  }
  for (std::pair<const std::string, double>& hold_wns_pair : summary.hold_wns_map) {
    summary.group_set.insert(hold_wns_pair.first);
  }

  for (std::string group_name : summary.group_set) {
    if (summary.setup_tns_map.count(group_name) > 0) {
      summary.total_setup_tns += summary.setup_tns_map[group_name];
    }
    if (summary.setup_nvp_map.count(group_name) > 0) {
      summary.total_setup_nvp += summary.setup_nvp_map[group_name];
    }
    if (summary.hold_tns_map.count(group_name) > 0) {
      summary.total_hold_tns += summary.hold_tns_map[group_name];
    }
    if (summary.hold_nvp_map.count(group_name) > 0) {
      summary.total_hold_nvp += summary.hold_nvp_map[group_name];
    }
  }

  summary.setup_group_list = getQorSortedGroupList(summary.setup_wns_map);
  summary.hold_group_list = getQorSortedGroupList(summary.hold_wns_map);
  summary.fanout_violations = getFanoutViolationNum();
  for (const auto& check : database.electricalChecks())
    if (check.violated()) {
      if (check.rule == ElectricalRule::kCapacitance)
        ++summary.capacitance_violations;
      else
        ++summary.transition_violations;
    }
  summary.cell_area = getQorCellArea();
  summary.leaf_cell_k = getQorLeafCellK();
  return summary;
}
}  // namespace ista
