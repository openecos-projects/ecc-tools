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
#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "VcdTransitionModel.hh"

namespace vcd {

class VcdSignalActivity
{
 public:
  VcdSignalActivity() = default;
  ~VcdSignalActivity() = default;
  // getter
  double get_transition_density() const { return _transition_density; }
  double get_static_probability() const { return _static_probability; }
  // setter
  void set_transition_density(double transition_density) { _transition_density = transition_density; }
  void set_static_probability(double static_probability) { _static_probability = static_probability; }
  // function

 private:
  double _transition_density = 0.0;
  double _static_probability = 0.0;
};

class VcdWindowSignalActivity
{
 public:
  VcdWindowSignalActivity() = default;
  ~VcdWindowSignalActivity() = default;

  std::size_t get_window_index() const { return _window_index; }
  double get_window_begin_time() const { return _window_begin_time; }
  double get_window_end_time() const { return _window_end_time; }
  double get_window_duration() const { return _window_end_time - _window_begin_time; }

  const std::vector<std::string>& get_signal_name_list() const { return _signal_name_list; }
  double get_rise_transition_density(std::size_t signal_index) const { return _rise_transition_density_list[signal_index]; }
  double get_fall_transition_density(std::size_t signal_index) const { return _fall_transition_density_list[signal_index]; }
  double get_transition_density(std::size_t signal_index) const
  {
    return _rise_transition_density_list[signal_index] + _fall_transition_density_list[signal_index];
  }
  double get_static_probability(std::size_t signal_index) const { return _static_probability_list[signal_index]; }
  void set_window(std::size_t window_index, double window_begin_time, double window_end_time)
  {
    _window_index = window_index;
    _window_begin_time = window_begin_time;
    _window_end_time = window_end_time;
  }
  // 信号表在 open() 时定一次，活动值同时清零。
  void set_signal_name_list(const std::vector<std::string>& signal_name_list)
  {
    _signal_name_list = signal_name_list;
    _rise_transition_density_list.assign(signal_name_list.size(), 0.0);
    _fall_transition_density_list.assign(signal_name_list.size(), 0.0);
    _static_probability_list.assign(signal_name_list.size(), 0.0);
  }
  void set_signal_activity(std::size_t signal_index, double rise_transition_density, double fall_transition_density,
                           double static_probability)
  {
    _rise_transition_density_list[signal_index] = rise_transition_density;
    _fall_transition_density_list[signal_index] = fall_transition_density;
    _static_probability_list[signal_index] = static_probability;
  }

 private:
  std::size_t _window_index = 0;
  double _window_begin_time = 0.0;
  double _window_end_time = 0.0;
  std::vector<std::string> _signal_name_list;
  std::vector<double> _rise_transition_density_list;
  std::vector<double> _fall_transition_density_list;
  std::vector<double> _static_probability_list;
};

class VcdReader
{
 public:
  VcdReader() = default;
  ~VcdReader() = default;
  // getter
  std::map<std::string, VcdSignalActivity>& get_signal_activity_map() { return _signal_activity_map; }
  // setter
  // function
  // options 默认 kAveraged，保持既有调用点行为不变。
  bool read(const std::string& file_path, const ActivityModelOptions& options = {});

 private:
  std::map<std::string, VcdSignalActivity> _signal_activity_map;
};

// 按时间窗读 VCD：open() 后反复 advance_window() 直到返回 false，每窗结果从
// get_window_activity() 取（reader 持有，下一次 advance_window() 覆盖）。
// window_length 单位为 ns，窗口是从文件第一个时间戳起算的
// [k * window_length, (k + 1) * window_length)，正好落在边界上的事件归右窗，
// 与 PrimeTime PX time_based 波形的 cycle 定义对齐（采样点 t 覆盖 [t, t+cycle)）。
// 活动口径由 open() 的 ActivityModelOptions 决定（峰值流程用 kTimeBased）。
class VcdWindowReader
{
 public:
  VcdWindowReader();
  ~VcdWindowReader();
  VcdWindowReader(const VcdWindowReader& other) = delete;
  VcdWindowReader(VcdWindowReader&& other) = delete;
  VcdWindowReader& operator=(const VcdWindowReader& other) = delete;
  VcdWindowReader& operator=(VcdWindowReader&& other) = delete;
  // function
  // options 默认 kAveraged；峰值（time_based）流程显式传 ActivityModel::kTimeBased。
  bool open(const std::string& file_path, double window_length, const ActivityModelOptions& options = {});
  bool advance_window();
  const VcdWindowSignalActivity& get_window_activity() const { return _window_activity; }

 private:
  class Impl;
  std::unique_ptr<Impl> _impl;
  VcdWindowSignalActivity _window_activity;
};

}  // namespace vcd
