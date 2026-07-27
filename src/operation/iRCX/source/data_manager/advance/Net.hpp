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

#include "Patch.hpp"
#include "Pin.hpp"
#include "Segment.hpp"
#include "Via.hpp"

namespace ircx {

class Net
{
 public:
  Net() = default;
  ~Net() = default;
  // getter
  int32_t get_net_idx() const { return _net_idx; }
  std::string& get_net_name() { return _net_name; }
  std::vector<Segment>& get_segment_list() { return _segment_list; }
  std::vector<Patch>& get_patch_list() { return _patch_list; }
  std::vector<Via>& get_via_list() { return _via_list; }
  std::vector<Pin>& get_pin_list() { return _pin_list; }
  // const getter
  const std::vector<Segment>& get_segment_list() const { return _segment_list; }
  const std::vector<Patch>& get_patch_list() const { return _patch_list; }
  const std::vector<Via>& get_via_list() const { return _via_list; }
  const std::vector<Pin>& get_pin_list() const { return _pin_list; }
  // setter
  void set_net_idx(int32_t net_idx) { _net_idx = net_idx; }
  void set_net_name(const std::string& net_name) { _net_name = net_name; }
  void set_segment_list(const std::vector<Segment>& segment_list) { _segment_list = segment_list; }
  void set_patch_list(const std::vector<Patch>& patch_list) { _patch_list = patch_list; }
  void set_via_list(const std::vector<Via>& via_list) { _via_list = via_list; }
  void set_pin_list(const std::vector<Pin>& pin_list) { _pin_list = pin_list; }
  // function

 private:
  int32_t _net_idx = -1;
  std::string _net_name;
  std::vector<Segment> _segment_list;
  std::vector<Patch> _patch_list;
  std::vector<Via> _via_list;
  std::vector<Pin> _pin_list;
};

}  // namespace ircx
