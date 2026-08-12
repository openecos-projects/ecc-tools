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

#include "PlanarRect.hpp"

namespace ifp {

class IOPort : public PlanarRect
{
 public:
  IOPort() = default;
  ~IOPort() = default;
  // getter
  std::string& get_layer_name() { return _layer_name; }
  int32_t get_x() const { return _x; }
  int32_t get_y() const { return _y; }
  bool get_placed() const { return _placed; }

  // const getter
  const std::string& get_layer_name() const { return _layer_name; }

  // setter
  void set_layer_name(std::string layer_name) { _layer_name = layer_name; }
  void set_x(int32_t x) { _x = x; }
  void set_y(int32_t y) { _y = y; }
  void set_coord(int32_t x, int32_t y)
  {
    _x = x;
    _y = y;
  }
  void set_placed(bool placed) { _placed = placed; }

  // function

 private:
  std::string _layer_name;
  int32_t _x = -1;
  int32_t _y = -1;
  bool _placed = false;
};

}  // namespace ifp
