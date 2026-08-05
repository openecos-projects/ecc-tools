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
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "FPHeader.hpp"

namespace ifp {

class NetPin
{
 public:
  NetPin() = default;
  ~NetPin() = default;
  // getter
  std::string& get_instance_name() { return _instance_name; }
  std::string& get_pin_name() { return _pin_name; }
  int32_t get_x() const { return _x; }
  int32_t get_y() const { return _y; }
  int32_t get_offset_x() const { return _offset_x; }
  int32_t get_offset_y() const { return _offset_y; }
  bool get_placed() const { return _placed; }
  bool get_io() const { return _io; }

  // const getter
  const std::string& get_instance_name() const { return _instance_name; }
  const std::string& get_pin_name() const { return _pin_name; }

  // setter
  void set_instance_name(std::string instance_name) { _instance_name = instance_name; }
  void set_pin_name(std::string pin_name) { _pin_name = pin_name; }
  void set_x(int32_t x) { _x = x; }
  void set_y(int32_t y) { _y = y; }
  void set_coord(int32_t x, int32_t y)
  {
    _x = x;
    _y = y;
  }
  void set_offset_x(int32_t offset_x) { _offset_x = offset_x; }
  void set_offset_y(int32_t offset_y) { _offset_y = offset_y; }
  void set_placed(bool placed) { _placed = placed; }
  void set_io(bool io) { _io = io; }

  // function

 private:
  std::string _instance_name;
  std::string _pin_name;
  int32_t _x = -1;
  int32_t _y = -1;
  int32_t _offset_x = -1;
  int32_t _offset_y = -1;
  bool _placed = false;
  bool _io = false;
};

}  // namespace ifp
