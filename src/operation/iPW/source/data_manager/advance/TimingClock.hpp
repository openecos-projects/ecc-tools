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

#include "AnalysisType.hpp"
#include "PWHeader.hpp"
#include "TransType.hpp"

namespace ipw {

class TimingClock
{
 public:
  TimingClock() = default;
  ~TimingClock() = default;
  // getter
  std::vector<std::string>& get_source_list() { return _source_list; }
  const std::vector<std::string>& get_source_list() const { return _source_list; }
  double get_period() const { return _period; }
  double get_rise_edge() const { return _rise_edge; }
  double get_fall_edge() const { return _fall_edge; }
  bool get_is_propagated() const { return _is_propagated; }
  const std::string& get_master_clock_name() const { return _master_clock_name; }
  const std::string& get_master_source() const { return _master_source; }
  bool get_is_generated() const { return !_master_clock_name.empty(); }
  bool get_is_generated_combinational() const { return _is_generated_combinational; }
  std::map<AnalysisType, std::map<TransType, double>>& get_transition_map() { return _transition_map; }
  // setter
  void set_source_list(const std::vector<std::string>& source_list) { _source_list = source_list; }
  void set_period(const double period) { _period = period; }
  void set_rise_edge(const double rise_edge) { _rise_edge = rise_edge; }
  void set_fall_edge(const double fall_edge) { _fall_edge = fall_edge; }
  void set_is_propagated(const bool is_propagated) { _is_propagated = is_propagated; }
  void set_master_clock_name(const std::string& name) { _master_clock_name = name; }
  void set_master_source(const std::string& source) { _master_source = source; }
  void set_is_generated_combinational(const bool is_generated_combinational) { _is_generated_combinational = is_generated_combinational; }
  // function

 private:
  std::string _master_clock_name;
  std::string _master_source;
  std::vector<std::string> _source_list;
  double _period = 0.0;
  double _rise_edge = 0.0;
  double _fall_edge = 0.0;
  bool _is_propagated = false;
  bool _is_generated_combinational = false;
  std::map<AnalysisType, std::map<TransType, double>> _transition_map;
};

}  // namespace ipw
