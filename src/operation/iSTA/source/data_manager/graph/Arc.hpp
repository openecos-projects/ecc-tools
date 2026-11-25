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
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "common/AnalysisType.hpp"
#include "common/TransType.hpp"
#include "graph/ArcType.hpp"
#include "graph/ClockState.hpp"
#include "liberty/LibertyArcId.hpp"

namespace ista {

// Slot index for an (analysis, input edge, output edge) triple; only
// kMax/kMin x kRise/kFall x kRise/kFall exist as keys — anything else behaves
// as "no key", matching a std::map miss. Ordinal slot order matches the old
// nested-map iteration order.
inline constexpr int arcIoSlot(AnalysisType mode, TransType in, TransType out)
{
  return (static_cast<int>(mode) - 1) * 4 + (static_cast<int>(in) - 1) * 2 + (static_cast<int>(out) - 1);
}
inline constexpr bool arcIoSlotValid(AnalysisType mode, TransType in, TransType out)
{
  return (mode == AnalysisType::kMax || mode == AnalysisType::kMin) && (in == TransType::kRise || in == TransType::kFall)
         && (out == TransType::kRise || out == TransType::kFall);
}

// Fixed 2x2x2 grid replacing map<AnalysisType, map<TransType, map<TransType, T>>>.
// Presence bits reproduce key-existence semantics: absent != stored zero.
template <typename T>
class ArcIoGrid
{
 public:
  std::optional<T> get(AnalysisType mode, TransType in, TransType out) const
  {
    if (!arcIoSlotValid(mode, in, out))
      return std::nullopt;
    const int slot = arcIoSlot(mode, in, out);
    if (!(_present & (1 << slot)))
      return std::nullopt;
    return _values[slot];
  }
  void set(AnalysisType mode, TransType in, TransType out, const T& value)
  {
    if (!arcIoSlotValid(mode, in, out))
      return;
    const int slot = arcIoSlot(mode, in, out);
    _values[slot] = value;
    _present |= (1 << slot);
  }
  void clear() { _present = 0; }
  bool empty() const { return _present == 0; }
  friend bool operator==(const ArcIoGrid& lhs, const ArcIoGrid& rhs)
  {
    return lhs._present == rhs._present && (!lhs._present || lhs._values == rhs._values);
  }
  friend bool operator!=(const ArcIoGrid& lhs, const ArcIoGrid& rhs) { return !(lhs == rhs); }
  // Present output edges for one (mode, input) pair, in ordinal order — the
  // old innermost-map key iteration.
  template <typename F>
  void eachOutput(AnalysisType mode, TransType in, F&& f) const
  {
    for (int out : {1, 2}) {
      const int slot = arcIoSlot(mode, in, static_cast<TransType>(out));
      if (_present & (1 << slot))
        f(static_cast<TransType>(out), _values[slot]);
    }
  }
  template <typename F>
  void each(F&& f) const
  {
    for (int mode : {1, 2})
      for (int in : {1, 2})
        for (int out : {1, 2}) {
          const int slot = arcIoSlot(static_cast<AnalysisType>(mode), static_cast<TransType>(in), static_cast<TransType>(out));
          if (_present & (1 << slot))
            f(static_cast<AnalysisType>(mode), static_cast<TransType>(in), static_cast<TransType>(out), _values[slot]);
        }
  }

