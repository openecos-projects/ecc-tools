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

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "common/AnalysisType.hpp"
#include "common/PathSourceType.hpp"
#include "common/TransType.hpp"
#include "graph/ArcType.hpp"
#include "liberty/TimingCheckType.hpp"

namespace ista {

class TimingPathPoint
{
 public:
  TimingPathPoint() = default;
  ~TimingPathPoint() = default;
  // getter
  std::string& get_pin_name() { return _pin_name; }
  std::string& get_instance_name() { return _instance_name; }
  std::string& get_cell_name() { return _cell_name; }
  std::string& get_net_name() { return _net_name; }
  std::string& get_arc_name() { return _arc_name; }
  std::string& get_source_pin() { return _source_pin; }
  std::string& get_sink_pin() { return _sink_pin; }
  ArcType get_arc_type() const { return _arc_type; }
  double get_arc_delay() const { return _arc_delay; }
  double get_arrival() const { return _arrival; }
  double get_required() const { return _required; }
  double get_slack() const { return _slack; }
  TransType get_trans_type() const { return _trans_type; }
  bool rcFailed() const { return _rc_failed; }
  bool rcNonbounding() const { return _rc_nonbounding; }
  void setRcFailed(bool value) { _rc_failed = value; }
  void setRcNonbounding(bool value) { _rc_nonbounding = value; }
  // setter
  void set_pin_name(const std::string& pin_name) { _pin_name = pin_name; }
  void set_instance_name(const std::string& instance_name) { _instance_name = instance_name; }
  void set_cell_name(const std::string& cell_name) { _cell_name = cell_name; }
  void set_net_name(const std::string& net_name) { _net_name = net_name; }
  void set_arc_name(const std::string& arc_name) { _arc_name = arc_name; }
  void set_source_pin(const std::string& source_pin) { _source_pin = source_pin; }
  void set_sink_pin(const std::string& sink_pin) { _sink_pin = sink_pin; }
  void set_arc_type(const ArcType& arc_type) { _arc_type = arc_type; }
  void set_arc_delay(const double arc_delay) { _arc_delay = arc_delay; }
  void set_arrival(const double arrival) { _arrival = arrival; }
  void set_required(const double required) { _required = required; }
  void set_slack(const double slack) { _slack = slack; }
  void set_trans_type(const TransType& trans_type) { _trans_type = trans_type; }
  // function

