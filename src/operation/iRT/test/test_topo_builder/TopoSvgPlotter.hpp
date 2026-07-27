// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You may obtain a copy of the Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include <string>
#include <vector>

#include "PlanarCoord.hpp"
#include "PlanarRect.hpp"
#include "Segment.hpp"

namespace irt {

struct TopoSvgPlotRequest
{
  std::string title;
  const PlanarRect& planar_search_region;
  const std::vector<PlanarRect>& planar_obs_list;
  const std::vector<PlanarCoord>& terminal_coord_list;
  const std::vector<Segment<PlanarCoord>>& flute_topo_list;
  const std::vector<Segment<PlanarCoord>>& legal_topo_list;
};

bool writeTopoSvg(const std::string& file_path, const TopoSvgPlotRequest& request, std::string& error_message);

}  // namespace irt
