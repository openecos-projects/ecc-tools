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

#include "DCRect.hpp"

namespace imj {

class DCLayerRect
{
 public:
  DCLayerRect() = default;
  DCLayerRect(const std::string& layer_name, const DCRect& rect) : _layer_name(layer_name), _rect(rect) {}
  ~DCLayerRect() = default;
  // getter
  std::string& get_layer_name() { return _layer_name; }
  DCRect& get_rect() { return _rect; }
  // const getter
  const std::string& get_layer_name() const { return _layer_name; }
  const DCRect& get_rect() const { return _rect; }
  // setter
  void set_layer_name(const std::string& layer_name) { _layer_name = layer_name; }
  void set_rect(const DCRect& rect) { _rect = rect; }
  // function

 private:
  std::string _layer_name;
  DCRect _rect;
};

}  // namespace imj
