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
/**
 * @file CapTableQuery.cc
 * @brief iRCX module implementation detail.
 */
#include "CapTableQuery.hh"

#include <algorithm>

namespace ircx {

auto CapTableQuery::nearCap(const std::string& below_layer,
                            const std::string& above_layer,
                            Micron spacing) const -> parser::CapacitanceResult
{
  const Micron lookup_dist = std::max<Micron>(spacing, 0.0);
  if (above_layer.empty()) {
    return cap_table_.queryTwoLayerCap(layer_name_, below_layer, lookup_dist);
  }
  return cap_table_.queryThreeLayerCap(
      layer_name_, below_layer, above_layer, lookup_dist);
}

auto CapTableQuery::farthestCap(const std::string& below_layer,
                                const std::string& above_layer) const -> parser::CapacitanceResult
{
  if (above_layer.empty()) {
    return cap_table_.queryTwoLayerFarthestCap(layer_name_, below_layer);
  }
  return cap_table_.queryThreeLayerFarthestCap(
      layer_name_, below_layer, above_layer);
}

}  // namespace ircx
