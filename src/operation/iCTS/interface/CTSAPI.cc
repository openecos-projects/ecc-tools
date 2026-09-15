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
 * @file CTSAPI.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-01-07
 * @brief iCTS API implementation.
 */
#include "CTSAPI.hh"

#include <string>
#include <utility>
#include <vector>

#include "LogTable.hh"
#include "Logger.hh"
#include "Monitor.hh"
#include "Utility.hh"
#include "data_manager/DataManager.hh"
#include "evaluation/Evaluation.hh"
#include "evaluation/qor/QOREvaluation.hh"
#include "feature_icts.h"
#include "instantiation/Instantiation.hh"
#include "optimization/Optimization.hh"
#include "output/Output.hh"
#include "synthesis/Synthesis.hh"

namespace icts {
namespace {

auto buildFeatureSummary(const QorSummary& qor_summary) -> ecc_feature::CTSSummary
{
  ecc_feature::CTSSummary summary{};
  summary.buffer_num = qor_summary.final_clock_buffer_count;
  summary.buffer_area = qor_summary.final_buffer_area_um2;
  summary.clock_path_min_buffer = qor_summary.clock_path_min_buffer;
  summary.clock_path_max_buffer = qor_summary.clock_path_max_buffer;
  summary.max_level_of_clock_tree = qor_summary.max_clock_network_level;
  summary.max_clock_wirelength = qor_summary.max_clock_net_wirelength_dbu;
  summary.total_clock_wirelength = qor_summary.total_clock_network_wirelength_dbu;
  return summary;
}

auto buildOkStatus(std::string message, std::vector<std::string> diagnostics = {}) -> CTSStatus
{
  return CTSStatus{.code = CTSStatusCode::kOk, .message = std::move(message), .diagnostics = std::move(diagnostics)};
}

auto buildInputStatus(const DataManagerStatus& input_status) -> CTSStatus
{
  if (input_status.ok()) {
    return buildOkStatus(input_status.message, input_status.diagnostics);
  }
  return CTSStatus{.code = CTSStatusCode::kConfigError, .message = input_status.message, .diagnostics = input_status.diagnostics};
}

}  // namespace

CTSAPI::CTSAPI() = default;

CTSAPI::~CTSAPI() = default;

auto CTSAPI::setLastStatus(CTSStatus status) -> CTSStatus
{
  _last_status = std::move(status);
  return _last_status;
}

auto CTSAPI::runCTS(const std::string& config_file, const std::string& work_dir) -> CTSStatus
{
  auto init_status = init(config_file, work_dir);
  if (!init_status.ok()) {
    return init_status;
  }
  return runCTS();
}

auto CTSAPI::runCTS() -> CTSStatus
{
  auto& api = getInst();
  if (!api._initialized) {
    return api.setLastStatus(CTSStatus{.code = CTSStatusCode::kNotInitialized, .message = "CTS is not initialized.", .diagnostics = {}});
  }

  Monitor monitor;
  CTSLOG.info(Loc::current(), "Starting CTS flow...");
  const auto synthesis = Synthesis::run();
  if (synthesis.outcome == SynthesisOutcome::kNoOp) {
    CTSLOG.info(Loc::current(), "Completed CTS flow with no work", monitor.getStatsInfo());
    return api.setLastStatus(CTSStatus{.code = CTSStatusCode::kNoOp, .message = synthesis.no_op_reason, .diagnostics = {}});
  }
  if (!synthesis.success || synthesis.outcome != SynthesisOutcome::kFinished) {
    CTSLOG.warn(Loc::current(), "CTS flow failed during synthesis", monitor.getStatsInfo());
    return api.setLastStatus(CTSStatus{.code = CTSStatusCode::kFlowError, .message = "CTS synthesis failed.", .diagnostics = {}});
  }

  const auto optimization = Optimization::run();
  if (!optimization.success) {
    CTSLOG.warn(Loc::current(), "CTS flow failed during optimization", monitor.getStatsInfo());
    return api.setLastStatus(CTSStatus{.code = CTSStatusCode::kFlowError, .message = "CTS optimization failed.", .diagnostics = {}});
  }
  const auto instantiation = Instantiation::run();
  if (!instantiation.success) {
    CTSLOG.warn(Loc::current(), "CTS flow failed during instantiation", monitor.getStatsInfo());
    return api.setLastStatus(CTSStatus{.code = CTSStatusCode::kFlowError, .message = "CTS instantiation failed.", .diagnostics = {}});
  }
  const auto evaluation = Evaluation::run();
  if (!evaluation.summary.evaluation_ready) {
    CTSLOG.warn(Loc::current(), "CTS flow failed during evaluation", monitor.getStatsInfo());
    return api.setLastStatus(CTSStatus{.code = CTSStatusCode::kFlowError, .message = "CTS evaluation failed.", .diagnostics = {}});
  }

  const auto& qor = CTSDM.getEvaluationState().summary;
  EmitLogTable(Loc::current(), "CTS Key Results", {"Metric", "Value"},
               {{"Final Clock Buffers", ToLogTableCell(qor.final_clock_buffer_count)},
                {"Final Buffer Area (um^2)", qor.final_buffer_area_um2.has_value() ? ToLogTableCell(*qor.final_buffer_area_um2) : "unavailable"},
                {"Accepted Sizing Edits", ToLogTableCell(optimization.accepted_edit_count)},
                {"Maximum Clock Net Wirelength (um)", ToLogTableCell(qor.max_clock_net_wirelength_um)},
                {"Total Clock Network Wirelength (um)", ToLogTableCell(qor.total_clock_network_wirelength_um)},
                {"Path Buffer Minimum", ToLogTableCell(qor.clock_path_min_buffer)},
                {"Path Buffer Maximum", ToLogTableCell(qor.clock_path_max_buffer)},
                {"Maximum Clock Level", ToLogTableCell(qor.max_clock_network_level)}});
  CTSLOG.info(Loc::current(), "Completed CTS flow", monitor.getStatsInfo());
  return api.setLastStatus(buildOkStatus("CTS flow finished."));
}

auto CTSAPI::report(const std::string& save_dir) -> CTSStatus
{
  auto& api = getInst();
  if (!api._initialized) {
    return api.setLastStatus(CTSStatus{.code = CTSStatusCode::kNotInitialized, .message = "CTS is not initialized.", .diagnostics = {}});
  }
  const auto report = Output::run(save_dir);
  return api.setLastStatus(report.success ? buildOkStatus("CTS reports emitted.")
                                          : CTSStatus{.code = CTSStatusCode::kReportError, .message = "CTS report generation failed.", .diagnostics = {}});
}

auto CTSAPI::destroyCTS() -> CTSStatus
{
  auto& api = getInst();
  Logger::initInst();
  CTSLOG.info(Loc::current(), "Starting CTS destruction...");
  DataManager::destroyInst();
  api._initialized = false;
  const auto memory_stats = Utility::releaseMemory();
  const auto status = api.setLastStatus(buildOkStatus("CTS destruction completed."));
  if (memory_stats.supported) {
    CTSLOG.info(Loc::current(), "Completed CTS destruction; allocator release supported, RSS before=", Utility::formatFixed(memory_stats.rss_before_mb, 2),
                " MiB, RSS after=", Utility::formatFixed(memory_stats.rss_after_mb, 2), " MiB.");
  } else {
    CTSLOG.info(Loc::current(), "Completed CTS destruction; allocator release unsupported.");
  }
  Logger::destroyInst();
  return status;
}

auto CTSAPI::init(const std::string& config_file, const std::string& work_dir) -> CTSStatus
{
  (void) destroyCTS();
  auto& api = getInst();
  Logger::initInst();
  Monitor monitor;
  CTSLOG.info(Loc::current(), "Starting CTS initialization...");
  DataManager::initInst();
  const auto input_status = CTSDM.input(DataManagerInput{
      .config_file = config_file,
      .work_dir = work_dir,
  });
  auto status = buildInputStatus(input_status);
  if (!input_status.ok()) {
    CTSLOG.warn(Loc::current(), "CTS initialization failed: ", input_status.message, monitor.getStatsInfo());
    (void) destroyCTS();
    return api.setLastStatus(std::move(status));
  }
  api._initialized = true;
  CTSLOG.info(Loc::current(), "Completed CTS initialization", monitor.getStatsInfo());
  CTSLOG.printLogFilePath();
  return api.setLastStatus(std::move(status));
}

auto CTSAPI::lastStatus() -> CTSStatus
{
  return getInst()._last_status;
}

auto CTSAPI::outputSummary() -> ecc_feature::CTSSummary
{
  const auto& api = getInst();
  return api._initialized ? buildFeatureSummary(CTSDM.getEvaluationState().summary) : ecc_feature::CTSSummary{};
}

auto CTSAPI::outputClockTiming() -> std::vector<CTSTimingClock>
{
  std::vector<CTSTimingClock> clock_timing;
  if (!getInst()._initialized) {
    return clock_timing;
  }
  for (const auto& timing : CTSDM.getOptimizationSummary().clock_timing) {
    clock_timing.push_back(CTSTimingClock{
        .clock = timing.clock,
        .sink_count = timing.sink_count,
        .target_skew_ns = timing.target_skew_ns,
        .initial_skew_ns = timing.initial_skew_ns,
        .optimized_skew_ns = timing.optimized_skew_ns,
        .min_insertion_latency_ns = timing.min_insertion_latency_ns,
        .max_insertion_latency_ns = timing.max_insertion_latency_ns,
        .mean_insertion_latency_ns = timing.mean_insertion_latency_ns,
        .target_met = timing.target_met,
    });
  }
  return clock_timing;
}

}  // namespace icts
