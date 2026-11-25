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
#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <string>
#include <string_view>

#include "common/AnalysisType.hpp"
#include "common/PathSourceType.hpp"
#include "common/TransType.hpp"
#include "graph/ClockState.hpp"
#include "graph/TimingPathState.hpp"

namespace ista {

struct ClockTag;

// Pack (AnalysisType, PathSourceType, TransType) into one byte; all three
// enums are 0..2, so the combo is unique and ascending combo order equals the
// old nested-map iteration order.
inline constexpr uint8_t pathStateCombo(AnalysisType mode, PathSourceType source, TransType trans)
{
  return static_cast<uint8_t>(static_cast<int>(mode) * 9 + static_cast<int>(source) * 3 + static_cast<int>(trans));
}
inline constexpr AnalysisType pathStateComboMode(uint8_t combo)
{
  return static_cast<AnalysisType>(combo / 9);
}
inline constexpr PathSourceType pathStateComboSource(uint8_t combo)
{
  return static_cast<PathSourceType>((combo / 3) % 3);
}
inline constexpr TransType pathStateComboTrans(uint8_t combo)
{
  return static_cast<TransType>(combo % 3);
}

// Iterate one analysis mode's (source, trans, states) rows in the old nested
// order (source ordinal, then trans ordinal). A mode's combos are contiguous
// in the packed byte, so this is one range walk over the two-level map.
template <typename Map, typename F>
void forEachPathStateOfMode(Map& map, AnalysisType mode, F&& f)
{
  for (auto it = map.lower_bound(pathStateCombo(mode, PathSourceType::kNone, TransType::kNone));
       it != map.end() && pathStateComboMode(it->first) == mode; ++it)
    f(pathStateComboSource(it->first), pathStateComboTrans(it->first), it->second);
}
template <typename Map>
bool hasPathStateOfMode(const Map& map, AnalysisType mode)
{
  const auto it = map.lower_bound(pathStateCombo(mode, PathSourceType::kNone, TransType::kNone));
  return it != map.end() && pathStateComboMode(it->first) == mode;
}
// Id into the SDC clock table (sdc/Sdc.hpp ClockTable); 0 == unset. Both the
// map key and the default/launch selection use it — name resolution happens
// at the SDC boundary, never per probe.
using ClockId = ecc::symbol::Id<ClockTag>;

class TimingPoint
{
 public:
  TimingPoint() = default;
  ~TimingPoint() = default;
  // getter
  double get_arrival() const { return _arrival; }
  double get_required() const { return _required; }
  double get_setup_check_time() const { return _setup_check_time; }
  double get_slack() const { return _slack; }
  double get_launch_time() const { return _launch_time; }
  int32_t get_level() const { return _level; }
  // Reference-clock selection survives data propagation. The scalar arrival
  // summary may instead describe a data path launched by another clock.
  ClockId defaultClock() const { return _default_clock; }
  ClockId launchClock() const { return _launch_clock; }
  std::size_t get_predecessor_arc_idx() const { return _predecessor_arc_idx; }
  std::map<AnalysisType, std::map<TransType, double>>& get_data_slew_map() { return _data_slew_map; }
  // Path tags retain startpoint, launch clock and ordered exception-match state.
  // The (mode, source, transition) triple packs into one byte so the map is
  // two levels, not four; combo order equals the old nested lexicographic order.
  std::map<uint8_t, std::map<uint32_t, TimingPathState>>& get_path_state_map()
  {
    return _path_state_map;
  }
  bool get_is_clock_point() const { return _is_clock_point; }
  bool has_clock_state(ClockId clock) const
  {
    const auto* state = find_clock_state(clock);
    return state != nullptr && !state->is_source_path_only;
  }
  ClockState& get_clock_state(ClockId clock) { return _clock_state_map[clock]; }
  ClockState* find_clock_state(ClockId clock)
  {
    const auto state = _clock_state_map.find(clock);
    return state == _clock_state_map.end() ? nullptr : &state->second;
  }
  const ClockState* find_clock_state(ClockId clock) const
  {
    const auto state = _clock_state_map.find(clock);
    return state == _clock_state_map.end() ? nullptr : &state->second;
  }
  std::map<ClockId, ClockState>& get_clock_state_map() { return _clock_state_map; }
  const std::map<ClockId, ClockState>& get_clock_state_map() const { return _clock_state_map; }
  const ClockState* findDefaultClockState() const
  {
    const auto* state = find_clock_state(_default_clock);
    return state == nullptr || state->is_source_path_only ? nullptr : state;
  }
  void clearClockStates()
  {
    _clock_state_map.clear();
    _default_clock = ClockId{};
  }
  const auto& get_path_state_map() const { return _path_state_map; }
  // setter
  void set_arrival(const double arrival) { _arrival = arrival; }
  void set_required(const double required) { _required = required; }
  void set_setup_check_time(const double setup_check_time) { _setup_check_time = setup_check_time; }
  void set_slack(const double slack) { _slack = slack; }
  void set_launch_time(const double launch_time) { _launch_time = launch_time; }
  void set_level(const int32_t level) { _level = level; }
  void setDefaultClock(ClockId clock) { _default_clock = clock; }
  void setLaunchClock(ClockId clock) { _launch_clock = clock; }
  void set_predecessor_arc_idx(const std::size_t predecessor_arc_idx) { _predecessor_arc_idx = predecessor_arc_idx; }
  void set_data_slew_map(const std::map<AnalysisType, std::map<TransType, double>>& data_slew_map) { _data_slew_map = data_slew_map; }

  void set_is_clock_point(const bool is_clock_point) { _is_clock_point = is_clock_point; }
  // function

 private:
  double _arrival = -std::numeric_limits<double>::infinity();
  double _required = std::numeric_limits<double>::infinity();
  double _setup_check_time = 0.0;
  double _slack = 0.0;
  double _launch_time = 0.0;
  int32_t _level = 0;
  ClockId _default_clock;
  ClockId _launch_clock;
  std::size_t _predecessor_arc_idx = std::numeric_limits<std::size_t>::max();
  std::map<AnalysisType, std::map<TransType, double>> _data_slew_map;
  std::map<uint8_t, std::map<uint32_t, TimingPathState>> _path_state_map;
  bool _is_clock_point = false;
  std::map<ClockId, ClockState> _clock_state_map;
};

}  // namespace ista
