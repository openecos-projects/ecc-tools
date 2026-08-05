// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of the Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "MIFillShape.hpp"

namespace izh {

class MILayerRule
{
 public:
  MILayerRule() = default;
  ~MILayerRule() = default;
  // getter
  std::string& get_layer_name() { return _layer_name; }
  std::vector<MIFillShape>& get_fill_shape_list() { return _fill_shape_list; }
  bool get_is_horizontal() const { return _is_horizontal; }
  int32_t get_space_to_fill() const { return _space_to_fill; }
  int32_t get_space_to_non_fill() const { return _space_to_non_fill; }
  // const getter
  const std::string& get_layer_name() const { return _layer_name; }
  const std::vector<MIFillShape>& get_fill_shape_list() const { return _fill_shape_list; }
  // setter
  void set_layer_name(const std::string& layer_name) { _layer_name = layer_name; }
  void set_fill_shape_list(const std::vector<MIFillShape>& fill_shape_list) { _fill_shape_list = fill_shape_list; }
  void set_is_horizontal(bool is_horizontal) { _is_horizontal = is_horizontal; }
  void set_space_to_fill(int32_t space_to_fill) { _space_to_fill = space_to_fill; }
  void set_space_to_non_fill(int32_t space_to_non_fill) { _space_to_non_fill = space_to_non_fill; }

 private:
  std::string _layer_name;
  std::vector<MIFillShape> _fill_shape_list;
  bool _is_horizontal = false;
  int32_t _space_to_fill = 0;
  int32_t _space_to_non_fill = 0;
};

}  // namespace izh
