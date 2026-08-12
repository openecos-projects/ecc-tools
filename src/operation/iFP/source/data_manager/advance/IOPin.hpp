// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of the Mulan PSL v2 at:
//
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "FPHeader.hpp"
#include "IOPort.hpp"
#include "PlacementOrientation.hpp"

namespace ifp {

class IOPin
{
 public:
  IOPin() = default;
  ~IOPin() = default;
  // getter
  std::string& get_name() { return _name; }
  std::string& get_instance_name() { return _instance_name; }
  PlacementOrientation get_orient() const { return _orient; }
  std::vector<IOPort>& get_port_list() { return _port_list; }
  std::vector<IOPort>& get_new_port_list() { return _new_port_list; }
  int32_t get_x() const { return _x; }
  int32_t get_y() const { return _y; }
  int32_t get_offset_x() const { return _offset_x; }
  int32_t get_offset_y() const { return _offset_y; }
  bool get_port_exist() const { return _port_exist; }
  bool get_special_net() const { return _special_net; }
  bool get_placed() const { return _placed; }
  bool get_fixed() const { return _fixed; }
  bool get_direct_location() const { return _direct_location; }
  bool is_offset_updated() const { return _offset_updated; }
  bool is_port_exist_updated() const { return _port_exist_updated; }
  bool is_updated() const { return _updated; }

  // const getter
  const std::string& get_name() const { return _name; }
  const std::string& get_instance_name() const { return _instance_name; }
  const std::vector<IOPort>& get_port_list() const { return _port_list; }
  const std::vector<IOPort>& get_new_port_list() const { return _new_port_list; }

  // setter
  void set_name(std::string name) { _name = name; }
  void set_instance_name(std::string instance_name) { _instance_name = instance_name; }
  void set_orient(PlacementOrientation orient) { _orient = orient; }
  void set_port_list(const std::vector<IOPort>& port_list) { _port_list = port_list; }
  void set_new_port_list(const std::vector<IOPort>& new_port_list) { _new_port_list = new_port_list; }
  void set_x(int32_t x) { _x = x; }
  void set_y(int32_t y) { _y = y; }
  void set_coord(int32_t x, int32_t y)
  {
    _x = x;
    _y = y;
  }
  void set_offset_x(int32_t offset_x) { _offset_x = offset_x; }
  void set_offset_y(int32_t offset_y) { _offset_y = offset_y; }
  void set_offset(int32_t offset_x, int32_t offset_y)
  {
    _offset_x = offset_x;
    _offset_y = offset_y;
  }
  void set_port_exist(bool port_exist) { _port_exist = port_exist; }
  void set_special_net(bool special_net) { _special_net = special_net; }
  void set_placed(bool placed) { _placed = placed; }
  void set_fixed(bool fixed) { _fixed = fixed; }
  void set_direct_location(bool direct_location) { _direct_location = direct_location; }
  void set_offset_updated(bool offset_updated) { _offset_updated = offset_updated; }
  void set_port_exist_updated(bool port_exist_updated) { _port_exist_updated = port_exist_updated; }
  void set_updated(bool updated) { _updated = updated; }

  // function

 private:
  std::string _name;
  std::string _instance_name;
  PlacementOrientation _orient = PlacementOrientation::kNone;
  std::vector<IOPort> _port_list;
  std::vector<IOPort> _new_port_list;
  int32_t _x = -1;
  int32_t _y = -1;
  int32_t _offset_x = -1;
  int32_t _offset_y = -1;
  bool _port_exist = false;
  bool _special_net = false;
  bool _placed = false;
  bool _fixed = false;
  bool _direct_location = false;
  bool _offset_updated = false;
  bool _port_exist_updated = false;
  bool _updated = false;
};

}  // namespace ifp
