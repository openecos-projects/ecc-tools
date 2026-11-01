// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of the Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include <algorithm>
#include <cassert>
#include <cmath>
#include <string>
#include <vector>

#include "VcdParser.hh"

int main()
{
  vcd::VcdReader vcd_reader;
  assert(vcd_reader.read(VCD_TEST_FILE_PATH));

  std::map<std::string, vcd::VcdSignalActivity>& signal_activity_map = vcd_reader.get_signal_activity_map();
  assert(signal_activity_map.count("top/a") == 1);
  assert(signal_activity_map.count("top/alias_a") == 1);
  assert(signal_activity_map.count("top/u0/bus[1]") == 1);
  assert(signal_activity_map.count("top/u0/bus[0]") == 1);
  assert(signal_activity_map.count("top/u0/hexbus[3]") == 1);

  assert(std::fabs(signal_activity_map["top/a"].get_transition_density() - 2.0 / 35.0) < 1E-9);
  assert(std::fabs(signal_activity_map["top/a"].get_static_probability() - 10.0 / 35.0) < 1E-9);
  assert(std::fabs(signal_activity_map["top/alias_a"].get_transition_density() - 2.0 / 35.0) < 1E-9);
  assert(std::fabs(signal_activity_map["top/alias_a"].get_static_probability() - 10.0 / 35.0) < 1E-9);
  assert(std::fabs(signal_activity_map["top/u0/bus[1]"].get_transition_density() - 1.0 / 35.0) < 1E-9);
  assert(std::fabs(signal_activity_map["top/u0/bus[0]"].get_static_probability() - 20.0 / 35.0) < 1E-9);
  assert(std::fabs(signal_activity_map["top/u0/hexbus[3]"].get_transition_density() - 2.0 / 35.0) < 1E-9);
  assert(std::fabs(signal_activity_map["top/u0/hexbus[3]"].get_static_probability() - 20.0 / 35.0) < 1E-9);

  // Unknown excursions are weighted once, when the signal returns to a defined
  // value: a return-type excursion weighs the derate factor, a change-type
  // excursion weighs one whole transition, and an unknown state that is never
  // left (top/u0/bus[1] stays at z) or that the dump starts in (top/lead_x)
  // weighs nothing.
  assert(std::fabs(signal_activity_map["top/ret_x"].get_transition_density() - 0.5 / 35.0) < 1E-9);
  assert(std::fabs(signal_activity_map["top/ret_x"].get_static_probability() - 25.0 / 35.0) < 1E-9);
  assert(std::fabs(signal_activity_map["top/chg_x"].get_transition_density() - 1.0 / 35.0) < 1E-9);
  assert(std::fabs(signal_activity_map["top/lead_x"].get_transition_density() - 1.0 / 35.0) < 1E-9);

  // Activity models: the same value change is weighted differently by the averaged
  // and the time_based rule (PrimeTime PX UG 6-8 ~ 6-9 defines the X/Z handling in
  // the time_based chapter).  (last_value, value, last_defined_value) -> weight.
  {
    vcd::ActivityModelOptions averaged;
    vcd::ActivityModelOptions time_based;
    time_based.model = vcd::ActivityModel::kTimeBased;
    struct TransitionCase
    {
      char last_value;
      char value;
      char last_defined_value;
      double averaged_weight;
      double time_based_weight;
    };
    const TransitionCase transition_case_list[] = {
        {'0', '1', '0', 1.0, 1.0},    // plain 0 -> 1
        {'0', 'x', '0', 0.0, 0.5},    // leaving a defined value into X
        {'1', 'x', '1', 0.0, 0.5},
        {'x', '1', '0', 1.0, 0.5},    // X -> a different value
        {'x', '1', '1', 0.5, 0.5},    // X -> the previous value
        {'x', '0', '\0', 0.0, 0.0},  // initial X -> known: ignored by default
        {'0', 'z', '0', 0.0, 0.0},    // entering Z consumes no power
        {'z', '1', '0', 1.0, 1.0},    // 0 -> Z -> 1 is one full transition
        {'z', '1', '1', 0.5, 0.0},    // 1 -> Z -> 1 is not a transition
    };
    for (const TransitionCase& transition_case : transition_case_list) {
      const double averaged_weight = vcd::evaluateTransition(averaged, transition_case.last_value, transition_case.value,
                                                             transition_case.last_defined_value);
      const double time_based_weight = vcd::evaluateTransition(time_based, transition_case.last_value, transition_case.value,
                                                               transition_case.last_defined_value);
      assert(std::fabs(averaged_weight - transition_case.averaged_weight) < 1E-9);
      assert(std::fabs(time_based_weight - transition_case.time_based_weight) < 1E-9);
    }

    // The option has to reach the parser: top/ret_x is 1 -> X -> 1, which the
    // time_based rule counts as one transition instead of the averaged 0.5.
    vcd::ActivityModelOptions time_based_read;
    time_based_read.model = vcd::ActivityModel::kTimeBased;
    vcd::VcdReader time_based_reader;
    assert(time_based_reader.read(VCD_TEST_FILE_PATH, time_based_read));
    std::map<std::string, vcd::VcdSignalActivity>& time_based_activity_map = time_based_reader.get_signal_activity_map();
    assert(std::fabs(time_based_activity_map["top/ret_x"].get_transition_density() - 1.0 / 35.0) < 1E-9);
    assert(std::fabs(time_based_activity_map["top/a"].get_transition_density() - 2.0 / 35.0) < 1E-9);
  }

  // Windowed read: the file spans 35ns, so 10ns windows split it into
  // [10, 20), [20, 30), [30, 40) and the trailing partial window [40, 45).
  vcd::VcdWindowReader window_reader;
  assert(window_reader.open(VCD_TEST_FILE_PATH, 10.0));

  std::vector<std::string> signal_name_list;
  std::vector<double> sum_rise_list;
  std::vector<double> sum_fall_list;
  std::vector<double> sum_high_list;
  std::vector<double> a_rise_list;
  std::vector<double> a_fall_list;
  std::vector<double> a_probability_list;
  std::vector<double> duration_list;
  std::size_t a_index = 0;
  std::size_t window_num = 0;
  double total_duration = 0.0;
  while (window_reader.advance_window()) {
    const vcd::VcdWindowSignalActivity& window_activity = window_reader.get_window_activity();
    if (window_num == 0) {
      signal_name_list = window_activity.get_signal_name_list();
      sum_rise_list.assign(signal_name_list.size(), 0.0);
      sum_fall_list.assign(signal_name_list.size(), 0.0);
      sum_high_list.assign(signal_name_list.size(), 0.0);
      a_index = std::find(signal_name_list.begin(), signal_name_list.end(), "top/a") - signal_name_list.begin();
    }
    assert(window_activity.get_signal_name_list() == signal_name_list);
    const double window_duration = window_activity.get_window_duration();
    duration_list.push_back(window_duration);
    total_duration += window_duration;
    for (std::size_t signal_index = 0; signal_index < signal_name_list.size(); signal_index++) {
      sum_rise_list[signal_index] += window_activity.get_rise_transition_density(signal_index) * window_duration;
      sum_fall_list[signal_index] += window_activity.get_fall_transition_density(signal_index) * window_duration;
      sum_high_list[signal_index] += window_activity.get_static_probability(signal_index) * window_duration;
    }
    a_rise_list.push_back(window_activity.get_rise_transition_density(a_index));
    a_fall_list.push_back(window_activity.get_fall_transition_density(a_index));
    a_probability_list.push_back(window_activity.get_static_probability(a_index));
    window_num++;
  }
  assert(window_num == 4);
  const std::vector<double> expected_duration_list = {10.0, 10.0, 10.0, 5.0};
  assert(duration_list.size() == expected_duration_list.size());
  for (std::size_t window_index = 0; window_index < duration_list.size(); window_index++) {
    assert(std::fabs(duration_list[window_index] - expected_duration_list[window_index]) < 1E-9);
  }
  assert(total_duration == 35.0);

  // An event that lands exactly on a boundary belongs to the window on its right:
  // 'a' rises at 20ns, so [20, 30) is the window that owns the rise, and the
  // x -> 0 change-type transition at 40ns lands in the last window [40, 45).
  assert(std::fabs(a_rise_list[0] - 0.0) < 1E-9);
  assert(std::fabs(a_probability_list[0] - 0.0) < 1E-9);
  assert(std::fabs(a_rise_list[1] - 1.0 / 10.0) < 1E-9);
  assert(std::fabs(a_probability_list[1] - 1.0) < 1E-9);
  assert(std::fabs(a_rise_list[2] - 0.0) < 1E-9);
  assert(std::fabs(a_probability_list[2] - 0.0) < 1E-9);
  assert(std::fabs(a_rise_list[3] - 0.0) < 1E-9);
  assert(std::fabs(a_fall_list[3] - 1.0 / 5.0) < 1E-9);
  assert(std::fabs(a_probability_list[3] - 0.0) < 1E-9);

  // Summing the windows back up has to reproduce the whole-run numbers.
  for (std::size_t signal_index = 0; signal_index < signal_name_list.size(); signal_index++) {
    const std::string& signal_name = signal_name_list[signal_index];
    if (signal_activity_map.count(signal_name) == 0) {
      continue;
    }
    const double global_transition_density = signal_activity_map[signal_name].get_transition_density();
    const double global_static_probability = signal_activity_map[signal_name].get_static_probability();
    assert(std::fabs(global_transition_density * total_duration - (sum_rise_list[signal_index] + sum_fall_list[signal_index])) < 1E-9);
    assert(std::fabs(global_static_probability * total_duration - sum_high_list[signal_index]) < 1E-9);
  }
  return 0;
}