 private:
  std::array<T, 8> _values{};
  uint8_t _present = 0;
};

class Arc
{
 public:
  Arc() = default;
  ~Arc() = default;
  // getter
  bool rcFailed(AnalysisType mode, TransType edge) const
  {
    return clockWaveSlotValid(mode, edge) && (_rc_failures & (1 << clockWaveSlot(mode, edge)));
  }
  // A failed crossing cannot establish a reliable bound in either analysis.
  bool rcNonbounding() const { return _rc_failures != 0; }
  void setRcFailed(AnalysisType mode, TransType edge)
  {
    if (clockWaveSlotValid(mode, edge))
      _rc_failures |= (1 << clockWaveSlot(mode, edge));
  }
  void clearRcFailures() { _rc_failures = 0; }
  int32_t get_source_vertex_idx() const { return _source_vertex_idx; }
  int32_t get_sink_vertex_idx() const { return _sink_vertex_idx; }
  std::string& get_arc_name() { return _arc_name; }
  std::string& get_source_pin() { return _source_pin; }
  const std::string& get_source_pin() const { return _source_pin; }
  std::string& get_sink_pin() { return _sink_pin; }
  const std::string& get_sink_pin() const { return _sink_pin; }
  std::string& get_owner_name() { return _owner_name; }
  std::string& get_library_source_port() { return _library_source_port; }
  std::string& get_library_sink_port() { return _library_sink_port; }
  ArcType get_type() const { return _type; }
  double get_delay() const { return _delay; }
  double get_delay_max() const { return _delay_max; }
  double get_delay_min() const { return _delay_min; }
  ClockWaveGrid<double>& get_trans_delay_map() { return _trans_delay_map; }
  ArcIoGrid<double>& get_input_output_delay_map() { return _input_output_delay_map; }
  ArcIoGrid<double>& get_graph_delay_map() { return _graph_delay_map; }
  // Per liberty-arc-variant delay grids; linear scan — a cell arc has at most
  // a handful of conditional variants sharing one graph arc.
  std::vector<std::pair<int32_t, ArcIoGrid<double>>>& get_timing_arc_delay_map() { return _timing_arc_delay_map; }
  void eraseTimingArcDelay(int32_t timing_arc_idx)
  {
    for (std::size_t pos = 0; pos < _timing_arc_delay_map.size(); ++pos)
      if (_timing_arc_delay_map[pos].first == timing_arc_idx) {
        _timing_arc_delay_map.erase(_timing_arc_delay_map.begin() + pos);
        _timing_arc_delay_fallback_masks.erase(_timing_arc_delay_fallback_masks.begin() + pos);
        _timing_arc_delay_zero_slew_masks.erase(_timing_arc_delay_zero_slew_masks.begin() + pos);
        return;
      }
  }
  const ArcIoGrid<double>* findTimingArcDelay(int32_t timing_arc_idx) const
  {
    for (const auto& [idx, grid] : _timing_arc_delay_map)
      if (idx == timing_arc_idx)
        return &grid;
    return nullptr;
  }
  const std::optional<LibertyArcId>& libertyArcId() const { return _liberty_arc_id; }
  bool get_is_clock_arc() const { return _is_clock_arc; }
  bool get_is_disable_arc() const { return _is_disable_arc || _is_case_analysis_disable; }
  bool get_is_case_analysis_disable() const { return _is_case_analysis_disable; }
  bool get_is_loop_disable() const { return _is_loop_disable; }
  // setter
  void set_source_vertex_idx(int32_t idx) { _source_vertex_idx = idx; }
  void set_sink_vertex_idx(int32_t idx) { _sink_vertex_idx = idx; }
  void set_arc_name(const std::string& arc_name) { _arc_name = arc_name; }
  void set_source_pin(const std::string& source_pin) { _source_pin = source_pin; }
  void set_sink_pin(const std::string& sink_pin) { _sink_pin = sink_pin; }
  void set_owner_name(const std::string& owner_name) { _owner_name = owner_name; }
  void set_library_source_port(const std::string& library_source_port) { _library_source_port = library_source_port; }
  void set_library_sink_port(const std::string& library_sink_port) { _library_sink_port = library_sink_port; }
  void set_type(const ArcType& type) { _type = type; }
  void set_delay(const double delay) { _delay = delay; }
  void set_delay_max(const double delay_max) { _delay_max = delay_max; }
  void set_delay_min(const double delay_min) { _delay_min = delay_min; }
  void setLibertyArcId(std::optional<LibertyArcId> id) { _liberty_arc_id = std::move(id); }
  void set_is_clock_arc(const bool is_clock_arc) { _is_clock_arc = is_clock_arc; }
  void set_is_disable_arc(const bool is_disable_arc) { _is_disable_arc = is_disable_arc; }
  void set_is_case_analysis_disable(const bool value) { _is_case_analysis_disable = value; }
  void set_is_loop_disable(const bool is_loop_disable) { _is_loop_disable = is_loop_disable; }
  // function
  // Find or append the grid for a liberty arc variant; position is stable, so
  // the fallback/zero-slew bitmasks index by position.
  std::pair<ArcIoGrid<double>&, std::size_t> timingArcDelaySlot(int32_t timing_arc_idx)
  {
    for (std::size_t pos = 0; pos < _timing_arc_delay_map.size(); ++pos)
      if (_timing_arc_delay_map[pos].first == timing_arc_idx)
        return {_timing_arc_delay_map[pos].second, pos};
    _timing_arc_delay_map.emplace_back(timing_arc_idx, ArcIoGrid<double>{});
    _timing_arc_delay_fallback_masks.push_back(0);
    _timing_arc_delay_zero_slew_masks.push_back(0);
    return {_timing_arc_delay_map.back().second, _timing_arc_delay_map.size() - 1};
  }

