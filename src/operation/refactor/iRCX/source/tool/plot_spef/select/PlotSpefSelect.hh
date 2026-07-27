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
 * @file PlotSpefSelect.hh
 * @brief Select the plot_spef objects that should be visible.
 */
#pragma once

#include <string>

#include "model/PlotSpefVisibility.hh"

namespace spef {
struct Exchange;
}

namespace ircx::plot_spef {

struct Config;
struct Model;

auto makeVisibleObjects(const Model& model,
                        const spef::Exchange& exchange,
                        const Config& config) -> Visibility;

auto makeEdgeVisibleObjects(const Model& model,
                            const spef::Exchange& exchange,
                            const Config& config) -> Visibility;

}  // namespace ircx::plot_spef
