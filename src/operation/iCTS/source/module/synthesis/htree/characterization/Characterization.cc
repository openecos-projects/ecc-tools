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
 * @file Characterization.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief H-tree characterization grid setup and CharBuilder result capture.
 */

#include "synthesis/htree/characterization/Characterization.hh"

#include <cstdint>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "LogTable.hh"
#include "Logger.hh"
#include "characterization/Characterization.hh"
#include "synthesis/htree/HTree.hh"
#include "synthesis/htree/characterization/library/CharacterizationLibrary.hh"
#include "synthesis/htree/characterization/wirelength/WirelengthGrid.hh"
#include "synthesis/htree/diagnostic/HTreeDiagnostic.hh"

namespace icts {
class Tree;
}  // namespace icts

namespace icts::htree {

namespace {

auto AppendPositiveLengths(std::vector<double>& target, const std::vector<double>& values) -> void
{
  for (const double value : values) {
    if (value > 0.0) {
      target.push_back(value);
    }
  }
}

auto AppendUniqueLengthIndex(std::vector<unsigned>& target, unsigned length_idx) -> void
{
  if (length_idx == 0U) {
    return;
  }
  for (const unsigned existing_idx : target) {
    if (existing_idx == length_idx) {
      return;
    }
  }
  target.push_back(length_idx);
}

template <typename Value>
auto JoinValues(const std::vector<Value>& values) -> std::string
{
  std::ostringstream stream;
  for (std::size_t index = 0U; index < values.size(); ++index) {
    if (index > 0U) {
      stream << ", ";
    }
    stream << values[index];
  }
  return stream.str();
}

auto EmitCharacterizationResult(const HTree::Input& input, bool success, std::size_t segment_char_count, double length_step_um, std::string_view failure_reason)
    -> void
{
  EmitLogTable(Loc::current(), "CharBuilder Results", {"Metric", "Value"},
               {{"Clock", input.log_context.clock_name},
                {"Sink Domain", input.log_context.sink_domain},
                {"Success", ToLogTableCell(success)},
                {"Segment Characters", ToLogTableCell(segment_char_count)},
                {"Length Step (um)", ToLogTableCell(length_step_um)},
                {"Failure Reason", failure_reason.empty() ? "n/a" : std::string(failure_reason)}});
}

}  // namespace

auto RunCharacterizationFlow(const Tree& topology, int32_t dbu_per_um, const CharBuilder::Input& base_char_input, const CharBuilder::Config& base_char_config,
                             htree::DiagnosticBuild& result, CharacterizationLibrary& char_library, const HTree::Input& input, const HTree::Config& config)
    -> CharacterizationSummary
{
  EmitLogTable(Loc::current(), "HTree Build Scope", {"Property", "Value"},
               {{"Clock", input.log_context.clock_name},
                {"Net", input.log_context.clock_net_name},
                {"Sink Domain", input.log_context.sink_domain},
                {"Stage", input.log_context.stage},
                {"Object Prefix", input.object_name_prefix},
                {"Topology Nodes", ToLogTableCell(topology.get_size())},
                {"Topology Levels", ToLogTableCell(topology.levels().size())},
                {"DBU per um", ToLogTableCell(dbu_per_um)}});

  auto requested_lengths_um = CollectRequestedLevelLengthsUm(topology, dbu_per_um);
  std::vector<double> coverage_lengths_um;
  AppendPositiveLengths(coverage_lengths_um, input.additional_characterization_lengths_um);
  const auto char_grid_plan = ResolveCharacterizationGridPlan(base_char_config, requested_lengths_um, coverage_lengths_um);
  EmitLogTable(Loc::current(), "HTree Characterization Grid Plan", {"Property", "Value"},
               {{"Source", ToCharGridSourceName(char_grid_plan.source)},
                {"Configured Unit (um)", ToLogTableCell(char_grid_plan.configured_wirelength_unit_um)},
                {"Auto-derived Unit (um)", ToLogTableCell(char_grid_plan.auto_derived_wirelength_unit_um)},
                {"Selected Unit (um)", ToLogTableCell(char_grid_plan.wirelength_unit_um)},
                {"Configured Iterations", ToLogTableCell(char_grid_plan.configured_wirelength_iterations)},
                {"Required Iterations", ToLogTableCell(char_grid_plan.required_covering_iterations)},
                {"Selected Iterations", ToLogTableCell(char_grid_plan.wirelength_iterations)},
                {"Requested Lengths", ToLogTableCell(char_grid_plan.requested_level_lengths)},
                {"Unique Bins", ToLogTableCell(char_grid_plan.unique_level_bins)},
                {"Adapted", ToLogTableCell(char_grid_plan.adapted)},
                {"Configured Unit Missing", ToLogTableCell(char_grid_plan.configured_wirelength_missing)},
                {"Configured Grid Collapsed", ToLogTableCell(char_grid_plan.configured_grid_collapsed)}});
  std::vector<unsigned> direct_length_indices;
  if (char_grid_plan.adapted) {
    direct_length_indices = ResolveDirectCharacterizationLengthIndices(requested_lengths_um, char_grid_plan);
    if (config.enable_analytical_solver) {
      AppendUniqueLengthIndex(direct_length_indices, 1U);
    }
  }
  auto char_config = base_char_config;
  if (char_grid_plan.adapted) {
    char_config.wirelength_unit_um = char_grid_plan.wirelength_unit_um;
    char_config.wirelength_iterations = char_grid_plan.wirelength_iterations;
    if (!direct_length_indices.empty()) {
      char_config.wirelength_indices = std::move(direct_length_indices);
    }
  }
  const auto ensure_result = char_library.ensure(base_char_input, char_config);
  if (!ensure_result.success) {
    EmitCharacterizationResult(input, false, 0U, 0.0, ensure_result.failure_reason.empty() ? "characterization_library_failed" : ensure_result.failure_reason);
    return CharacterizationSummary{.success = false,
                                   .failure_reason = ensure_result.failure_reason.empty() ? "characterization_library_failed" : ensure_result.failure_reason,
                                   .length_step_um = 0.0};
  }

  const auto& char_builder = char_library.getCharBuilder();

  std::vector<std::string> buffer_masters;
  for (const auto& cell : char_builder.get_characterization_buffer_cells()) {
    buffer_masters.push_back(cell.cell_master);
  }
  const auto& route_rc = char_builder.get_clock_route_segment_rc();
  EmitLogTable(Loc::current(), "CharBuilder Setup", {"Property", "Value"},
               {{"Wirelength Points", ToLogTableCell(char_builder.get_wirelengths_um().size())},
                {"Wirelength Values (um)", JoinValues(char_builder.get_wirelengths_um())},
                {"Wirelength Unit Source", char_builder.get_wirelength_unit_source()},
                {"Wirelength Iterations", ToLogTableCell(char_builder.get_wirelength_iterations())},
                {"Routing Layer", ToLogTableCell(char_builder.get_routing_layer())},
                {"DBU per um", ToLogTableCell(route_rc.dbu_per_um)},
                {"Resistance (ohm/um)", ToLogTableCell(route_rc.resistance_per_um_ohm)},
                {"Capacitance (pF/um)", ToLogTableCell(route_rc.capacitance_per_um_pf)},
                {"Buffer Masters", JoinValues(buffer_masters)}});

  const double length_step_um = char_builder.get_wirelength_unit_um();
  if (length_step_um <= 0.0 || char_builder.get_segment_chars().empty()) {
    const auto failure_reason
        = char_builder.get_build_failure_reason().empty() ? std::string{"no_usable_segment_chars"} : char_builder.get_build_failure_reason();
    CTSLOG.warn(Loc::current(), "HTree: characterization failed: ", failure_reason, ".");
    EmitCharacterizationResult(input, false, char_builder.get_segment_chars().size(), length_step_um, failure_reason);
    return CharacterizationSummary{.success = false, .failure_reason = failure_reason, .length_step_um = length_step_um};
  }

  result.diagnostics.char_wirelength_unit_um = length_step_um;
  result.diagnostics.char_wirelength_iterations = char_builder.get_wirelength_iterations();
  result.diagnostics.char_unique_level_bins = char_grid_plan.adapted
                                                  ? char_grid_plan.unique_level_bins
                                                  : CountUniqueAlignedLengthBins(CollectRequestedLevelLengthsUm(topology, dbu_per_um), length_step_um);
  result.diagnostics.char_grid_adapted = char_grid_plan.adapted;
  result.diagnostics.char_max_slew_ns = char_builder.get_max_slew();
  result.diagnostics.char_max_cap_pf = char_builder.get_max_cap();
  result.diagnostics.char_slew_steps = char_builder.get_slew_steps();
  result.diagnostics.char_cap_steps = char_builder.get_cap_steps();
  EmitLogTable(Loc::current(), "CharBuilder Results", {"Metric", "Value"},
               {{"Clock", input.log_context.clock_name},
                {"Sink Domain", input.log_context.sink_domain},
                {"Success", "true"},
                {"Segment Characters", ToLogTableCell(char_builder.get_segment_chars().size())},
                {"Length Step (um)", ToLogTableCell(length_step_um)},
                {"Wirelength Iterations", ToLogTableCell(char_builder.get_wirelength_iterations())},
                {"Maximum Slew (ns)", ToLogTableCell(char_builder.get_max_slew())},
                {"Maximum Capacitance (pF)", ToLogTableCell(char_builder.get_max_cap())},
                {"Slew Steps", ToLogTableCell(char_builder.get_slew_steps())},
                {"Capacitance Steps", ToLogTableCell(char_builder.get_cap_steps())},
                {"Executed STA Samples", ToLogTableCell(char_builder.get_executed_sta_samples())},
                {"Skipped STA Samples", ToLogTableCell(char_builder.get_skipped_sta_samples())},
                {"Output Slew Overflow Samples", ToLogTableCell(char_builder.get_output_slew_overflow_samples())},
                {"Driven Cap Overflow Samples", ToLogTableCell(char_builder.get_driven_cap_overflow_samples())},
                {"Driven Cap Overflow Load Points", ToLogTableCell(char_builder.get_driven_cap_overflow_load_points())},
                {"Maximum Observed Output Slew (ns)", ToLogTableCell(char_builder.get_max_observed_output_slew_ns())},
                {"Maximum Observed Driven Cap (pF)", ToLogTableCell(char_builder.get_max_observed_driven_cap_pf())},
                {"Failure Reason", "n/a"}});
  return CharacterizationSummary{.success = true, .failure_reason = {}, .length_step_um = length_step_um};
}

}  // namespace icts::htree
