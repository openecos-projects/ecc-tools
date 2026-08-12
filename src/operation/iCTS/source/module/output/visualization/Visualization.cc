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
 * @file Visualization.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-30
 * @brief CTS clock-tree visualization report output implementation.
 */

#include "output/visualization/Visualization.hh"

#include "output/visualization/gds/GDSVisualization.hh"
#include "output/visualization/svg/SVGVisualization.hh"

namespace icts {

auto Visualization::emit(const std::filesystem::path& visualization_dir, const Config& cts_config, const Drawing& drawing, const VisualizationConfig& config)
    -> VisualizationSummary
{
  const auto svg_summary = config.emit_svg ? visualization::EmitSvgVisualizations(visualization_dir, cts_config, drawing)
                                           : visualization::SvgVisualizationSummary{.success = true};
  const auto gds_summary = config.emit_gds ? visualization::EmitGdsVisualizations(visualization_dir, cts_config, drawing)
                                           : visualization::GdsVisualizationSummary{.success = true};
  return VisualizationSummary{
      .svg_success = svg_summary.success,
      .gds_success = gds_summary.success,
      .success = svg_summary.success && gds_summary.success,
  };
}

}  // namespace icts
