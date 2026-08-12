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

#include "EnvPixelOverlap.hpp"
#include "RCXHeader.hpp"

namespace ircx {

class EnvLayerPixelOverlapList
{
 public:
  EnvLayerPixelOverlapList() = default;
  EnvLayerPixelOverlapList(int32_t layer_idx, const std::vector<EnvPixelOverlap>& pixel_overlap_list)
      : _layer_idx(layer_idx), _pixel_overlap_list(pixel_overlap_list)
  {
  }
  ~EnvLayerPixelOverlapList() = default;
  // getter
  int32_t get_layer_idx() const { return _layer_idx; }
  std::vector<EnvPixelOverlap>& get_pixel_overlap_list() { return _pixel_overlap_list; }
  const std::vector<EnvPixelOverlap>& get_pixel_overlap_list() const { return _pixel_overlap_list; }
  // setter
  void set_layer_idx(int32_t layer_idx) { _layer_idx = layer_idx; }
  void set_pixel_overlap_list(const std::vector<EnvPixelOverlap>& pixel_overlap_list) { _pixel_overlap_list = pixel_overlap_list; }
  void set_pixel_overlap_list(std::vector<EnvPixelOverlap>&& pixel_overlap_list) { _pixel_overlap_list = std::move(pixel_overlap_list); }
  // function

 private:
  int32_t _layer_idx = -1;
  std::vector<EnvPixelOverlap> _pixel_overlap_list;
};

}  // namespace ircx
