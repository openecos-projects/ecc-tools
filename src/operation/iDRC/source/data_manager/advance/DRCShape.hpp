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

#include "DRCHeader.hpp"
#include "../../../../../database/interaction/RT_DRC/ids.hpp"
#include "LayerRect.hpp"
#include "Utility.hpp"

namespace idrc {

class DRCShape : public LayerRect
{
 public:
  DRCShape() = default;
  DRCShape(int32_t net_idx, const LayerRect& layer_rect, bool is_routing) : LayerRect(layer_rect)
  {
    _net_idx = net_idx;
    _is_routing = is_routing;
  }
  ~DRCShape() = default;
  // getter
  int32_t get_net_idx() const { return _net_idx; }
  bool get_is_routing() const { return _is_routing; }
  ids::Shape::SourceType get_source_type() const { return _source_type; }
  const std::string& get_via_name() const { return _via_name; }
  const std::string& get_via_master_name() const { return _via_master_name; }
  int32_t get_via_cut_idx() const { return _via_cut_idx; }
  int32_t get_via_cut_count() const { return _via_cut_count; }
  // setter
  void set_net_idx(const int32_t net_idx) { _net_idx = net_idx; }
  void set_is_routing(const bool is_routing) { _is_routing = is_routing; }
  void set_source_type(ids::Shape::SourceType source_type) { _source_type = source_type; }
  void set_via_name(std::string via_name) { _via_name = std::move(via_name); }
  void set_via_master_name(std::string via_master_name) { _via_master_name = std::move(via_master_name); }
  void set_via_cut_idx(const int32_t via_cut_idx) { _via_cut_idx = via_cut_idx; }
  void set_via_cut_count(const int32_t via_cut_count) { _via_cut_count = via_cut_count; }
  // function

 private:
  int32_t _net_idx = -1;
  bool _is_routing = true;
  ids::Shape::SourceType _source_type = ids::Shape::SourceType::kUnknown;
  std::string _via_name;
  std::string _via_master_name;
  int32_t _via_cut_idx = -1;
  int32_t _via_cut_count = 0;
};

}  // namespace idrc
