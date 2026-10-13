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
 * @file CharBuildOrchestrator.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-20
 * @brief Characterization sweep build driver.
 */

#include "characterization/builder/CharBuildOrchestrator.hh"

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

#include "BufferingPattern.hh"
#include "LogTable.hh"
#include "Logger.hh"
#include "SegmentChar.hh"
#include "Utility.hh"
#include "characterization/builder/CharBuilderImpl.hh"
#include "characterization/pattern/CharPatternEnumerator.hh"

namespace icts::char_builder::detail {
namespace {

auto calcRatio(std::size_t numerator, std::size_t denominator) -> double
{
  if (denominator == 0U) {
    return 0.0;
  }
  return static_cast<double>(numerator) / static_cast<double>(denominator);
}

}  // namespace

auto CharBuildOrchestrator::build() -> void
{
  _impl._build_failure_reason.clear();
  _impl._evaluated_patterns = 0U;
  _impl._feasible_patterns = 0U;
  _impl._skipped_patterns_infeasible = 0U;
  _impl._wire_only_patterns = 0U;
  _impl._leaf_buffered_patterns = 0U;
  _impl._terminal_branch_patterns = 0U;
  _impl._mixed_master_patterns = 0U;
  _impl._skipped_load_points = 0U;
  _impl._executed_sta_samples = 0U;
  _impl._skipped_sta_samples = 0U;
  _impl._output_slew_overflow_samples = 0U;
  _impl._driven_cap_overflow_samples = 0U;
  _impl._driven_cap_overflow_load_points = 0U;
  _impl._max_observed_output_slew_ns = 0.0;
  _impl._max_observed_output_slew_idx = 0U;
  _impl._max_observed_driven_cap_pf = 0.0;
  _impl._max_observed_driven_cap_idx = 0U;

  if (_impl._sorted_buffers.empty()) {
    CTSLOG.warn(Loc::current(), "CharBuilder: no usable buffers remain after Input/Config/liberty filtering, skip characterization build");
    return;
  }
  if (_impl._wirelengths_um.empty()) {
    CTSLOG.warn(Loc::current(), "CharBuilder: no wirelengths to enumerate, aborting build");
    return;
  }
  if (_impl._slews_to_test.empty() || _impl._loads_to_test.empty()) {
    CTSLOG.warn(Loc::current(), "CharBuilder: characterization limits are unresolved", " (max_slew=", _impl._max_slew, " ns, max_cap=", _impl._max_cap,
                " pF), skip characterization build");
    return;
  }

  LogTableRows sweep_rows;
  sweep_rows.reserve(_impl._wirelengths_um.size());
  for (std::size_t wirelength_index = 0; wirelength_index < _impl._wirelengths_um.size(); ++wirelength_index) {
    const unsigned length_idx = _impl._wirelength_indices.at(wirelength_index);
    const double wirelength_um = _impl._wirelengths_um.at(wirelength_index);
    const std::size_t estimated_patterns_per_wirelength = _impl.patternEnumerator().estimatePatternCountPerWirelength(wirelength_um);
    const std::size_t estimated_sta_samples_per_wirelength = estimated_patterns_per_wirelength * _impl._loads_to_test.size() * _impl._slews_to_test.size();
    BuildProgress build_progress;
    build_progress.wirelength_um = wirelength_um;
    build_progress.estimated_patterns = estimated_patterns_per_wirelength;
    build_progress.estimated_sta_samples = estimated_sta_samples_per_wirelength;
    _impl.patternEnumerator().enumerateWirelength(length_idx, wirelength_um, build_progress);

    _impl._evaluated_patterns += build_progress.evaluated_patterns;
    _impl._feasible_patterns += build_progress.feasible_patterns;
    _impl._skipped_patterns_infeasible += build_progress.skipped_patterns_infeasible;
    _impl._wire_only_patterns += build_progress.wire_only_patterns;
    _impl._leaf_buffered_patterns += build_progress.leaf_buffered_patterns;
    _impl._terminal_branch_patterns += build_progress.terminal_branch_patterns;
    _impl._mixed_master_patterns += build_progress.mixed_master_patterns;
    _impl._skipped_load_points += build_progress.skipped_load_points;
    _impl._output_slew_overflow_samples += build_progress.output_slew_overflow_samples;
    _impl._executed_sta_samples += build_progress.executed_sta_samples;
    _impl._skipped_sta_samples += build_progress.skipped_sta_samples;
    _impl._driven_cap_overflow_samples += build_progress.driven_cap_overflow_samples;
    _impl._driven_cap_overflow_load_points += build_progress.driven_cap_overflow_load_points;
    _impl._max_observed_output_slew_ns = std::max(_impl._max_observed_output_slew_ns, build_progress.max_observed_output_slew_ns);
    _impl._max_observed_output_slew_idx = std::max(_impl._max_observed_output_slew_idx, build_progress.max_observed_output_slew_idx);
    _impl._max_observed_driven_cap_pf = std::max(_impl._max_observed_driven_cap_pf, build_progress.max_observed_driven_cap_pf);
    _impl._max_observed_driven_cap_idx = std::max(_impl._max_observed_driven_cap_idx, build_progress.max_observed_driven_cap_idx);
    sweep_rows.push_back({ToLogTableCell(length_idx), ToLogTableCell(wirelength_um), ToLogTableCell(build_progress.estimated_patterns),
                          ToLogTableCell(build_progress.evaluated_patterns), ToLogTableCell(build_progress.feasible_patterns),
                          ToLogTableCell(build_progress.wire_only_patterns), ToLogTableCell(build_progress.leaf_buffered_patterns),
                          ToLogTableCell(build_progress.terminal_branch_patterns), ToLogTableCell(build_progress.mixed_master_patterns),
                          ToLogTableCell(build_progress.skipped_patterns_infeasible), ToLogTableCell(build_progress.skipped_load_points),
                          ToLogTableCell(build_progress.executed_sta_samples), ToLogTableCell(build_progress.skipped_sta_samples),
                          ToLogTableCell(build_progress.output_slew_overflow_samples), ToLogTableCell(build_progress.driven_cap_overflow_samples)});
  }

  EmitLogTable(Loc::current(), "CharBuilder Wirelength Sweep",
               {"Length Idx", "Length (um)", "Estimated", "Evaluated", "Feasible", "Wire", "Leaf", "Branch", "Mixed", "Infeasible", "Load Skip", "STA Run",
                "STA Skip", "Slew Overflow", "Cap Overflow"},
               sweep_rows);

  const double output_slew_overflow_ratio = calcRatio(_impl._output_slew_overflow_samples, _impl._executed_sta_samples);
  if (_impl._output_slew_overflow_samples > 0U && output_slew_overflow_ratio >= 0.10) {
    CTSLOG.warn(Loc::current(), "CharBuilder: ", _impl._output_slew_overflow_samples, " samples exceeded the output-slew lattice (",
                Utility::getPercentage(_impl._output_slew_overflow_samples, _impl._executed_sta_samples, 2), ", max ",
                Utility::formatFixed(_impl._max_observed_output_slew_ns), " ns)");
  }
}

}  // namespace icts::char_builder::detail
