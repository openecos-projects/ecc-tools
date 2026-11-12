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

#include "PinDirection.hpp"
#include "TimingCaseValue.hpp"
#include "PWHeader.hpp"

namespace ipw {

class Pin
{
 public:
  Pin() = default;
  ~Pin() = default;
  // getter
  TimingCaseValue get_constant_value() const { return _constant_value; }
  bool get_is_driver_disabled() const { return _is_driver_disabled; }
  int32_t get_load_vertex_idx() const { return _load_vertex_idx; }
  int32_t get_driver_vertex_idx() const { return _driver_vertex_idx; }
  std::string& get_pin_name() { return _pin_name; }
  // 稠密编号（GraphBuilder 里按 pin_map 迭代序赋值），供按 id 索引的缓存使用。
  uint64_t get_index() const { return _index; }
  std::string& get_instance_name() { return _instance_name; }
  std::string& get_net_name() { return _net_name; }
  PinDirection get_direction() const { return _direction; }
  bool get_is_port() const { return _is_port; }
  // setter
  void set_constant_value(TimingCaseValue value) { _constant_value = value; }
  void set_is_driver_disabled(bool value) { _is_driver_disabled = value; }
  void set_load_vertex_idx(int32_t idx) { _load_vertex_idx = idx; }
  void set_driver_vertex_idx(int32_t idx) { _driver_vertex_idx = idx; }
  void set_pin_name(const std::string& pin_name) { _pin_name = pin_name; }
  void set_index(const uint64_t index) { _index = index; }
  void set_instance_name(const std::string& instance_name) { _instance_name = instance_name; }
  void set_net_name(const std::string& net_name) { _net_name = net_name; }
  void set_direction(const PinDirection& direction) { _direction = direction; }
  void set_is_port(const bool is_port) { _is_port = is_port; }
  // function

 private:
  TimingCaseValue _constant_value = TimingCaseValue::kNone;
  bool _is_driver_disabled = false;
  int32_t _load_vertex_idx = -1;
  int32_t _driver_vertex_idx = -1;
  uint64_t _index = 0;
  std::string _pin_name;
  std::string _instance_name;
  std::string _net_name;
  PinDirection _direction = PinDirection::kNone;
  bool _is_port = false;
};

}  // namespace ipw
