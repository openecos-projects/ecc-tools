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
 * @file Evaluation.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-30
 * @brief CTS evaluation entry facade implementation.
 */

#include "evaluation/Evaluation.hh"

#include <string>
#include <utility>

#include "LogTable.hh"
#include "Logger.hh"
#include "Monitor.hh"
#include "data_manager/DataManager.hh"

namespace icts {

auto Evaluation::run() -> EvaluationBuild
{
  Monitor monitor;
  CTSLOG.info(Loc::current(), "Starting CTS evaluation...");
  QorEvaluationModel model{
      .config = CTSDM.getConfig(),
      .design = CTSDM.getDesign(),
      .wrapper = CTSDM.getWrapper(),
      .state = {},
  };
  QorEvaluation::evaluate(model);
  auto& evaluation_state = model.state;
  const bool evaluation_ready = isEvaluationReady(evaluation_state);
  const auto& qor_summary = evaluation_state.summary;
  const auto& statistics = evaluation_state.statistics;
  EmitLogTable(
      Loc::current(), "CTS Evaluation Overview", {"Metric", "Value"},
      {{"Ready", ToLogTableCell(evaluation_ready)},
       {"QoR Metric Status", qor_summary.qor_metric_status},
       {"Physical Metric Source", qor_summary.physical_metric_source},
       {"Path Depth Status", qor_summary.path_depth_metric_status},
       {"DBU per um", qor_summary.design_dbu_per_um.has_value() ? ToLogTableCell(*qor_summary.design_dbu_per_um) : "unavailable"},
       {"Final Clock Buffers", ToLogTableCell(qor_summary.final_clock_buffer_count)},
       {"Final Buffer Area (um^2)", qor_summary.final_buffer_area_um2.has_value() ? ToLogTableCell(*qor_summary.final_buffer_area_um2) : "unavailable"},
       {"Clock Member Buffers", ToLogTableCell(qor_summary.clock_member_buffer_count)},
       {"Path Buffer Minimum", ToLogTableCell(qor_summary.clock_path_min_buffer)},
       {"Path Buffer Maximum", ToLogTableCell(qor_summary.clock_path_max_buffer)},
       {"Maximum Clock Level", ToLogTableCell(qor_summary.max_clock_network_level)}});
  EmitLogTable(Loc::current(), "CTS Evaluation Wirelength", {"Region", "Routed (um)", "HPWL (um)"},
               {{"Top", ToLogTableCell(statistics.top_wirelength_um), ToLogTableCell(statistics.hpwl_top_wirelength_um)},
                {"Trunk", ToLogTableCell(statistics.trunk_wirelength_um), ToLogTableCell(statistics.hpwl_trunk_wirelength_um)},
                {"Leaf", ToLogTableCell(statistics.leaf_wirelength_um), ToLogTableCell(statistics.hpwl_leaf_wirelength_um)},
                {"Total", ToLogTableCell(statistics.total_wirelength_um), ToLogTableCell(statistics.hpwl_total_wirelength_um)},
                {"Maximum Net", ToLogTableCell(statistics.max_net_wirelength_um), ToLogTableCell(statistics.hpwl_max_net_wirelength_um)},
                {"Valid", ToLogTableCell(statistics.valid), "-"}});
  auto build = EvaluationBuild{
      .output = EvaluationOutput{.state = std::move(evaluation_state)},
      .summary = EvaluationSummary{.evaluation_ready = evaluation_ready, .status = evaluation_ready ? "finished" : "failed"},
  };
  if (build.summary.evaluation_ready) {
    const auto commit_status = CTSDM.commitEvaluation(build.output.state);
    if (!commit_status.ok()) {
      build.summary.evaluation_ready = false;
      build.summary.status = "failed";
      CTSLOG.warn(Loc::current(), "CTS evaluation commit failed: ", commit_status.message);
    }
  }
  CTSLOG.info(Loc::current(), "Completed CTS evaluation", monitor.getStatsInfo());
  return build;
}

auto Evaluation::outputSummary(const EvaluationState& state) -> QorSummary
{
  return QorEvaluation::outputSummary(state);
}

auto Evaluation::isEvaluationReady(const EvaluationState& state) -> bool
{
  return QorEvaluation::isEvaluationReady(state);
}

auto Evaluation::reset(EvaluationState& state) -> void
{
  QorEvaluation::reset(state);
}

}  // namespace icts
