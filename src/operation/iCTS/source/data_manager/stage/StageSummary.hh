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
 * @file StageSummary.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-07-30
 * @brief Defines shared summaries and committed evaluation state for CTS stages.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "qor/QOR.hh"

namespace icts {

enum class SynthesisOutcome
{
  kFinished,
  kFailed,
  kNoOp
};

struct SynthesisTraceStatusRecord
{
  std::string clock_name;
  std::string clock_net_name;
  std::string status;
  std::string sink_domain;
  std::size_t valid_sink_count = 0U;
  std::size_t sink_domain_sink_count = 0U;
  std::string detail;
};

struct SynthesisTraceSummary
{
  bool success = true;
  SynthesisOutcome outcome = SynthesisOutcome::kFinished;
  std::string no_op_reason;
  std::string failure_reason;
  std::string commit_status = "not_attempted";
  std::size_t total_clocks = 0U;
  std::size_t successful_clocks = 0U;
  std::size_t skipped_clocks = 0U;
  std::size_t failed_clocks = 0U;
  std::size_t total_sink_domains = 0U;
  std::size_t hard_macro_sinks = 0U;
  std::size_t regular_sinks = 0U;
  std::size_t selected_htree_level_count = 0U;
  unsigned selected_htree_depth = 0U;
  std::size_t inserted_inst_count = 0U;
  std::size_t inserted_net_count = 0U;
  std::size_t htree_inserted_buffer_count = 0U;
  std::size_t htree_inserted_net_count = 0U;
  std::vector<SynthesisTraceStatusRecord> domain_status;
};

struct ClockTimingSummary
{
  std::string clock;
  std::size_t sink_count = 0U;
  double target_skew_ns = 0.0;
  double initial_skew_ns = 0.0;
  double optimized_skew_ns = 0.0;
  double min_insertion_latency_ns = 0.0;
  double max_insertion_latency_ns = 0.0;
  double mean_insertion_latency_ns = 0.0;
  bool target_met = false;
};

struct OptimizationSummary
{
  bool success = true;
  bool optimized = false;
  std::size_t clock_count = 0U;
  std::size_t optimized_clock_count = 0U;
  std::size_t accepted_edit_count = 0U;
  std::string status = "no_op";
  std::string reason = "n/a";
  std::vector<ClockTimingSummary> clock_timing;
};

struct InstantiationSummary
{
  bool design_ready = false;
  bool success = false;
  std::size_t clock_count = 0U;
  std::size_t inserted_inst_count = 0U;
  std::size_t inserted_net_count = 0U;
  std::string failure_reason = "n/a";
};

struct QorSummary
{
  bool has_evaluation_result = false;
  std::string qor_metric_status = "unavailable";
  std::string physical_metric_source = "unavailable";
  std::string path_depth_metric_status = "unavailable";
  int32_t final_clock_buffer_count = 0;
  std::optional<double> final_buffer_area_um2 = std::nullopt;
  int32_t clock_member_buffer_count = 0;
  double max_clock_net_wirelength_um = 0.0;
  double total_clock_network_wirelength_um = 0.0;
  int32_t max_clock_net_wirelength_dbu = 0;
  double total_clock_network_wirelength_dbu = 0.0;
  std::optional<int32_t> design_dbu_per_um = std::nullopt;
  int32_t clock_path_min_buffer = 0;
  int32_t clock_path_max_buffer = 0;
  int32_t max_clock_network_level = 0;
};

struct EvaluationState
{
  QorSummary summary;
  Qor statistics;
};

}  // namespace icts
