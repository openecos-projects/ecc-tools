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
 * @file FastSTALiberty.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-18
 * @brief Fast STA Liberty timing and power record extraction implementation.
 */

#include "FastSTALiberty.hh"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <optional>
#include <ranges>
#include <utility>
#include <vector>

#include "Type.hh"
#include "io/Wrapper.hh"
#include "liberty/Lib.hh"

namespace icts {
namespace {

constexpr double kMilliwattToWatt = 1.0 / 1000.0;

auto convertLibCapToPf(idb::LibCell* lib_cell, double cap_value) -> double
{
  auto* owner_lib = lib_cell != nullptr ? lib_cell->get_owner_lib() : nullptr;
  if (owner_lib == nullptr) {
    return cap_value;
  }
  return idb::ConvertCapUnit(owner_lib->get_cap_unit(), idb::CapacitiveUnit::kPF, cap_value);
}

auto convertLibTimeToNs(idb::LibCell* lib_cell, double time_value) -> double
{
  auto* owner_lib = lib_cell != nullptr ? lib_cell->get_owner_lib() : nullptr;
  if (owner_lib == nullptr) {
    return time_value;
  }
  return owner_lib->convert_time_unit_to_ns(time_value);
}

auto queryLibPortCapacitancePf(idb::LibCell* lib_cell, idb::LibPort* lib_port) -> std::optional<double>
{
  if (lib_cell == nullptr || lib_port == nullptr || lib_port->isInput() == 0U) {
    return std::nullopt;
  }

  std::optional<double> cap_value = std::nullopt;
  const auto consider_cap = [&cap_value](std::optional<double> candidate) -> void {
    if (!candidate.has_value() || !std::isfinite(*candidate) || *candidate < 0.0) {
      return;
    }
    cap_value = cap_value.has_value() ? std::optional<double>{std::max(*cap_value, *candidate)} : candidate;
  };
  if (lib_port->has_port_cap()) {
    consider_cap(lib_port->get_port_cap());
  }
  consider_cap(lib_port->get_port_cap(idb::AnalysisMode::kMax, idb::TransType::kRise));
  consider_cap(lib_port->get_port_cap(idb::AnalysisMode::kMax, idb::TransType::kFall));
  consider_cap(lib_port->get_port_cap(idb::AnalysisMode::kMin, idb::TransType::kRise));
  consider_cap(lib_port->get_port_cap(idb::AnalysisMode::kMin, idb::TransType::kFall));
  if (!cap_value.has_value()) {
    return std::nullopt;
  }
  const double cap_pf = *cap_value;
  return std::isfinite(cap_pf) && cap_pf >= 0.0 ? std::optional<double>{cap_pf} : std::nullopt;
}

auto queryLibPortCapacitanceProfilePf(idb::LibPort* lib_port) -> std::optional<std::array<std::array<double, 2U>, 2U>>
{
  if (lib_port == nullptr) {
    return std::nullopt;
  }
  std::array<std::array<double, 2U>, 2U> profile{};
  for (const auto [analysis_index, analysis] : {std::pair{0U, idb::AnalysisMode::kMin}, std::pair{1U, idb::AnalysisMode::kMax}}) {
    for (const auto [transition_index, transition] : {std::pair{0U, idb::TransType::kRise}, std::pair{1U, idb::TransType::kFall}}) {
      auto value = lib_port->get_port_cap(analysis, transition);
      if (!value.has_value() && lib_port->has_port_cap()) {
        value = lib_port->get_port_cap();
      }
      if (!value.has_value() || !std::isfinite(*value) || *value < 0.0) {
        return std::nullopt;
      }
      profile.at(analysis_index).at(transition_index) = *value;
    }
  }
  return profile;
}

auto matchingArcSets(idb::LibCell* lib_cell, const std::string& input_port_name, const std::string& output_port_name,
                     std::optional<idb::LibArc::TimingType> timing_type) -> std::vector<idb::LibArcSet*>
{
  std::vector<idb::LibArcSet*> result;
  if (lib_cell == nullptr || input_port_name.empty() || output_port_name.empty()) {
    return result;
  }
  for (const auto& arc_set_holder : lib_cell->get_cell_arcs()) {
    auto* arc_set = arc_set_holder.get();
    if (arc_set == nullptr || arc_set->get_arcs().empty()) {
      continue;
    }
    auto* representative = arc_set->front();
    if (representative == nullptr || input_port_name != representative->get_src_port() || output_port_name != representative->get_snk_port()) {
      continue;
    }
    const auto type = representative->get_timing_type();
    const bool type_matches = timing_type.has_value() ? type == *timing_type
                                                      : type == idb::LibArc::TimingType::kDefault || type == idb::LibArc::TimingType::kComb
                                                            || type == idb::LibArc::TimingType::kCombRise || type == idb::LibArc::TimingType::kCombFall;
    if (type_matches) {
      result.push_back(arc_set);
    }
  }
  return result;
}

auto toFastStaAxisKind(idb::LibLutTableTemplate::Variable variable) -> FastStaLibertyAxisKind
{
  switch (variable) {
    case idb::LibLutTableTemplate::Variable::INPUT_NET_TRANSITION:
    case idb::LibLutTableTemplate::Variable::RELATED_PIN_TRANSITION:
    case idb::LibLutTableTemplate::Variable::INPUT_TRANSITION_TIME:
      return FastStaLibertyAxisKind::kInputSlew;
    case idb::LibLutTableTemplate::Variable::TOTAL_OUTPUT_NET_CAPACITANCE:
    case idb::LibLutTableTemplate::Variable::EQUAL_OR_OPPOSITE_OUTPUT_NET_CAPACITANCE:
      return FastStaLibertyAxisKind::kOutputLoad;
    default:
      return FastStaLibertyAxisKind::kUnknown;
  }
}

auto convertTableAxisValue(idb::LibCell* lib_cell, idb::LibLutTableTemplate::Variable variable, double value) -> double
{
  auto* owner_lib = lib_cell != nullptr ? lib_cell->get_owner_lib() : nullptr;
  if (owner_lib == nullptr) {
    return value;
  }

  switch (toFastStaAxisKind(variable)) {
    case FastStaLibertyAxisKind::kInputSlew:
      return owner_lib->convert_time_unit_to_ns(value);
    case FastStaLibertyAxisKind::kOutputLoad:
      return convertLibCapToPf(lib_cell, value);
    case FastStaLibertyAxisKind::kUnknown:
      return value;
  }
  return value;
}

auto convertTableValue(idb::LibCell* lib_cell, FastStaLibertyTableKind kind, double value) -> double
{
  switch (kind) {
    case FastStaLibertyTableKind::kCellDelay:
    case FastStaLibertyTableKind::kOutputSlew:
      return convertLibTimeToNs(lib_cell, value);
    case FastStaLibertyTableKind::kInternalPower:
      return lib_cell != nullptr ? lib_cell->convertInternalPowerTableToMwNs(value) : value;
  }
  return value;
}

auto appendAxis(idb::LibCell* lib_cell, idb::LibTable* table, std::optional<idb::LibLutTableTemplate::Variable> variable, unsigned axis_index,
                FastStaLibertyTable& table_record) -> void
{
  if (!variable.has_value() || axis_index >= table->get_axes().size()) {
    return;
  }

  FastStaLibertyAxis axis;
  axis.kind = toFastStaAxisKind(*variable);
  auto& source_axis_values = table->getAxis(axis_index).get_axis_values();
  axis.values.reserve(source_axis_values.size());
  for (const auto& value : source_axis_values) {
    if (value == nullptr) {
      continue;
    }
    axis.values.push_back(convertTableAxisValue(lib_cell, *variable, value->getFloatValue()));
  }
  table_record.axes.push_back(std::move(axis));
}

auto extractTable(idb::LibCell* lib_cell, idb::LibTable* table, FastStaLibertyTableKind kind, FastStaTransition transition) -> FastStaLibertyTable
{
  FastStaLibertyTable table_record;
  table_record.kind = kind;
  table_record.transition = transition;
  if (table == nullptr) {
    return table_record;
  }

  auto* table_template = table->get_table_template();
  if (table_template != nullptr) {
    appendAxis(lib_cell, table, table_template->get_template_variable1(), 0U, table_record);
    appendAxis(lib_cell, table, table_template->get_template_variable2(), 1U, table_record);
  }

  const auto& source_values = table->get_table_values();
  table_record.values.reserve(source_values.size());
  for (const auto& value : source_values) {
    if (value == nullptr) {
      continue;
    }
    table_record.values.push_back(convertTableValue(lib_cell, kind, value->getFloatValue()));
  }
  return table_record;
}

auto appendDelayTable(idb::LibCell* lib_cell, idb::LibDelayTableModel* delay_model, idb::LibTable::TableType table_type, FastStaLibertyTableKind kind,
                      FastStaTransition transition, std::vector<FastStaLibertyTable>& tables) -> void
{
  auto* table = delay_model != nullptr ? delay_model->getTable(static_cast<int>(table_type)) : nullptr;
  if (table == nullptr) {
    return;
  }
  auto table_record = extractTable(lib_cell, table, kind, transition);
  if (table_record.valid()) {
    tables.push_back(std::move(table_record));
  }
}

auto appendPowerTable(idb::LibCell* lib_cell, idb::LibPowerTableModel* power_model, idb::LibTable::TableType table_type, FastStaTransition transition,
                      std::vector<FastStaLibertyTable>& tables) -> void
{
  auto* table = power_model != nullptr ? power_model->getTable(CAST_POWER_TYPE_TO_INDEX(table_type)) : nullptr;
  if (table == nullptr) {
    return;
  }
  auto table_record = extractTable(lib_cell, table, FastStaLibertyTableKind::kInternalPower, transition);
  if (table_record.valid()) {
    tables.push_back(std::move(table_record));
  }
}

auto calcLeakagePowerW(idb::LibCell* lib_cell) -> std::optional<double>
{
  if (lib_cell == nullptr) {
    return std::nullopt;
  }

  const double cell_leakage_mw = lib_cell->get_cell_leakage_power();
  if (lib_cell->has_cell_leakage_power()) {
    return std::isfinite(cell_leakage_mw) && cell_leakage_mw >= 0.0 ? std::optional<double>{cell_leakage_mw * kMilliwattToWatt} : std::nullopt;
  }

  double leakage_mw = 0.0;
  bool has_unconditional_leakage = false;
  for (auto* leakage_power : lib_cell->getLeakagePowerList()) {
    if (leakage_power != nullptr && leakage_power->get_when().empty() && std::isfinite(leakage_power->get_value())) {
      leakage_mw += leakage_power->get_value();
      has_unconditional_leakage = true;
    }
  }
  const double leakage_power_w = leakage_mw * kMilliwattToWatt;
  return has_unconditional_leakage && std::isfinite(leakage_power_w) && leakage_power_w >= 0.0 ? std::optional<double>{leakage_power_w} : std::nullopt;
}

auto percentOrDefault(double value, double default_value) -> double
{
  return value > 0.0 && value < 1.0 ? value : default_value;
}

auto findBestTimingArc(idb::LibArcSet* arc_set) -> idb::LibArc*
{
  if (arc_set == nullptr) {
    return nullptr;
  }
  idb::LibArc* first_enabled = nullptr;
  for (const auto& arc_holder : arc_set->get_arcs()) {
    auto* arc = arc_holder.get();
    if (arc == nullptr || arc->isDisableArc() != 0U) {
      continue;
    }
    if (first_enabled == nullptr) {
      first_enabled = arc;
    }
    if (arc->get_when().empty()) {
      return arc;
    }
  }
  return first_enabled;
}

auto appendPowerArcTables(idb::LibCell* lib_cell, idb::LibPowerArcSet* power_arc_set, FastStaLibertyArc& arc_record) -> void
{
  if (power_arc_set == nullptr) {
    return;
  }
  for (const auto& power_arc_holder : power_arc_set->get_power_arcs()) {
    auto* power_arc = power_arc_holder.get();
    auto* internal_power = power_arc != nullptr && power_arc->get_internal_power_info() != nullptr ? power_arc->get_internal_power_info().get() : nullptr;
    auto* power_model = internal_power != nullptr ? dynamic_cast<idb::LibPowerTableModel*>(internal_power->get_power_table_model()) : nullptr;
    if (power_model == nullptr) {
      continue;
    }
    appendPowerTable(lib_cell, power_model, idb::LibTable::TableType::kRisePower, FastStaTransition::kRise, arc_record.internal_power_tables);
    appendPowerTable(lib_cell, power_model, idb::LibTable::TableType::kFallPower, FastStaTransition::kFall, arc_record.internal_power_tables);
  }
}

auto extractCellArcFromLibCell(Wrapper& wrapper, idb::LibCell* lib_cell, idb::LibPort* input_override = nullptr, idb::LibPort* output_override = nullptr,
                               std::optional<idb::LibArc::TimingType> timing_type = std::nullopt) -> std::optional<FastStaLibertyCell>
{
  if (lib_cell == nullptr) {
    return std::nullopt;
  }

  idb::LibPort* input_port = nullptr;
  idb::LibPort* output_port = nullptr;
  if (input_override != nullptr && output_override != nullptr) {
    input_port = input_override;
    output_port = output_override;
  } else {
    lib_cell->bufferPorts(input_port, output_port);
  }
  if (input_port == nullptr || output_port == nullptr) {
    return std::nullopt;
  }
  const auto cell_master = std::string(lib_cell->get_cell_name());
  const auto input_port_name = std::string(input_port->get_port_name());
  const auto output_port_name = std::string(output_port->get_port_name());
  auto* owner_lib = lib_cell->get_owner_lib();
  if (cell_master.empty() || input_port_name.empty() || output_port_name.empty() || owner_lib == nullptr || !owner_lib->has_time_unit()
      || !owner_lib->has_cap_unit()) {
    return std::nullopt;
  }

  std::optional<double> output_cap_limit_pf = std::nullopt;
  if (auto cap_limit = output_port->get_port_cap_limit(idb::AnalysisMode::kMax); cap_limit.has_value() && std::isfinite(*cap_limit) && *cap_limit > 0.0) {
    const double cap_limit_pf = *cap_limit;
    if (std::isfinite(cap_limit_pf) && cap_limit_pf > 0.0) {
      output_cap_limit_pf = cap_limit_pf;
    }
  }

  std::optional<double> input_slew_limit_ns = std::nullopt;
  if (auto slew_limit = input_port->get_port_slew_limit(idb::AnalysisMode::kMax); slew_limit.has_value() && std::isfinite(*slew_limit) && *slew_limit > 0.0) {
    const double slew_limit_ns = *slew_limit;
    if (std::isfinite(slew_limit_ns) && slew_limit_ns > 0.0) {
      input_slew_limit_ns = slew_limit_ns;
    }
  }
  if (!input_slew_limit_ns.has_value()) {
    input_slew_limit_ns = owner_lib->get_default_max_transition();
  }
  auto output_slew_limit_ns = output_port->get_port_slew_limit(idb::AnalysisMode::kMax);
  if (!output_slew_limit_ns.has_value() || !std::isfinite(*output_slew_limit_ns) || *output_slew_limit_ns <= 0.0) {
    output_slew_limit_ns = owner_lib->get_default_max_transition();
  }
  const auto input_cap_pf = queryLibPortCapacitancePf(lib_cell, input_port);
  const auto input_cap_profile = queryLibPortCapacitanceProfilePf(input_port);
  if (!input_cap_profile.has_value()) {
    return std::nullopt;
  }
  const auto area_um2 = wrapper.queryCellAreaUm2(cell_master);
  if (input_override == nullptr && (!input_cap_pf.has_value() || !area_um2.has_value())) {
    return std::nullopt;
  }

  FastStaLibertyCell cell{
      .library_name = owner_lib->get_lib_name(),
      .cell_master = cell_master,
      .input_port = input_port_name,
      .output_port = output_port_name,
      .input_cap_pf = input_cap_pf.value_or(0.0),
      .input_cap_pf_by_timing = *input_cap_profile,
      .output_cap_limit_pf = output_cap_limit_pf.value_or(0.0),
      .input_slew_limit_ns = input_slew_limit_ns.value_or(0.0),
      .output_slew_limit_ns = output_slew_limit_ns.value_or(0.0),
      .input_threshold_rise = owner_lib != nullptr ? percentOrDefault(owner_lib->get_input_threshold_pct_rise(), 0.5) : 0.5,
      .input_threshold_fall = owner_lib != nullptr ? percentOrDefault(owner_lib->get_input_threshold_pct_fall(), 0.5) : 0.5,
      .output_threshold_rise = owner_lib != nullptr ? percentOrDefault(owner_lib->get_output_threshold_pct_rise(), 0.5) : 0.5,
      .output_threshold_fall = owner_lib != nullptr ? percentOrDefault(owner_lib->get_output_threshold_pct_fall(), 0.5) : 0.5,
      .slew_lower_threshold_rise = owner_lib != nullptr ? percentOrDefault(owner_lib->get_slew_lower_threshold_pct_rise(), 0.3) : 0.3,
      .slew_lower_threshold_fall = owner_lib != nullptr ? percentOrDefault(owner_lib->get_slew_lower_threshold_pct_fall(), 0.3) : 0.3,
      .slew_upper_threshold_rise = owner_lib != nullptr ? percentOrDefault(owner_lib->get_slew_upper_threshold_pct_rise(), 0.7) : 0.7,
      .slew_upper_threshold_fall = owner_lib != nullptr ? percentOrDefault(owner_lib->get_slew_upper_threshold_pct_fall(), 0.7) : 0.7,
      .slew_derate_from_library = owner_lib != nullptr && owner_lib->get_slew_derate_from_library() > 0.0
                                      ? owner_lib->get_slew_derate_from_library()
                                      : 1.0,
      .area_um2 = area_um2.value_or(0.0),
      .voltage_v = owner_lib != nullptr ? owner_lib->get_nom_voltage() : 0.0,
      .leakage_power_w = calcLeakagePowerW(lib_cell),
      .timing_arc = FastStaLibertyArc{
          .library_name = owner_lib->get_lib_name(),
          .from_port = input_port_name,
          .to_port = output_port_name,
          .edge_triggered = timing_type == idb::LibArc::TimingType::kRisingEdge || timing_type == idb::LibArc::TimingType::kFallingEdge,
          .trigger_transition = timing_type == idb::LibArc::TimingType::kFallingEdge ? FastStaTransition::kFall : FastStaTransition::kRise,
          .delay_tables = {},
          .slew_tables = {},
          .internal_power_tables = {},
      },
      .timing_arcs = {},
      .input_cap_profile_available = true,
  };

  const auto timing_arc_sets = matchingArcSets(lib_cell, input_port_name, output_port_name, timing_type);
  idb::LibArc* preferred_arc = nullptr;
  std::size_t variant_index = 0U;
  for (auto* timing_arc_set : timing_arc_sets) {
    if (preferred_arc == nullptr) {
      preferred_arc = findBestTimingArc(timing_arc_set);
    }
    const auto& arc_holders = timing_arc_set->get_arcs();
    for (const auto& arc_holder : arc_holders) {
      auto* timing_arc = arc_holder.get();
      if (timing_arc == nullptr || timing_arc->isDisableArc() != 0U) {
        continue;
      }
      auto* delay_model = dynamic_cast<idb::LibDelayTableModel*>(timing_arc->get_table_model());
      if (delay_model == nullptr) {
        return std::nullopt;
      }
      auto arc_record = cell.timing_arc;
      arc_record.delay_tables.clear();
      arc_record.slew_tables.clear();
      arc_record.internal_power_tables.clear();
      arc_record.variant_index = variant_index;
      arc_record.positive_unate = timing_arc->isPositiveArc() != 0U;
      arc_record.negative_unate = timing_arc->isNegativeArc() != 0U;
      arc_record.conditional = !timing_arc->get_when().empty();
      arc_record.when = timing_arc->get_when();
      appendDelayTable(lib_cell, delay_model, idb::LibTable::TableType::kCellRise, FastStaLibertyTableKind::kCellDelay, FastStaTransition::kRise,
                       arc_record.delay_tables);
      appendDelayTable(lib_cell, delay_model, idb::LibTable::TableType::kCellFall, FastStaLibertyTableKind::kCellDelay, FastStaTransition::kFall,
                       arc_record.delay_tables);
      appendDelayTable(lib_cell, delay_model, idb::LibTable::TableType::kRiseTransition, FastStaLibertyTableKind::kOutputSlew, FastStaTransition::kRise,
                       arc_record.slew_tables);
      appendDelayTable(lib_cell, delay_model, idb::LibTable::TableType::kFallTransition, FastStaLibertyTableKind::kOutputSlew, FastStaTransition::kFall,
                       arc_record.slew_tables);
      if (!arc_record.delay_tables.empty() && !arc_record.slew_tables.empty()) {
        cell.timing_arcs.push_back(std::move(arc_record));
        if (timing_arc == preferred_arc) {
          cell.timing_arc = cell.timing_arcs.back();
        }
      } else {
        // A missing model is unavailable input, not an inapplicable conditional
        // alternative that can silently be discarded from the extrema.
        return std::nullopt;
      }
      ++variant_index;
    }
  }
  if (cell.timing_arcs.empty()) {
    return std::nullopt;
  }
  if (cell.timing_arc.delay_tables.empty() || cell.timing_arc.slew_tables.empty()) {
    cell.timing_arc = cell.timing_arcs.front();
  }

  if (lib_cell->isICG()) {
    FastStaClockGateModel gate;
    gate.clock_port = input_port_name;
    gate.output_port = output_port_name;
    gate.output_expression = output_port->get_func_expr_str();
    gate.latch_based = lib_cell->get_clock_gating_integrated_cell().find("latch") != std::string::npos;
    for (const auto& port : lib_cell->get_cell_ports()) {
      if (port->get_clock_gate_enable_pin()) {
        gate.enable_ports.emplace_back(port->get_port_name());
      }
      if (port->get_clock_gate_test_pin()) {
        gate.test_ports.emplace_back(port->get_port_name());
      }
    }
    if (lib_cell->get_latches().size() == 1U) {
      const auto& latch = lib_cell->get_latches().front();
      gate.state = latch.state;
      gate.inverted_state = latch.inverted_state;
      gate.data_expression = latch.data_in;
      gate.latch_enable_expression = latch.enable;
      gate.latch_based = true;
    }
    cell.clock_gate = std::move(gate);
  }

  auto power_arc_set = lib_cell->findLibertyPowerArcSet(input_port_name.c_str(), output_port_name.c_str());
  appendPowerArcTables(lib_cell, power_arc_set.value_or(nullptr), cell.timing_arc);
  return cell;
}

}  // namespace

auto FastStaLiberty::extractBufferCell(Wrapper& wrapper, const std::string& cell_master) -> std::optional<FastStaLibertyCell>
{
  auto* lib_cell = wrapper.findLibertyCell(cell_master);
  if (lib_cell != nullptr) {
    return extractCellArcFromLibCell(wrapper, lib_cell);
  }
  return std::nullopt;
}

auto FastStaLiberty::extractCellArc(Wrapper& wrapper, const std::string& cell_master, const std::string& input_port, const std::string& output_port)
    -> std::optional<FastStaLibertyCell>
{
  auto* lib_cell = wrapper.findLibertyCell(cell_master);
  if (lib_cell == nullptr) {
    return std::nullopt;
  }
  auto* input = lib_cell->get_cell_port_or_port_bus(input_port.c_str());
  auto* output = lib_cell->get_cell_port_or_port_bus(output_port.c_str());
  if (input == nullptr || output == nullptr || input->isInput() == 0U || output->isOutput() == 0U) {
    return std::nullopt;
  }
  return extractCellArcFromLibCell(wrapper, lib_cell, input, output);
}

auto FastStaLiberty::extractLaunchArc(Wrapper& wrapper, const std::string& cell_master, const std::string& clock_port, const std::string& output_port,
                                      FastStaTransition clock_transition) -> std::optional<FastStaLibertyCell>
{
  auto* lib_cell = wrapper.findLibertyCell(cell_master);
  if (lib_cell == nullptr) {
    return std::nullopt;
  }
  auto* input = lib_cell->get_cell_port_or_port_bus(clock_port.c_str());
  auto* output = lib_cell->get_cell_port_or_port_bus(output_port.c_str());
  if (input == nullptr || output == nullptr || input->isInput() == 0U || output->isOutput() == 0U) {
    return std::nullopt;
  }
  const auto timing_type = clock_transition == FastStaTransition::kRise ? idb::LibArc::TimingType::kRisingEdge : idb::LibArc::TimingType::kFallingEdge;
  return extractCellArcFromLibCell(wrapper, lib_cell, input, output, timing_type);
}

auto FastStaLiberty::queryTimingCheck(Wrapper& wrapper, const std::string& cell_master, const std::string& clock_port, const std::string& data_port,
                                      FastStaTimingCheckKind kind, FastStaTransition clock_transition, FastStaTransition data_transition, double clock_slew_ns,
                                      double data_slew_ns, std::size_t& conditional_fallback_count, std::size_t& conditional_extrema_count,
                                      const FastStaCondition::PinValueLookup& case_lookup) -> FastStaCheckValue
{
  auto* lib_cell = wrapper.findLibertyCell(cell_master);
  if (lib_cell == nullptr || !std::isfinite(clock_slew_ns) || clock_slew_ns < 0.0 || !std::isfinite(data_slew_ns) || data_slew_ns < 0.0) {
    return {};
  }
  idb::LibArc::TimingType timing_type = idb::LibArc::TimingType::kSetupRising;
  if (kind == FastStaTimingCheckKind::kSetup) {
    timing_type = clock_transition == FastStaTransition::kRise ? idb::LibArc::TimingType::kSetupRising : idb::LibArc::TimingType::kSetupFalling;
  } else {
    timing_type = clock_transition == FastStaTransition::kRise ? idb::LibArc::TimingType::kHoldRising : idb::LibArc::TimingType::kHoldFalling;
  }
  const auto arc_set = lib_cell->findLibertyArcSet(clock_port.c_str(), data_port.c_str(), timing_type);
  if (!arc_set.has_value() || *arc_set == nullptr || lib_cell->get_owner_lib() == nullptr || !lib_cell->get_owner_lib()->has_time_unit()) {
    return {};
  }
  double data_slew_in_lib_units = data_slew_ns;
  switch (lib_cell->get_owner_lib()->get_time_unit()) {
    case idb::TimeUnit::kPS:
      data_slew_in_lib_units *= 1.0e3;
      break;
    case idb::TimeUnit::kFS:
      data_slew_in_lib_units *= 1.0e6;
      break;
    default:
      break;
  }
  const auto trans_type = data_transition == FastStaTransition::kRise ? idb::TransType::kRise : idb::TransType::kFall;
  std::vector<idb::LibArc*> candidate_arcs;
  for (const auto& arc_holder : (*arc_set)->get_arcs()) {
    auto* arc = arc_holder.get();
    if (arc == nullptr || arc->isDisableArc() != 0U) {
      continue;
    }
    const auto condition = FastStaCondition::evaluate(arc->get_when(), case_lookup);
    if (condition == FastStaLogicValue::kInvalid) {
      return {.status = FastStaCheckStatus::kInvalidCondition};
    }
    if (condition == FastStaLogicValue::kZero) {
      continue;
    }
    if (arc->get_table_model() == nullptr) {
      return {.status = FastStaCheckStatus::kUnavailable};
    }
    candidate_arcs.push_back(arc);
  }
  const auto has_conditional = std::ranges::any_of(candidate_arcs, [](auto* arc) -> bool { return arc != nullptr && !arc->get_when().empty(); });
  if (has_conditional) {
    ++conditional_extrema_count;
  }
  (void) conditional_fallback_count;
  std::vector<double> values;
  for (auto* arc : candidate_arcs) {
    const double value = arc->getDelayOrConstrainCheckNs(trans_type, clock_slew_ns, data_slew_in_lib_units);
    if (!std::isfinite(value)) {
      return {.status = FastStaCheckStatus::kUnavailable};
    }
    values.push_back(value);
  }
  if (values.empty()) {
    return {.status = candidate_arcs.empty() ? FastStaCheckStatus::kInactive : FastStaCheckStatus::kUnavailable};
  }
  return {.status = FastStaCheckStatus::kMeasured, .requirement_ns = *std::ranges::max_element(values)};
}

}  // namespace icts
