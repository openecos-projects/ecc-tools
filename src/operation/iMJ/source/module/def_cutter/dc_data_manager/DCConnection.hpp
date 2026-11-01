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

#include "DCLayerRect.hpp"
#include "DCPinType.hpp"

namespace imj {

class DCConnection
{
 public:
  DCConnection() = default;
  ~DCConnection() = default;
  // getter
  int32_t get_object_idx() { return _object_idx; }
  std::string& get_pin_name() { return _pin_name; }
  bool get_is_io() { return _is_io; }
  DCPinType get_pin_type() { return _pin_type; }
  std::set<std::string>& get_layer_name_set() { return _layer_name_set; }
  std::vector<DCLayerRect>& get_shape_list() { return _shape_list; }
  bool get_retained() { return _retained; }
  // const getter
  int32_t get_object_idx() const { return _object_idx; }
  const std::string& get_pin_name() const { return _pin_name; }
  bool get_is_io() const { return _is_io; }
  DCPinType get_pin_type() const { return _pin_type; }
  const std::set<std::string>& get_layer_name_set() const { return _layer_name_set; }
  const std::vector<DCLayerRect>& get_shape_list() const { return _shape_list; }
  bool get_retained() const { return _retained; }
  // setter
  void set_object_idx(int32_t object_idx) { _object_idx = object_idx; }
  void set_pin_name(const std::string& pin_name) { _pin_name = pin_name; }
  void set_is_io(bool is_io) { _is_io = is_io; }
  void set_pin_type(DCPinType pin_type) { _pin_type = pin_type; }
  void set_layer_name_set(const std::set<std::string>& layer_name_set) { _layer_name_set = layer_name_set; }
  void set_shape_list(const std::vector<DCLayerRect>& shape_list) { _shape_list = shape_list; }
  void set_retained(bool retained) { _retained = retained; }
  // function

 private:
  int32_t _object_idx = -1;
  std::string _pin_name;
  bool _is_io = false;
  DCPinType _pin_type = DCPinType::kNone;
  std::set<std::string> _layer_name_set;
  std::vector<DCLayerRect> _shape_list;
  bool _retained = false;
};

}  // namespace imj
