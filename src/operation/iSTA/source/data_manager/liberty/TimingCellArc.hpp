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

#include <string>
#include <vector>

#include "liberty/TimingArc.hpp"

namespace ista {

class TimingCellArc
{
 public:
  TimingCellArc() = default;
  ~TimingCellArc() = default;
  // getter
  std::string& get_source_port() { return _source_port; }
  std::string& get_sink_port() { return _sink_port; }
  double get_delay() const { return _delay; }
  double get_delay_max() const { return _delay_max; }
  double get_delay_min() const { return _delay_min; }
  std::vector<TimingArc>& get_timing_arc_list() { return _timing_arc_list; }
  const std::vector<TimingArc>& get_timing_arc_list() const { return _timing_arc_list; }
  bool get_is_timing_graph_arc() const { return _is_timing_graph_arc; }
  bool get_is_clock_arc() const { return _is_clock_arc; }
  bool get_is_control_arc() const { return _is_control_arc; }
  bool get_is_disable_arc() const { return _is_disable_arc; }
  bool get_is_clear_preset_arc() const { return _is_clear_preset_arc; }
  bool isThreeStateArc() const
  {
    for (const auto& arc : _timing_arc_list)
      if (arc.isThreeStateArc())
        return true;
    return false;
  }
  bool get_is_latch_data_arc() const { return _is_latch_data_arc; }
  // setter
  void set_source_port(const std::string& source_port) { _source_port = source_port; }
  void set_sink_port(const std::string& sink_port) { _sink_port = sink_port; }
  void set_delay(const double delay) { _delay = delay; }
  void set_delay_max(const double delay_max) { _delay_max = delay_max; }
  void set_delay_min(const double delay_min) { _delay_min = delay_min; }
  void set_timing_arc_list(const std::vector<TimingArc>& timing_arc_list) { _timing_arc_list = timing_arc_list; }
  void set_is_timing_graph_arc(const bool is_timing_graph_arc) { _is_timing_graph_arc = is_timing_graph_arc; }
  void set_is_clock_arc(const bool is_clock_arc) { _is_clock_arc = is_clock_arc; }
  void set_is_control_arc(const bool is_control_arc) { _is_control_arc = is_control_arc; }
  void set_is_disable_arc(const bool is_disable_arc) { _is_disable_arc = is_disable_arc; }
  void set_is_clear_preset_arc(const bool is_clear_preset_arc) { _is_clear_preset_arc = is_clear_preset_arc; }
  void set_is_latch_data_arc(const bool is_latch_data_arc) { _is_latch_data_arc = is_latch_data_arc; }
  // function

 private:
  std::string _source_port;
  std::string _sink_port;
  double _delay = 0.0;
  double _delay_max = 0.0;
  double _delay_min = 0.0;
  std::vector<TimingArc> _timing_arc_list;
  bool _is_timing_graph_arc = true;
  bool _is_clock_arc = false;
  // Three-state enable/disable arcs are data-path control arcs. They must
  // participate in propagation, but their source is not a Boolean input of
  // the driven output function (for example OE -> PAD).
  bool _is_control_arc = false;
  bool _is_disable_arc = false;
  bool _is_clear_preset_arc = false;
  bool _is_latch_data_arc = false;
};

}  // namespace ista