 private:
  std::string _pin_name;
  std::string _instance_name;
  std::string _cell_name;
  std::string _net_name;
  std::string _arc_name;
  std::string _source_pin;
  std::string _sink_pin;
  ArcType _arc_type = ArcType::kNone;
  double _arc_delay = 0.0;
  double _arrival = 0.0;
  double _required = 0.0;
  double _slack = 0.0;
  TransType _trans_type = TransType::kNone;
  bool _rc_failed = false;
  bool _rc_nonbounding = false;
};

class TimingPath
{
 public:
  TimingPath() = default;
  ~TimingPath() = default;
  // getter
  double get_clock_uncertainty() const { return _clock_uncertainty; }
  std::optional<double> get_input_delay() const { return _input_delay; }
  std::optional<double> get_output_delay() const { return _output_delay; }
  TransType get_launch_clock_pin_transition() const { return _launch_clock_pin_transition; }
  TransType get_capture_clock_pin_transition() const { return _capture_clock_pin_transition; }
  void set_launch_clock_pin_transition(TransType value) { _launch_clock_pin_transition = value; }
  void set_capture_clock_pin_transition(TransType value) { _capture_clock_pin_transition = value; }
  TransType get_launch_clock_transition() const { return _launch_clock_transition; }
  std::string& get_start_point() { return _start_point; }
  std::string& get_end_point() { return _end_point; }
  double get_path_delay() const { return _path_delay; }
  double get_required_time() const { return _required_time; }
  double get_required_time_adjustment() const { return _required_time_adjustment; }
  double get_slack() const { return _slack; }
  double get_cell_delay() const { return _cell_delay; }
  double get_net_delay() const { return _net_delay; }
  double get_launch_time() const { return _launch_time; }
  double get_capture_time() const { return _capture_time; }
  double get_launch_clock_source_latency() const { return _launch_clock_source_latency; }
  double get_capture_clock_source_latency() const { return _capture_clock_source_latency; }
  double get_launch_clock_network_delay() const { return _launch_clock_network_delay; }
  double get_capture_clock_network_delay() const { return _capture_clock_network_delay; }
  double get_clock_reconvergence_pessimism() const { return _clock_reconvergence_pessimism; }
  double get_setup_time() const { return _setup_time; }
  double get_check_time() const { return _check_time; }
  int32_t get_level() const { return _level; }
  AnalysisType get_analysis_type() const { return _analysis_type; }
  PathSourceType get_source_type() const { return _source_type; }
  TimingCheckType get_check_type() const { return _check_type; }
  TransType get_trans_type() const { return _trans_type; }
  std::string& get_clock_name() { return _clock_name; }
  std::string& get_capture_clock_name() { return _capture_clock_name; }
  TransType get_capture_clock_transition() const { return _capture_clock_transition; }
  const std::string& get_launch_clock_pin() const { return _launch_clock_pin; }
  std::string& get_capture_clock_pin() { return _capture_clock_pin; }
  std::string& get_last_common_pin() { return _last_common_pin; }
  std::vector<TimingPathPoint>& get_point_list() { return _point_list; }
  // setter
  void set_clock_uncertainty(double value) { _clock_uncertainty = value; }
  void set_input_delay(std::optional<double> value) { _input_delay = value; }
  void set_output_delay(std::optional<double> value) { _output_delay = value; }
  void set_launch_clock_transition(TransType value) { _launch_clock_transition = value; }
  void set_start_point(const std::string& start_point) { _start_point = start_point; }
  void set_end_point(const std::string& end_point) { _end_point = end_point; }
  void set_path_delay(const double path_delay) { _path_delay = path_delay; }
  void set_required_time(const double required_time) { _required_time = required_time; }
  void set_required_time_adjustment(double adjustment) { _required_time_adjustment = adjustment; }
  void set_slack(const double slack) { _slack = slack; }
  void set_cell_delay(const double cell_delay) { _cell_delay = cell_delay; }
  void set_net_delay(const double net_delay) { _net_delay = net_delay; }
  void set_launch_time(const double launch_time) { _launch_time = launch_time; }
  void set_capture_time(const double capture_time) { _capture_time = capture_time; }
  void set_launch_clock_source_latency(double latency) { _launch_clock_source_latency = latency; }
  void set_capture_clock_source_latency(double latency) { _capture_clock_source_latency = latency; }
  void set_launch_clock_network_delay(const double launch_clock_network_delay) { _launch_clock_network_delay = launch_clock_network_delay; }
  void set_capture_clock_network_delay(const double capture_clock_network_delay) { _capture_clock_network_delay = capture_clock_network_delay; }
  void set_clock_reconvergence_pessimism(const double clock_reconvergence_pessimism) { _clock_reconvergence_pessimism = clock_reconvergence_pessimism; }
  void set_setup_time(const double setup_time) { _setup_time = setup_time; }
  void set_check_time(const double check_time) { _check_time = check_time; }
  void set_level(const int32_t level) { _level = level; }
  void set_analysis_type(const AnalysisType& analysis_type) { _analysis_type = analysis_type; }
  void set_source_type(const PathSourceType& source_type) { _source_type = source_type; }
  void set_check_type(const TimingCheckType& check_type) { _check_type = check_type; }
  void set_trans_type(const TransType& trans_type) { _trans_type = trans_type; }
  void set_clock_name(const std::string& clock_name) { _clock_name = clock_name; }
  void set_capture_clock_name(const std::string_view name) { _capture_clock_name = name; }
  void set_capture_clock_transition(TransType transition) { _capture_clock_transition = transition; }
  void set_launch_clock_pin(const std::string& pin) { _launch_clock_pin = pin; }
  void set_capture_clock_pin(const std::string& capture_clock_pin) { _capture_clock_pin = capture_clock_pin; }
  void set_last_common_pin(const std::string& last_common_pin) { _last_common_pin = last_common_pin; }
  void set_point_list(const std::vector<TimingPathPoint>& point_list) { _point_list = point_list; }
  // function

 private:
  double _clock_uncertainty = 0.0;
  std::optional<double> _input_delay;
  std::optional<double> _output_delay;
  TransType _launch_clock_pin_transition = TransType::kRise;
  TransType _capture_clock_pin_transition = TransType::kRise;
  TransType _launch_clock_transition = TransType::kNone;
  std::string _start_point;
  std::string _end_point;
  double _path_delay = 0.0;
  double _required_time = 0.0;
  double _required_time_adjustment = 0.0;
  double _slack = 0.0;
  double _cell_delay = 0.0;
  double _net_delay = 0.0;
  double _launch_time = 0.0;
  double _capture_time = 0.0;
  double _launch_clock_source_latency = 0.0;
  double _capture_clock_source_latency = 0.0;
  double _launch_clock_network_delay = 0.0;
  double _capture_clock_network_delay = 0.0;
  double _clock_reconvergence_pessimism = 0.0;
  double _setup_time = 0.0;
  double _check_time = 0.0;
  int32_t _level = 0;
  AnalysisType _analysis_type = AnalysisType::kNone;
  PathSourceType _source_type = PathSourceType::kNone;
  TimingCheckType _check_type = TimingCheckType::kNone;
  TransType _trans_type = TransType::kNone;
  std::string _clock_name;
  std::string _capture_clock_name;
  TransType _capture_clock_transition = TransType::kRise;
  std::string _launch_clock_pin;
  std::string _capture_clock_pin;
  std::string _last_common_pin;
  std::vector<TimingPathPoint> _point_list;
};

}  // namespace ista
