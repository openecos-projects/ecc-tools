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
 * @file Output.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-30
 * @brief CTS report entry facade implementation.
 */

#include "output/Output.hh"

#include <filesystem>

#include "LogTable.hh"
#include "Logger.hh"
#include "Monitor.hh"
#include "config/Config.hh"
#include "data_manager/DataManager.hh"
#include "output/export/OutputPathResolver.hh"
#include "output/qor/QORReport.hh"
#include "output/visualization/Visualization.hh"
#include "output/visualization/drawing/Drawing.hh"

namespace icts {

auto Output::run(const std::string& save_dir) -> OutputSummary
{
  Monitor monitor;
  CTSLOG.info(Loc::current(), "Starting CTS output...");
  const auto* evaluation_state = CTSDM.getCommittedEvaluationState();
  if (evaluation_state == nullptr) {
    CTSLOG.warn(Loc::current(), "CTS output requires a committed evaluation result.");
    CTSLOG.info(Loc::current(), "Completed CTS output with failure", monitor.getStatsInfo());
    return {};
  }
  const auto& config = CTSDM.getConfig();
  if (config.get_work_dir().empty()) {
    CTSLOG.error(Loc::current(), "CTS output requires an initialized CTS session.");
  }

  const auto paths = OutputPathResolver::resolvePaths(config, save_dir);
  const bool statistics_success = QorReport::write(*evaluation_state, paths.statistics_dir.string());
  const auto drawing = DrawingBuilder::build(DrawingInput{
      .design = &CTSDM.getDesign(),
      .wrapper = &CTSDM.getWrapper(),
      .clock_layout = &CTSDM.getClockLayout(),
  });
  const auto visualization_summary = Visualization::emit(paths.visualization_dir, config, drawing);
  const bool output_success = statistics_success && visualization_summary.success;

  auto summary = OutputSummary{
      .success = output_success,
      .evaluation_ready = true,
      .statistics_success = statistics_success,
      .svg_success = visualization_summary.svg_success,
      .gds_success = visualization_summary.gds_success,
  };
  EmitLogTable(Loc::current(), "CTS Report Artifacts", {"Artifact", "Path", "Status"},
               {{"Output Root", paths.output_root_dir.string(), summary.success ? "ready" : "incomplete"},
                {"Statistics Directory", paths.statistics_dir.string(), summary.statistics_success ? "ready" : "failed"},
                {"Visualization Directory", paths.visualization_dir.string(), visualization_summary.success ? "ready" : "failed"},
                {"Wirelength Report", (paths.statistics_dir / "wirelength.rpt").string(), summary.statistics_success ? "written" : "failed"},
                {"Cell Statistics Report", (paths.statistics_dir / "cell_stats.rpt").string(), summary.statistics_success ? "written" : "failed"},
                {"Library Cell Distribution Report", (paths.statistics_dir / "lib_cell_dist.rpt").string(), summary.statistics_success ? "written" : "failed"},
                {"SVG", paths.visualization_dir.string(), summary.svg_success ? "written" : "failed"},
                {"GDS", paths.visualization_dir.string(), summary.gds_success ? "written" : "failed"}});
  CTSLOG.info(Loc::current(), output_success ? "Completed CTS output" : "Completed CTS output with failure", monitor.getStatsInfo());
  return summary;
}

}  // namespace icts
