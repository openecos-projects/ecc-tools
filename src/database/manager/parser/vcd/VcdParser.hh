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

#include <map>
#include <string>

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

class VcdReader
{
 public:
  VcdReader() = default;
  ~VcdReader() = default;
  // getter
  std::map<std::string, VcdSignalActivity>& get_signal_activity_map() { return _signal_activity_map; }
  // setter
  // function
  bool read(const std::string& file_path);

 private:
  std::map<std::string, VcdSignalActivity> _signal_activity_map;
};

}  // namespace vcd
