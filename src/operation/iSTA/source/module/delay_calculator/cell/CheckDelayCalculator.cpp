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
#include "CheckDelayCalculator.hpp"

#include <algorithm>
#include <functional>
#include <ranges>
#include <vector>
namespace ista {
double CheckDelayCalculator::calculate(TimingCheckArc& arc, AnalysisType analysis, TransType clock, TransType data, double clock_slew, double data_slew)
{
  double check_time = arc.get_check_time();
  std::vector<TimingArc*> candidate_arc_list = _selection.checkCandidates(arc, clock, data);
  if (!candidate_arc_list.empty()) {
    std::vector<double> delay_list;
    for (TimingArc* timing_arc : candidate_arc_list) {
      if (timing_arc->get_check_table_map().count(data) == 0) {
        continue;
      }
      // The table is on the constrained data/asynchronous pin. related_pin
      // names the clock for setup, hold, recovery and removal alike.
      double delay
          = timing_arc->get_check_table_map()[data].findValue(clock_slew * timing_arc->get_time_unit_scale(), data_slew * timing_arc->get_time_unit_scale());
      delay_list.push_back(delay / timing_arc->get_time_unit_scale());
    }
    if (!delay_list.empty()) {
      std::ranges::sort(delay_list, std::greater<double>());
      check_time = analysis == AnalysisType::kMin ? delay_list.back() : delay_list.front();
    }
  }

  return check_time;
}
}  // namespace ista
