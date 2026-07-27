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

#include "PlanarCoord.hpp"
#include "PlanarRect.hpp"
#include "RTHeader.hpp"

namespace irt {

class TBTask
{
 public:
  TBTask() = default;
  ~TBTask() = default;
  // getter
  std::vector<PlanarCoord>& get_planar_coord_list() { return _planar_coord_list; }
  const std::vector<PlanarRect>& get_planar_obs_list() const { return _planar_obs_list; }
  const PlanarRect& get_planar_search_region() const { return _planar_search_region; }
  bool has_planar_search_region() const { return _has_planar_search_region; }
  // const getter
  const std::vector<PlanarCoord>& get_planar_coord_list() const { return _planar_coord_list; }
  // setter
  void set_planar_coord_list(std::vector<PlanarCoord> planar_coord_list) { _planar_coord_list = std::move(planar_coord_list); }
  void set_planar_obs_list(std::vector<PlanarRect> planar_obs_list) { _planar_obs_list = std::move(planar_obs_list); }
  void set_planar_search_region(const PlanarRect& planar_search_region)
  {
    _planar_search_region = planar_search_region;
    _has_planar_search_region = true;
  }
  // function
 private:
  std::vector<PlanarCoord> _planar_coord_list;
  // GCell-coordinate obstacles with inclusive lower and upper bounds.
  std::vector<PlanarRect> _planar_obs_list;
  PlanarRect _planar_search_region;
  bool _has_planar_search_region = false;
};

}  // namespace irt