  void update_timing_arc_delay(int32_t timing_arc_idx, AnalysisType analysis_type, TransType input_trans_type, TransType output_trans_type, double delay,
                               bool is_initialization, bool is_zero_slew_probe = false)
  {
    // Grid semantics: out-of-range keys are absent and unwritable, matching
    // set()'s no-op on the same keys.
    if (!arcIoSlotValid(analysis_type, input_trans_type, output_trans_type))
      return;
    auto [grid, pos] = timingArcDelaySlot(timing_arc_idx);
    const uint64_t bit = uint64_t{1} << arcIoSlot(analysis_type, input_trans_type, output_trans_type);
    const bool has_delay = grid.get(analysis_type, input_trans_type, output_trans_type).has_value();

    if (!has_delay) {
      grid.set(analysis_type, input_trans_type, output_trans_type, delay);
      if (is_initialization)
        _timing_arc_delay_fallback_masks[pos] |= bit;
      // A constant cone is resolved in a follow-up sweep, after the tied driver
      // has picked up its own slew. The main sweep evaluates the arc with
      // input_slew == 0, an optimistic probe that the min rule would then keep
      // forever. Record it so the first real-slew evaluation wins. The flag has
      // to be set on the first write only; afterwards has_delay is true and this
      // branch is never reached again.
      if (is_zero_slew_probe)
        _timing_arc_delay_zero_slew_masks[pos] |= bit;
      return;
    }

    const bool is_fallback = _timing_arc_delay_fallback_masks[pos] & bit;
    // Replace a zero-slew probe as soon as a real input transition is known,
    // for both min and max, bypassing the monotonic rule either way.
    if (!is_initialization && !is_zero_slew_probe && (_timing_arc_delay_zero_slew_masks[pos] & bit)) {
      grid.set(analysis_type, input_trans_type, output_trans_type, delay);
      _timing_arc_delay_zero_slew_masks[pos] &= ~bit;
      _timing_arc_delay_fallback_masks[pos] &= ~bit;
      return;
    }
    if (!is_initialization && is_fallback) {
      grid.set(analysis_type, input_trans_type, output_trans_type, delay);
      _timing_arc_delay_fallback_masks[pos] &= ~bit;
      return;
    }
    if (is_initialization && !is_fallback) {
      return;
    }

    const auto cached_delay = grid.get(analysis_type, input_trans_type, output_trans_type);
    if ((analysis_type == AnalysisType::kMin && delay < *cached_delay) || (analysis_type == AnalysisType::kMax && delay > *cached_delay)) {
      grid.set(analysis_type, input_trans_type, output_trans_type, delay);
    }
  }

 private:
  int32_t _source_vertex_idx = -1;
  int32_t _sink_vertex_idx = -1;
  std::string _arc_name;
  std::string _source_pin;
  std::string _sink_pin;
  std::string _owner_name;
  std::string _library_source_port;
  std::string _library_sink_port;
  ArcType _type = ArcType::kNone;
  double _delay = 1.0;
  double _delay_max = 1.0;
  double _delay_min = 1.0;
  ClockWaveGrid<double> _trans_delay_map;
  ArcIoGrid<double> _input_output_delay_map;
  ArcIoGrid<double> _graph_delay_map;
  std::vector<std::pair<int32_t, ArcIoGrid<double>>> _timing_arc_delay_map;
  std::vector<uint64_t> _timing_arc_delay_fallback_masks;
  std::vector<uint64_t> _timing_arc_delay_zero_slew_masks;
  uint8_t _rc_failures = 0;
  std::optional<LibertyArcId> _liberty_arc_id;
  bool _is_clock_arc = false;
  bool _is_disable_arc = false;
  bool _is_case_analysis_disable = false;
  bool _is_loop_disable = false;
};

}  // namespace ista
