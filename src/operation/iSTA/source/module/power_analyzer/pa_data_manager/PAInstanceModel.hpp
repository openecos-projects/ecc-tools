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

#include "PowerActivity.hpp"
#include "STAHeader.hpp"

namespace ista {

class LogicExpression;
class TimingPowerArc;

class PAInstanceModel
{
 public:
  using OutputTimingPowerArcGroup = std::pair<std::string, std::string>;
  using SensitivityProbabilityKey = std::pair<LogicExpression*, std::string>;

  PAInstanceModel() = default;
  ~PAInstanceModel() = default;
  // getter
  std::map<std::string, PowerActivity>& get_port_activity_map() { return _port_activity_map; }
  std::map<TimingPowerArc*, double>& get_output_timing_power_arc_weight_map() { return _output_timing_power_arc_weight_map; }
  std::map<OutputTimingPowerArcGroup, double>& get_output_timing_power_arc_weight_sum_map()
  {
    return _output_timing_power_arc_weight_sum_map;
  }
  std::map<LogicExpression*, PowerActivity>& get_logic_expression_activity_map() { return _logic_expression_activity_map; }
  std::map<SensitivityProbabilityKey, double>& get_sensitivity_probability_map() { return _sensitivity_probability_map; }
  bool get_is_port_activity_map_built() const { return _is_port_activity_map_built; }
  // setter
  void set_is_port_activity_map_built(const bool is_port_activity_map_built)
  {
    _is_port_activity_map_built = is_port_activity_map_built;
  }
  // function

 private:
  std::map<std::string, PowerActivity> _port_activity_map;
  std::map<TimingPowerArc*, double> _output_timing_power_arc_weight_map;
  std::map<OutputTimingPowerArcGroup, double> _output_timing_power_arc_weight_sum_map;
  std::map<LogicExpression*, PowerActivity> _logic_expression_activity_map;
  std::map<SensitivityProbabilityKey, double> _sensitivity_probability_map;
  bool _is_port_activity_map_built = false;
};

}  // namespace ista
