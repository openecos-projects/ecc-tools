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

#include "FPHeader.hpp"
#include "PPRegion.hpp"

namespace ifp {

class PPModel
{
 public:
  PPModel() = default;
  ~PPModel() = default;
  // getter
  std::vector<PPRegion>& get_pp_region_list() { return _pp_region_list; }
  std::vector<PPRegion>& get_boundary_region_list() { return _boundary_region_list; }
  std::vector<PPRegion>& get_occupied_region_list() { return _occupied_region_list; }
  int32_t get_top_y_coord() const { return _top_y_coord; }
  int32_t get_bottom_y_coord() const { return _bottom_y_coord; }

  // const getter
  const std::vector<PPRegion>& get_pp_region_list() const { return _pp_region_list; }
  const std::vector<PPRegion>& get_boundary_region_list() const { return _boundary_region_list; }
  const std::vector<PPRegion>& get_occupied_region_list() const { return _occupied_region_list; }

  // setter
  void set_pp_region_list(const std::vector<PPRegion>& pp_region_list) { _pp_region_list = pp_region_list; }
  void set_boundary_region_list(const std::vector<PPRegion>& boundary_region_list) { _boundary_region_list = boundary_region_list; }
  void set_occupied_region_list(const std::vector<PPRegion>& occupied_region_list) { _occupied_region_list = occupied_region_list; }
  void set_top_y_coord(int32_t top_y_coord) { _top_y_coord = top_y_coord; }
  void set_bottom_y_coord(int32_t bottom_y_coord) { _bottom_y_coord = bottom_y_coord; }

  // function

 private:
  std::vector<PPRegion> _pp_region_list;
  std::vector<PPRegion> _boundary_region_list;
  std::vector<PPRegion> _occupied_region_list;
  int32_t _top_y_coord = INT32_MIN;
  int32_t _bottom_y_coord = INT32_MAX;
};

}  // namespace ifp
