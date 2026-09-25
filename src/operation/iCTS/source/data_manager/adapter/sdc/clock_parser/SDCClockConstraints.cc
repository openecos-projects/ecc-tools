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
 * @file SDCClockConstraints.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-09-05
 * @brief SDC clock transition facts, the only clock-domain constraint iCTS reads
 *        besides clock identity and case analysis.
 */

#include <algorithm>
#include <string>
#include <utility>

#include "SDCClockParser.hh"

namespace icts::sdc_reader {

auto SdcSubsetEvaluator::evaluateClockTransition(const std::vector<SdcValue>& args) -> void
{
  const std::string command = "set_clock_transition";
  const auto parsed = parseOptions(command, args, {}, {"-rise", "-fall", "-min", "-max"});
  if (!parsed) {
    return;
  }
  SdcClockTransition transition;
  if (!readScalarObjects(command, *parsed, SdcObjectKind::kClock, transition.value_ns, transition.clocks)) {
    return;
  }
  if (transition.value_ns < 0.0) {
    reportIssue(SdcConstraintStatusCode::kMalformed, command, "negative_transition");
    return;
  }
  if (!scaleNumber(command, transition.value_ns, _time_unit_ns)) {
    return;
  }
  transition.transition = OptionTransition(*parsed);
  SelectBothUnlessOne(*parsed, "-min", "-max", transition.min, transition.max);
  _data.clock_transitions.push_back(std::move(transition));
}

}  // namespace icts::sdc_reader
