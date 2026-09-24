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

#include "PWHeader.hpp"
#include "TimingCaseValue.hpp"
#include "TimingClock.hpp"
#include "TimingPortConstraint.hpp"
#include "TransType.hpp"

namespace ipw {

class TimingConstraint
{
 public:
  TimingConstraint() = default;
  ~TimingConstraint() = default;
  // getter
  std::map<std::string, TimingClock>& get_clock_map() { return _clock_map; }
  std::map<std::string, TimingPortConstraint>& get_port_constraint_map() { return _port_constraint_map; }
  std::map<std::string, TimingCaseValue>& get_case_analysis_map() { return _case_analysis_map; }
  std::map<std::string, TimingCaseValue>& get_effective_case_analysis_map() { return _effective_case_analysis_map; }
  // setter
  // function
  void clear_net_load() { _net_load_map.clear(); }
  void set_net_load(std::string_view net_name, AnalysisType analysis_type, TransType trans_type, double load)
  {
    _net_load_map[std::string(net_name)][analysis_type][trans_type] = load;
  }
  bool has_net_load(std::string_view net_name, AnalysisType analysis_type, TransType trans_type) const
  {
    const auto net_load = _net_load_map.find(std::string(net_name));
    if (net_load == _net_load_map.end()) {
      return false;
    }
    const auto analysis_load = net_load->second.find(analysis_type);
    return analysis_load != net_load->second.end() && analysis_load->second.contains(trans_type);
  }
  double get_net_load(std::string_view net_name, AnalysisType analysis_type, TransType trans_type) const
  {
    const auto net_load = _net_load_map.find(std::string(net_name));
    if (net_load == _net_load_map.end()) {
      return 0.0;
    }
    const auto analysis_load = net_load->second.find(analysis_type);
    if (analysis_load == net_load->second.end()) {
      return 0.0;
    }
    const auto transition_load = analysis_load->second.find(trans_type);
    return transition_load == analysis_load->second.end() ? 0.0 : transition_load->second;
  }
  bool allowsTransition(std::string_view pin_name, TransType trans_type) const
  {
    const auto value = _effective_case_analysis_map.find(std::string(pin_name));
    if (value == _effective_case_analysis_map.end()) {
      return true;
    }
    switch (value->second) {
      case TimingCaseValue::kNone:
        return true;
      case TimingCaseValue::kRise:
        return trans_type == TransType::kRise;
      case TimingCaseValue::kFall:
        return trans_type == TransType::kFall;
      case TimingCaseValue::kZero:
      case TimingCaseValue::kOne:
      case TimingCaseValue::kStatic:
        return false;
    }
    return true;
  }

 private:
  std::map<std::string, TimingClock> _clock_map;
  std::map<std::string, TimingPortConstraint> _port_constraint_map;
  std::map<std::string, TimingCaseValue> _case_analysis_map;
  std::map<std::string, TimingCaseValue> _effective_case_analysis_map;
  std::map<std::string, std::map<AnalysisType, std::map<TransType, double>>> _net_load_map;
};

}  // namespace ipw
