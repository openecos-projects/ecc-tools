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

#include "ACAntennaRule.hpp"
#include "ACViolation.hpp"
#include "ZHHeader.hpp"

namespace izh {

class ACModel
{
 public:
  ACModel() = default;
  ACModel(const ACModel& other) = delete;
  ACModel(ACModel&& other) noexcept
      : _violation_num(other._violation_num),
        _micron_dbu(other._micron_dbu),
        _report_dir(std::move(other._report_dir)),
        _rule_list(std::move(other._rule_list)),
        _rule_map(std::move(other._rule_map)),
        _thickness_by_order(std::move(other._thickness_by_order)),
        _conductive_order(std::move(other._conductive_order)),
        _violation_list(std::move(other._violation_list)),
        _max_layer_order(other._max_layer_order),
        _signal_net_cnt(other._signal_net_cnt),
        _pins_missing_antenna_info(other._pins_missing_antenna_info.load(std::memory_order_relaxed)),
        _pins_with_gate_area(other._pins_with_gate_area.load(std::memory_order_relaxed)),
        _comps_without_gate(other._comps_without_gate.load(std::memory_order_relaxed)),
        _skipped_segments(other._skipped_segments.load(std::memory_order_relaxed)),
        _conductors_out_of_range(other._conductors_out_of_range.load(std::memory_order_relaxed)),
        _partial_areas_dropped(other._partial_areas_dropped.load(std::memory_order_relaxed))
  {
  }
  ~ACModel() = default;
  // getter
  int32_t get_violation_num() const { return _violation_num; }
  int& get_micron_dbu() { return _micron_dbu; }
  int get_micron_dbu() const { return _micron_dbu; }
  std::string& get_report_dir() { return _report_dir; }
  const std::string& get_report_dir() const { return _report_dir; }
  std::vector<ACAntennaRule>& get_rule_list() { return _rule_list; }
  const std::vector<ACAntennaRule>& get_rule_list() const { return _rule_list; }
  std::vector<std::vector<const ACAntennaRule*>>& get_rule_map() { return _rule_map; }
  const std::vector<std::vector<const ACAntennaRule*>>& get_rule_map() const { return _rule_map; }
  std::vector<double>& get_thickness_by_order() { return _thickness_by_order; }
  const std::vector<double>& get_thickness_by_order() const { return _thickness_by_order; }
  std::vector<bool>& get_conductive_order() { return _conductive_order; }
  const std::vector<bool>& get_conductive_order() const { return _conductive_order; }
  std::vector<ACViolation>& get_violation_list() { return _violation_list; }
  const std::vector<ACViolation>& get_violation_list() const { return _violation_list; }
  int& get_max_layer_order() { return _max_layer_order; }
  int get_max_layer_order() const { return _max_layer_order; }
  int64_t& get_signal_net_cnt() { return _signal_net_cnt; }
  int64_t get_signal_net_cnt() const { return _signal_net_cnt; }
  std::atomic<int64_t>& get_pins_missing_antenna_info() { return _pins_missing_antenna_info; }
  std::atomic<int64_t>& get_pins_with_gate_area() { return _pins_with_gate_area; }
  std::atomic<int64_t>& get_comps_without_gate() { return _comps_without_gate; }
  std::atomic<int64_t>& get_skipped_segments() { return _skipped_segments; }
  std::atomic<int64_t>& get_conductors_out_of_range() { return _conductors_out_of_range; }
  std::atomic<int64_t>& get_partial_areas_dropped() { return _partial_areas_dropped; }
  // setter
  void set_violation_num(const int32_t violation_num) { _violation_num = violation_num; }
  // function
  void addViolationNum(const int32_t violation_num) { _violation_num += violation_num; }

 private:
  int32_t _violation_num = 0;
  int _micron_dbu = 1000;
  std::string _report_dir;
  std::vector<ACAntennaRule> _rule_list;
  std::vector<std::vector<const ACAntennaRule*>> _rule_map;
  std::vector<double> _thickness_by_order;
  std::vector<bool> _conductive_order;
  std::vector<ACViolation> _violation_list;
  int _max_layer_order = 200;
  int64_t _signal_net_cnt = 0;
  std::atomic<int64_t> _pins_missing_antenna_info{0};
  std::atomic<int64_t> _pins_with_gate_area{0};
  std::atomic<int64_t> _comps_without_gate{0};
  std::atomic<int64_t> _skipped_segments{0};
  std::atomic<int64_t> _conductors_out_of_range{0};
  std::atomic<int64_t> _partial_areas_dropped{0};

  ACModel& operator=(const ACModel& other) = delete;
  ACModel& operator=(ACModel&& other) = delete;
};

}  // namespace izh
