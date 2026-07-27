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

#include "STAHeader.hpp"
#include "TimingClock.hpp"
#include "TimingPortConstraint.hpp"

namespace ista {

class TimingConstraint
{
 public:
  TimingConstraint() = default;
  ~TimingConstraint() = default;
  // getter
  std::string& get_sdc_file_path() { return _sdc_file_path; }
  std::map<std::string, TimingClock>& get_clock_map() { return _clock_map; }
  std::map<std::string, TimingPortConstraint>& get_port_constraint_map() { return _port_constraint_map; }
  std::map<std::string, bool>& get_case_analysis_map() { return _case_analysis_map; }
  // setter
  void set_sdc_file_path(const std::string& sdc_file_path) { _sdc_file_path = sdc_file_path; }
  void set_clock_map(const std::map<std::string, TimingClock>& clock_map) { _clock_map = clock_map; }
  void set_port_constraint_map(const std::map<std::string, TimingPortConstraint>& port_constraint_map) { _port_constraint_map = port_constraint_map; }
  void set_case_analysis_map(const std::map<std::string, bool>& case_analysis_map) { _case_analysis_map = case_analysis_map; }
  // function

 private:
  std::string _sdc_file_path;
  std::map<std::string, TimingClock> _clock_map;
  std::map<std::string, TimingPortConstraint> _port_constraint_map;
  std::map<std::string, bool> _case_analysis_map;
};

}  // namespace ista
