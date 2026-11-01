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

#include "DANet.hpp"
#include "DANode.hpp"

namespace imj {

class DAModel
{
 public:
  DAModel() = default;
  ~DAModel() = default;
  // getter
  std::string& get_design_name() { return _design_name; }
  std::vector<DANet>& get_net_list() { return _net_list; }
  std::vector<DANode>& get_node_list() { return _node_list; }
  int32_t get_inferred_pin_num() { return _inferred_pin_num; }
  int32_t get_multi_driver_net_num() { return _multi_driver_net_num; }
  int32_t get_unresolved_net_num() { return _unresolved_net_num; }
  int32_t get_undriven_net_num() { return _undriven_net_num; }
  int32_t get_excluded_pg_net_num() { return _excluded_pg_net_num; }
  int32_t get_excluded_io_boundary_net_num() { return _excluded_io_boundary_net_num; }
  // const getter
  const std::string& get_design_name() const { return _design_name; }
  const std::vector<DANet>& get_net_list() const { return _net_list; }
  const std::vector<DANode>& get_node_list() const { return _node_list; }
  int32_t get_inferred_pin_num() const { return _inferred_pin_num; }
  int32_t get_multi_driver_net_num() const { return _multi_driver_net_num; }
  int32_t get_unresolved_net_num() const { return _unresolved_net_num; }
  int32_t get_undriven_net_num() const { return _undriven_net_num; }
  int32_t get_excluded_pg_net_num() const { return _excluded_pg_net_num; }
  int32_t get_excluded_io_boundary_net_num() const { return _excluded_io_boundary_net_num; }
  // setter
  void set_design_name(const std::string& design_name) { _design_name = design_name; }
  void set_net_list(const std::vector<DANet>& net_list) { _net_list = net_list; }
  void set_node_list(const std::vector<DANode>& node_list) { _node_list = node_list; }
  void set_inferred_pin_num(int32_t inferred_pin_num) { _inferred_pin_num = inferred_pin_num; }
  void set_multi_driver_net_num(int32_t multi_driver_net_num) { _multi_driver_net_num = multi_driver_net_num; }
  void set_unresolved_net_num(int32_t unresolved_net_num) { _unresolved_net_num = unresolved_net_num; }
  void set_undriven_net_num(int32_t undriven_net_num) { _undriven_net_num = undriven_net_num; }
  void set_excluded_pg_net_num(int32_t excluded_pg_net_num) { _excluded_pg_net_num = excluded_pg_net_num; }
  void set_excluded_io_boundary_net_num(int32_t excluded_io_boundary_net_num) { _excluded_io_boundary_net_num = excluded_io_boundary_net_num; }
  // function

 private:
  std::string _design_name;
  std::vector<DANet> _net_list;
  std::vector<DANode> _node_list;
  int32_t _inferred_pin_num = 0;
  int32_t _multi_driver_net_num = 0;
  int32_t _unresolved_net_num = 0;
  int32_t _undriven_net_num = 0;
  int32_t _excluded_pg_net_num = 0;
  int32_t _excluded_io_boundary_net_num = 0;
};

}  // namespace imj
