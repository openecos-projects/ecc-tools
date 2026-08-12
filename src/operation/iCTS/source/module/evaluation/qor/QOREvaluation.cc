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
 * @file QOREvaluation.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-26
 * @brief CTS clock-tree evaluation stage implementation.
 */

#include "evaluation/qor/QOREvaluation.hh"

#include <optional>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Logger.hh"
#include "config/Config.hh"
#include "design/Clock.hh"
#include "design/ClockDAG.hh"
#include "design/Design.hh"
#include "design/Inst.hh"
#include "evaluation/qor/ClockQORMetricCollector.hh"
#include "io/Wrapper.hh"
#include "qor/QOR.hh"

namespace icts {

auto QorEvaluation::evaluate(QorEvaluationModel& model) -> void
{
  const auto& config = model.config.get();
  auto& design = model.design.get();
  auto& wrapper = model.wrapper.get();
  auto& state = model.state;
  auto& summary = state.summary;
  auto& statistics = state.statistics;
  qor_evaluation::ClearSummary(summary);
  qor_evaluation::ClearStatistics(statistics);

  auto clocks = design.get_clocks();
  const auto design_dbu_per_um = wrapper.queryDbUnit();
  if (!design_dbu_per_um.has_value()) {
    CTSLOG.warn(Loc::current(), "CTS evaluation is unavailable because DBU-per-micron is missing.");
    return;
  }
  const int32_t resolved_dbu_per_um = design_dbu_per_um.value();
  summary.design_dbu_per_um = resolved_dbu_per_um;
  const bool clock_dag_valid = design.rebuildClockDAG();
  const auto& clock_dag = design.get_clock_dag();
  qor_evaluation::AppendPathDepthStats(clock_dag.pathBufferStats(), summary);
  if (!clock_dag_valid) {
    CTSLOG.warn(Loc::current(), "CTS evaluation skipped because committed topology is not a valid clock DAG: ", clock_dag.get_status());
    return;
  }

  summary.final_buffer_area_um2 = 0.0;
  std::unordered_set<const Inst*> counted_buffer_insts;
  std::vector<qor_evaluation::ClockNetMeasurement> clock_net_measurements;
  bool cell_statistics_complete = true;
  for (auto* clock : clocks) {
    if (clock == nullptr) {
      continue;
    }

    int32_t clock_member_buffer_count = 0;
    for (auto* inst : clock->get_insts()) {
      if (inst == nullptr || !inst->is_buffer()) {
        continue;
      }
      ++clock_member_buffer_count;
      const bool is_new_buffer_inst = counted_buffer_insts.insert(inst).second;
      if (is_new_buffer_inst) {
        ++summary.final_clock_buffer_count;
        cell_statistics_complete = qor_evaluation::AccumulateInstStatistics(wrapper, *inst, statistics) && cell_statistics_complete;
      }
      if (is_new_buffer_inst) {
        const auto area_um2 = wrapper.is_layout_ready() ? wrapper.queryCellAreaUm2(inst->get_cell_master()) : std::nullopt;
        if (!area_um2.has_value() || !summary.final_buffer_area_um2.has_value()) {
          summary.final_buffer_area_um2 = std::nullopt;
          cell_statistics_complete = false;
        } else {
          *summary.final_buffer_area_um2 += *area_um2;
        }
      }
    }
    summary.clock_member_buffer_count += clock_member_buffer_count;

    for (auto* net : clock_dag.reachableNets(clock)) {
      if (net == nullptr) {
        continue;
      }
      if (auto measurement = qor_evaluation::MeasureClockNet(qor_evaluation::ClockNetMeasurementInput{
              .config = &config,
              .wrapper = &wrapper,
              .net = net,
              .role = qor_evaluation::ClassifyClockNet(*clock, net),
          });
          measurement.has_value()) {
        clock_net_measurements.push_back(*measurement);
      }
    }
  }

  qor_evaluation::AppendClockNetStatistics(clock_net_measurements, resolved_dbu_per_um, summary, statistics);
  if (clock_net_measurements.empty()) {
    summary.physical_metric_source = "unavailable";
  } else {
    summary.physical_metric_source = "cts_clock_tree";
  }
  if (clock_net_measurements.empty()) {
    summary.qor_metric_status = "unavailable";
  } else {
    summary.qor_metric_status = cell_statistics_complete ? "available" : "partial";
  }
  statistics.valid = !clock_net_measurements.empty();
  summary.has_evaluation_result = statistics.valid;
}

auto QorEvaluation::outputSummary(const EvaluationState& state) -> QorSummary
{
  return state.summary;
}

auto QorEvaluation::isEvaluationReady(const EvaluationState& state) -> bool
{
  return state.summary.has_evaluation_result && state.statistics.valid;
}

auto QorEvaluation::reset(EvaluationState& state) -> void
{
  qor_evaluation::ClearSummary(state.summary);
  qor_evaluation::ClearStatistics(state.statistics);
}

}  // namespace icts
