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
 * @file PlotSpefCgEdgeReport.hh
 * @brief Write SPEF *RES indexes for ground caps that can be assigned to edges.
 */
#pragma once

#include "RCXHeader.hpp"
#include "Types.hh"

namespace ircx::plot_spef {

struct Config;
struct Model;

struct EdgeRow
{
  std::string net_name;
  Size res_index = 0;
};

auto collectCgEdgeRows(const Model& model) -> std::vector<EdgeRow>;

auto collectCoupledEdgeRows(const Model& model) -> std::vector<EdgeRow>;

class CgEdgeReport
{
 public:
  auto write(const Model& model, const Config& config) const -> bool;
};

}  // namespace ircx::plot_spef
