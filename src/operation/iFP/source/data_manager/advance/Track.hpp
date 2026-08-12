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

class Track
{
 public:
  Track() = default;
  ~Track() = default;
  // getter
  std::string& get_layer_name() { return _layer_name; }
  int32_t get_x_offset() const { return _x_offset; }
  int32_t get_x_pitch() const { return _x_pitch; }
  int32_t get_y_offset() const { return _y_offset; }
  int32_t get_y_pitch() const { return _y_pitch; }

  // const getter
  const std::string& get_layer_name() const { return _layer_name; }

  // setter
  void set_layer_name(std::string layer_name) { _layer_name = layer_name; }
  void set_x_offset(int32_t x_offset) { _x_offset = x_offset; }
  void set_x_pitch(int32_t x_pitch) { _x_pitch = x_pitch; }
  void set_y_offset(int32_t y_offset) { _y_offset = y_offset; }
  void set_y_pitch(int32_t y_pitch) { _y_pitch = y_pitch; }

  // function

 private:
  std::string _layer_name;
  int32_t _x_offset = -1;
  int32_t _x_pitch = -1;
  int32_t _y_offset = -1;
  int32_t _y_pitch = -1;
};

}  // namespace ifp
