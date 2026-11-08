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
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "PGSegment.hpp"
#include "PlanarRect.hpp"

namespace ifp {

class PGRepairGroup
{
 public:
  PGRepairGroup() = default;
  ~PGRepairGroup() = default;
  // getter
  std::string& get_net_name() { return _net_name; }
  PlanarRect& get_rail_rect() { return _rail_rect; }
  std::vector<PlanarRect>& get_rail_rect_list() { return _rail_rect_list; }
  std::vector<PGSegment>& get_candidate_list() { return _candidate_list; }
  int32_t get_selected_idx() const { return _selected_idx; }
  // const getter
  const std::string& get_net_name() const { return _net_name; }
  const PlanarRect& get_rail_rect() const { return _rail_rect; }
  const std::vector<PlanarRect>& get_rail_rect_list() const { return _rail_rect_list; }
  const std::vector<PGSegment>& get_candidate_list() const { return _candidate_list; }
  // setter
  void set_net_name(const std::string& net_name) { _net_name = net_name; }
  void set_rail_rect(const PlanarRect& rail_rect) { _rail_rect = rail_rect; }
  void set_selected_idx(int32_t selected_idx) { _selected_idx = selected_idx; }
  // function

 private:
  std::string _net_name;
  PlanarRect _rail_rect;
  std::vector<PlanarRect> _rail_rect_list;
  std::vector<PGSegment> _candidate_list;
  int32_t _selected_idx = -1;
};

}  // namespace ifp
