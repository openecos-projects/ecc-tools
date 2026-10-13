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
  bool get_is_obs() const { return _is_obs; }
  bool get_is_special_net() const { return _is_special_net; }
  ids::Shape::SourceType get_source_type() const { return _source_type; }
  // setter
  void set_net_idx(const int32_t net_idx) { _net_idx = net_idx; }
  void set_is_routing(const bool is_routing) { _is_routing = is_routing; }
  void set_is_obs(const bool is_obs) { _is_obs = is_obs; }
  void set_is_special_net(const bool is_special_net) { _is_special_net = is_special_net; }
  void set_source_type(ids::Shape::SourceType source_type)
  {
    _source_type = source_type;
    if (source_type == ids::Shape::SourceType::kInstanceObs) {
      _is_obs = true;
    } else if (source_type == ids::Shape::SourceType::kSpecialWire) {
      _is_special_net = true;
    }
  }
  // function

 private:
  int32_t _net_idx = -1;
  bool _is_routing = true;
  bool _is_obs = false;
  bool _is_special_net = false;
  ids::Shape::SourceType _source_type = ids::Shape::SourceType::kUnknown;
};

}  // namespace idrc
