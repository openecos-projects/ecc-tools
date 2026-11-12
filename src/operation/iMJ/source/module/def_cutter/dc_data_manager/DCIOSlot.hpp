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

namespace imj {

class DCIOSlot
{
 public:
  DCIOSlot() = default;
  ~DCIOSlot() = default;
  // getter
  DCLayerRect& get_shape() { return _shape; }
  bool get_available() { return _available; }
  // const getter
  const DCLayerRect& get_shape() const { return _shape; }
  bool get_available() const { return _available; }
  // setter
  void set_shape(const DCLayerRect& shape) { _shape = shape; }
  void set_available(bool available) { _available = available; }
  // function

 private:
  DCLayerRect _shape;
  bool _available = true;
};

}  // namespace imj
