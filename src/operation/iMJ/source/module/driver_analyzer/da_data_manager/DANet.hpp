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

#include "DAPin.hpp"

namespace imj {

class DANet
{
 public:
  DANet() = default;
  ~DANet() = default;
  // getter
  std::string& get_name() { return _name; }
  std::vector<DAPin>& get_pin_list() { return _pin_list; }
  std::vector<int32_t>& get_node_idx_list() { return _node_idx_list; }
  int32_t get_driver_num() { return _driver_num; }
  int32_t get_unresolved_num() { return _unresolved_num; }
  // const getter
  const std::string& get_name() const { return _name; }
  const std::vector<DAPin>& get_pin_list() const { return _pin_list; }
  const std::vector<int32_t>& get_node_idx_list() const { return _node_idx_list; }
  int32_t get_driver_num() const { return _driver_num; }
  int32_t get_unresolved_num() const { return _unresolved_num; }
  // setter
  void set_name(const std::string& name) { _name = name; }
  void set_pin_list(const std::vector<DAPin>& pin_list) { _pin_list = pin_list; }
  void set_node_idx_list(const std::vector<int32_t>& node_idx_list) { _node_idx_list = node_idx_list; }
  void set_driver_num(int32_t driver_num) { _driver_num = driver_num; }
  void set_unresolved_num(int32_t unresolved_num) { _unresolved_num = unresolved_num; }
  // function

 private:
  std::string _name;
  std::vector<DAPin> _pin_list;
  std::vector<int32_t> _node_idx_list;
  int32_t _driver_num = 0;
  int32_t _unresolved_num = 0;
};

}  // namespace imj
