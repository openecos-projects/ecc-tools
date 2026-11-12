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

#include "DCConnection.hpp"
#include "DCRoute.hpp"

namespace imj {

class DCNet
{
 public:
  DCNet() = default;
  ~DCNet() = default;
  // getter
  int32_t get_source_idx() { return _source_idx; }
  std::string& get_name() { return _name; }
  bool get_is_special() { return _is_special; }
  bool get_is_pg() { return _is_pg; }
  int32_t get_cut_pin_idx() { return _cut_pin_idx; }
  std::vector<DCLayerRect>& get_io_shape_list() { return _io_shape_list; }
  std::vector<DCConnection>& get_connection_list() { return _connection_list; }
  std::vector<DCRoute>& get_route_list() { return _route_list; }
  bool get_retained() { return _retained; }
  int32_t get_removed_pin_num() { return _removed_pin_num; }
  // const getter
  int32_t get_source_idx() const { return _source_idx; }
  const std::string& get_name() const { return _name; }
  bool get_is_special() const { return _is_special; }
  bool get_is_pg() const { return _is_pg; }
  int32_t get_cut_pin_idx() const { return _cut_pin_idx; }
  const std::vector<DCLayerRect>& get_io_shape_list() const { return _io_shape_list; }
  const std::vector<DCConnection>& get_connection_list() const { return _connection_list; }
  const std::vector<DCRoute>& get_route_list() const { return _route_list; }
  bool get_retained() const { return _retained; }
  int32_t get_removed_pin_num() const { return _removed_pin_num; }
  // setter
  void set_source_idx(int32_t source_idx) { _source_idx = source_idx; }
  void set_name(const std::string& name) { _name = name; }
  void set_is_special(bool is_special) { _is_special = is_special; }
  void set_is_pg(bool is_pg) { _is_pg = is_pg; }
  void set_cut_pin_idx(int32_t cut_pin_idx) { _cut_pin_idx = cut_pin_idx; }
  void set_io_shape_list(const std::vector<DCLayerRect>& io_shape_list) { _io_shape_list = io_shape_list; }
  void set_connection_list(const std::vector<DCConnection>& connection_list) { _connection_list = connection_list; }
  void set_route_list(const std::vector<DCRoute>& route_list) { _route_list = route_list; }
  void set_retained(bool retained) { _retained = retained; }
  void set_removed_pin_num(int32_t removed_pin_num) { _removed_pin_num = removed_pin_num; }
  // function

 private:
  int32_t _source_idx = -1;
  std::string _name;
  bool _is_special = false;
  bool _is_pg = false;
  int32_t _cut_pin_idx = -1;
  std::vector<DCLayerRect> _io_shape_list;
  std::vector<DCConnection> _connection_list;
  std::vector<DCRoute> _route_list;
  bool _retained = false;
  int32_t _removed_pin_num = 0;
};

}  // namespace imj
