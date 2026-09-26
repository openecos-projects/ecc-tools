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
 * @file WrapperLiberty.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-29
 * @brief Wrapper-backed Liberty queries for iCTS.
 */
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "IdbCellMaster.h"
#include "IdbLayout.h"
#include "IdbUnits.h"
#include "LibParserCpp.hh"
#include "Logger.hh"
#include "Monitor.hh"
#include "Type.hh"
#include "Wrapper.hh"
#include "config/Config.hh"
#include "design/Design.hh"
#include "design/Inst.hh"
#include "design/Pin.hh"
#include "dm_config.h"
#include "idm.h"
#include "liberty/Lib.hh"

namespace icts {
namespace {

constexpr double kMilliwattToWatt = 1.0 / 1000.0;
constexpr double kClockToggleDensityNumerator = 2.0;
constexpr std::array<idb::LibTable::TableType, 4> kCharArcTableTypes = {
    idb::LibTable::TableType::kCellRise,
    idb::LibTable::TableType::kCellFall,
    idb::LibTable::TableType::kRiseTransition,
    idb::LibTable::TableType::kFallTransition,
};

auto normalizePortName(const std::string& pin_name) -> std::string
{
  const auto separator_pos = pin_name.rfind('/');
  return separator_pos == std::string::npos ? pin_name : pin_name.substr(separator_pos + 1);
}

auto resolvePositiveMax(const std::vector<std::optional<double>>& values) -> std::optional<double>
{
  std::optional<double> max_value = std::nullopt;
  for (const auto& value : values) {
    if (!value.has_value() || !std::isfinite(*value) || *value <= 0.0) {
      continue;
    }
    max_value = max_value.has_value() ? std::optional<double>{std::max(*max_value, *value)} : value;
  }
  return max_value;
}

auto convertLibCapToPf(idb::LibCell* lib_cell, double cap_value) -> double
{
  (void) lib_cell;
  // Pin attributes are normalized by LibParserCpp; only table axes stay raw.
  return cap_value;
}

auto convertLibTimeToNs(idb::LibCell* lib_cell, double time_value) -> double
{
  (void) lib_cell;
  return time_value;
}

auto convertPfLoadToLibUnit(idb::LibCell* lib_cell, double load_pf) -> double
{
  auto* owner_lib = lib_cell != nullptr ? lib_cell->get_owner_lib() : nullptr;
  if (owner_lib == nullptr) {
    return load_pf;
  }

  if (owner_lib->get_cap_unit() == idb::CapacitiveUnit::kFF) {
    return PF_TO_FF(load_pf);
  }
  if (owner_lib->get_cap_unit() == idb::CapacitiveUnit::kPF) {
    return load_pf;
  }
  if (owner_lib->get_cap_unit() == idb::CapacitiveUnit::kF) {
    return PF_TO_F(load_pf);
  }
  return load_pf;
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
  const double cap_pf = convertLibCapToPf(lib_cell, *cap_value);
  return std::isfinite(cap_pf) && cap_pf >= 0.0 ? std::optional<double>{cap_pf} : std::nullopt;
}

auto findBufferArcSet(idb::LibCell* lib_cell) -> std::optional<idb::LibArcSet*>
{
  if (lib_cell == nullptr) {
    return std::nullopt;
  }

  idb::LibPort* input = nullptr;
  idb::LibPort* output = nullptr;
  lib_cell->bufferPorts(input, output);
  if (input == nullptr || output == nullptr) {
    return std::nullopt;
  }

  auto timing_arc_set = lib_cell->findLibertyArcSet(input->get_port_name(), output->get_port_name(), idb::LibArc::TimingType::kComb);
  if (!timing_arc_set.has_value()) {
    timing_arc_set = lib_cell->findLibertyArcSet(input->get_port_name(), output->get_port_name());
  }
  return timing_arc_set;
}

auto convertAxisValue(idb::LibLibrary* owner_lib, idb::LibLutTableTemplate::Variable variable, double axis_value) -> std::optional<double>
{
  if (owner_lib == nullptr) {
    return std::nullopt;
  }

  switch (variable) {
    case idb::LibLutTableTemplate::Variable::TOTAL_OUTPUT_NET_CAPACITANCE:
    case idb::LibLutTableTemplate::Variable::EQUAL_OR_OPPOSITE_OUTPUT_NET_CAPACITANCE:
      return idb::ConvertCapUnit(owner_lib->get_cap_unit(), idb::CapacitiveUnit::kPF, axis_value);
    case idb::LibLutTableTemplate::Variable::INPUT_NET_TRANSITION:
    case idb::LibLutTableTemplate::Variable::RELATED_PIN_TRANSITION:
    case idb::LibLutTableTemplate::Variable::INPUT_TRANSITION_TIME:
    case idb::LibLutTableTemplate::Variable::CONSTRAINED_PIN_TRANSITION:
      return owner_lib->convert_time_unit_to_ns(axis_value);
    default:
      return std::nullopt;
  }
}

auto queryBufferTableAxisMax(idb::LibCell* lib_cell, std::initializer_list<idb::LibLutTableTemplate::Variable> target_variables) -> std::optional<double>
{
  if (lib_cell == nullptr) {
    return std::nullopt;
  }

  auto timing_arc_set = findBufferArcSet(lib_cell);
  if (!timing_arc_set.has_value() || timing_arc_set.value() == nullptr || timing_arc_set.value()->get_arcs().empty()) {
    return std::nullopt;
  }

  auto* owner_lib = lib_cell->get_owner_lib();
  if (owner_lib == nullptr) {
    return std::nullopt;
  }

  auto is_target_variable = [&target_variables](idb::LibLutTableTemplate::Variable variable) -> bool {
    return std::ranges::find(target_variables, variable) != target_variables.end();
  };

  double min_axis_max = std::numeric_limits<double>::infinity();
  bool found_axis = false;

  for (const auto& timing_arc_holder : timing_arc_set.value()->get_arcs()) {
    auto* timing_arc = timing_arc_holder.get();
    auto* delay_model = timing_arc != nullptr ? dynamic_cast<idb::LibDelayTableModel*>(timing_arc->get_table_model()) : nullptr;
    if (delay_model == nullptr) {
      continue;
    }

    for (const auto table_type : kCharArcTableTypes) {
      auto* table = delay_model->getTable(static_cast<int>(table_type));
      if (table == nullptr || table->get_axes().empty()) {
        continue;
      }

      auto* table_template = table->get_table_template();
      if (table_template == nullptr) {
        continue;
      }

      auto inspect_axis = [&](std::optional<idb::LibLutTableTemplate::Variable> variable, unsigned axis_index) -> void {
        if (!variable.has_value() || !is_target_variable(*variable) || axis_index >= table->get_axes().size()) {
          return;
        }

        auto& axis_values = table->getAxis(axis_index).get_axis_values();
        if (axis_values.empty()) {
          return;
        }

        const auto converted_axis_max = convertAxisValue(owner_lib, *variable, axis_values.back()->getFloatValue());
        if (converted_axis_max.has_value() && *converted_axis_max > 0.0) {
          min_axis_max = std::min(min_axis_max, *converted_axis_max);
          found_axis = true;
        }
      };

      inspect_axis(table_template->get_template_variable1(), 0U);
      inspect_axis(table_template->get_template_variable2(), 1U);
    }
  }

  if (!found_axis) {
    return std::nullopt;
  }

  return min_axis_max;
}

auto queryLibOutputPinCapLimitPf(idb::LibCell* lib_cell, const Pin* pin) -> std::optional<double>
{
  if (pin == nullptr || pin->get_inst() == nullptr) {
    return std::nullopt;
  }
  if (lib_cell == nullptr) {
    return std::nullopt;
  }

  const auto port_name = normalizePortName(pin->get_name());
  auto* lib_port = lib_cell->get_cell_port_or_port_bus(port_name.c_str());
  if (lib_port == nullptr) {
    return std::nullopt;
  }
  if (lib_port->isOutput() == 0U) {
    return std::nullopt;
  }

  const auto cap_limit = lib_port->get_port_cap_limit(idb::AnalysisMode::kMax);
  if (!cap_limit.has_value() || !std::isfinite(*cap_limit) || *cap_limit <= 0.0) {
    return std::nullopt;
  }
  const double cap_limit_pf = convertLibCapToPf(lib_cell, *cap_limit);
  return std::isfinite(cap_limit_pf) && cap_limit_pf > 0.0 ? std::optional<double>{cap_limit_pf} : std::nullopt;
}

auto queryConfiguredMaxCapBoundaryPf(std::optional<double> configured_max_cap_pf, const Pin* clock_source) -> std::optional<double>
{
  if (configured_max_cap_pf.has_value() && std::isfinite(*configured_max_cap_pf) && *configured_max_cap_pf > 0.0) {
    CTSLOG.warn(Loc::current(), "Clock-source drive-cap query uses configured max_cap=", *configured_max_cap_pf, " pF as the hard source boundary for ",
                Design::getPinFullName(clock_source), " because source-specific Liberty cap limit is unavailable.");
    return configured_max_cap_pf;
  }
  return std::nullopt;
}

auto flipTrans(idb::TransType trans_type) -> idb::TransType
{
  return trans_type == idb::TransType::kRise ? idb::TransType::kFall : idb::TransType::kRise;
}

auto lookupRootCellDelayNs(idb::LibCell* lib_cell, idb::LibArcSet* timing_arc_set, double input_slew_ns, double output_load_pf) -> std::optional<double>
{
  if (lib_cell == nullptr || timing_arc_set == nullptr || input_slew_ns < 0.0 || output_load_pf < 0.0) {
    return std::nullopt;
  }

  const double output_load = convertPfLoadToLibUnit(lib_cell, output_load_pf);
  double worst_delay_ns = 0.0;
  bool has_delay = false;
  for (const auto input_trans : {idb::TransType::kRise, idb::TransType::kFall}) {
    const auto output_trans = timing_arc_set->isNegativeArc() != 0U ? flipTrans(input_trans) : input_trans;
    if (!timing_arc_set->isMatchTimingType(output_trans)) {
      continue;
    }
    const auto delay_values = timing_arc_set->getDelayOrConstrainCheckNs(input_trans, output_trans, input_slew_ns, output_load);
    for (const double delay_ns : delay_values) {
      if (!std::isfinite(delay_ns)) {
        continue;
      }
      worst_delay_ns = has_delay ? std::max(worst_delay_ns, delay_ns) : delay_ns;
      has_delay = true;
    }
  }
  return has_delay ? std::optional<double>{worst_delay_ns} : std::nullopt;
}

auto lookupRootCellOutputSlewNs(idb::LibCell* lib_cell, idb::LibArcSet* timing_arc_set, double input_slew_ns, double output_load_pf) -> std::optional<double>
{
  if (lib_cell == nullptr || timing_arc_set == nullptr || input_slew_ns < 0.0 || output_load_pf < 0.0) {
    return std::nullopt;
  }

  const double output_load = convertPfLoadToLibUnit(lib_cell, output_load_pf);
  double worst_slew_ns = 0.0;
  bool has_slew = false;
  for (const auto input_trans : {idb::TransType::kRise, idb::TransType::kFall}) {
    const auto output_trans = timing_arc_set->isNegativeArc() != 0U ? flipTrans(input_trans) : input_trans;
    if (!timing_arc_set->isMatchTimingType(output_trans)) {
      continue;
    }
    const auto slew_values = timing_arc_set->getSlewNs(input_trans, output_trans, input_slew_ns, output_load);
    for (const double slew_ns : slew_values) {
      if (!std::isfinite(slew_ns)) {
        continue;
      }
      worst_slew_ns = has_slew ? std::max(worst_slew_ns, slew_ns) : slew_ns;
      has_slew = true;
    }
  }
  return has_slew ? std::optional<double>{worst_slew_ns} : std::nullopt;
}

auto lookupInternalPowerW(idb::LibCell* lib_cell, idb::LibPowerArcSet* power_arc_set, double input_slew_ns, double output_load_pf, double clock_period_ns)
    -> std::optional<double>
{
  if (lib_cell == nullptr || power_arc_set == nullptr || input_slew_ns < 0.0 || output_load_pf < 0.0 || clock_period_ns <= 0.0) {
    return std::nullopt;
  }

  const double output_load = convertPfLoadToLibUnit(lib_cell, output_load_pf);
  const double output_toggle_per_ns = kClockToggleDensityNumerator / clock_period_ns;
  double power_mw = 0.0;
  bool has_power = false;

  for (const auto& power_arc_ptr : power_arc_set->get_power_arcs()) {
    auto* power_arc = power_arc_ptr.get();
    if (power_arc == nullptr || power_arc->get_internal_power_info() == nullptr) {
      continue;
    }
    auto* internal_power_info = power_arc->get_internal_power_info().get();
    const double rise_energy_mw_ns
        = lib_cell->convertInternalPowerTableToMwNs(internal_power_info->gatePower(idb::TransType::kRise, input_slew_ns, output_load));
    const double fall_energy_mw_ns
        = lib_cell->convertInternalPowerTableToMwNs(internal_power_info->gatePower(idb::TransType::kFall, input_slew_ns, output_load));
    const double average_energy_mw_ns = (rise_energy_mw_ns + fall_energy_mw_ns) / 2.0;
    if (std::isfinite(average_energy_mw_ns)) {
      power_mw += output_toggle_per_ns * average_energy_mw_ns;
      has_power = true;
    }
  }

  return has_power ? std::optional<double>{power_mw * kMilliwattToWatt} : std::nullopt;
}

auto lookupLeakagePowerW(idb::LibCell* lib_cell) -> std::optional<double>
{
  if (lib_cell == nullptr) {
    return std::nullopt;
  }

  const double default_leakage_power_w = lib_cell->get_cell_leakage_power();
  if (lib_cell->has_cell_leakage_power()) {
    return std::isfinite(default_leakage_power_w) && default_leakage_power_w >= 0.0 ? std::optional<double>{default_leakage_power_w} : std::nullopt;
  }

  double unconditional_leakage_power_w = 0.0;
  bool has_unconditional_leakage = false;
  for (auto* leakage_power : lib_cell->getLeakagePowerList()) {
    if (leakage_power != nullptr && leakage_power->get_when().empty() && std::isfinite(leakage_power->get_value())) {
      unconditional_leakage_power_w += leakage_power->get_value();
      has_unconditional_leakage = true;
    }
  }
  return has_unconditional_leakage ? std::optional<double>{unconditional_leakage_power_w} : std::nullopt;
}

}  // namespace

auto Wrapper::queryParallelWorkerCount() -> std::size_t
{
  return static_cast<std::size_t>(std::max(1, dmInst->get_config().get_thread_number()));
}

auto Wrapper::loadLibertyIfNeeded() const -> void
{
  const auto current_generation = dmInst->get_liberty_generation();
  const auto& configured_paths = dmInst->get_config().get_lib_paths();
  if (_liberty_loaded && current_generation == _liberty_generation
      && (_liberty_generation == nullptr ? configured_paths.empty() : _liberty_generation->get_paths() == configured_paths)) {
    return;
  }
  ++_liberty_revision;
  _lib_cell_by_master.clear();
  _liberty_generation.reset();

  auto& lib_paths = dmInst->get_config().get_lib_paths();
  if (lib_paths.empty()) {
    _liberty_loaded = true;
    CTSLOG.warn(Loc::current(), "Wrapper: no Liberty files are configured; Liberty-backed CTS queries will return empty values.");
    return;
  }

  auto generation = dmInst->get_liberty_generation();
  if (generation == nullptr || generation->get_paths() != lib_paths) {
    if (!dmInst->readLib(lib_paths)) {
      CTSLOG.warn(Loc::current(), "Wrapper: central Liberty generation load failed; Liberty-backed CTS queries will return empty values.");
      return;
    }
    generation = dmInst->get_liberty_generation();
  }
  if (generation == nullptr) {
    _liberty_loaded = true;
    CTSLOG.warn(Loc::current(), "Wrapper: central Liberty generation is unavailable; Liberty-backed CTS queries will return empty values.");
    return;
  }

  Monitor index_monitor;
  const auto index_start = std::chrono::steady_clock::now();
  size_t cell_count = 0U;
  for (const auto& library : generation->get_libraries()) {
    if (library == nullptr) {
      continue;
    }
    for (const auto& cell : library->get_cells()) {
      if (cell == nullptr) {
        continue;
      }
      _lib_cell_by_master[cell->get_cell_name()] = cell.get();
      ++cell_count;
    }
  }
  _liberty_generation = std::move(generation);
  _liberty_loaded = true;
  const double index_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - index_start).count();
  CTSLOG.info(Loc::current(), "Wrapper: indexed central Liberty generation: libraries=", _liberty_generation->get_libraries().size(),
              ", configured_workers=", _liberty_generation->get_configured_workers(), ", active_workers=", _liberty_generation->get_active_workers(),
              ", cells=", cell_count, ", index_seconds=", index_seconds, index_monitor.getStatsInfo());
}

auto Wrapper::querySdcUnits() const -> std::optional<SdcUnits>
{
  loadLibertyIfNeeded();
  if (_liberty_generation == nullptr) {
    return std::nullopt;
  }
  std::optional<SdcUnits> units;
  for (const auto& library : _liberty_generation->get_libraries()) {
    if (library == nullptr || !library->has_cap_unit() || !library->has_time_unit()) {
      return std::nullopt;
    }
    const SdcUnits candidate{.time_unit_ns = library->convert_time_unit_to_ns(1.0),
                             .capacitance_unit_pf = idb::ConvertCapUnit(library->get_cap_unit(), idb::CapacitiveUnit::kPF, 1.0)};
    if (!std::isfinite(candidate.time_unit_ns) || candidate.time_unit_ns <= 0.0 || !std::isfinite(candidate.capacitance_unit_pf)
        || candidate.capacitance_unit_pf <= 0.0
        || (units.has_value() && (units->time_unit_ns != candidate.time_unit_ns || units->capacitance_unit_pf != candidate.capacitance_unit_pf))) {
      return std::nullopt;
    }
    units = candidate;
  }
  return units;
}

auto Wrapper::queryLibertyRevision() const -> std::uint64_t
{
  loadLibertyIfNeeded();
  return _liberty_revision;
}

auto Wrapper::querySupplyVoltage() const -> std::optional<double>
{
  loadLibertyIfNeeded();
  if (_liberty_generation == nullptr) {
    return std::nullopt;
  }
  std::optional<double> voltage;
  for (const auto& library : _liberty_generation->get_libraries()) {
    if (library == nullptr || !std::isfinite(library->get_nom_voltage()) || library->get_nom_voltage() <= 0.0
        || (voltage.has_value() && *voltage != library->get_nom_voltage())) {
      return std::nullopt;
    }
    voltage = library->get_nom_voltage();
  }
  return voltage;
}

auto Wrapper::findLibertyCell(const std::string& cell_master) const -> idb::LibCell*
{
  if (cell_master.empty()) {
    return nullptr;
  }
  loadLibertyIfNeeded();
  const auto iter = _lib_cell_by_master.find(cell_master);
  return iter == _lib_cell_by_master.end() ? nullptr : iter->second;
}

auto Wrapper::queryCellOutPinCapLimit(const std::string& cell_master) const -> std::optional<double>
{
  auto* lib_cell = findLibertyCell(cell_master);
  if (lib_cell == nullptr) {
    return std::nullopt;
  }

  idb::LibPort* input = nullptr;
  idb::LibPort* output = nullptr;
  lib_cell->bufferPorts(input, output);
  if (output == nullptr) {
    return std::nullopt;
  }

  auto cap_limit = output->get_port_cap_limit(idb::AnalysisMode::kMax);
  if (!cap_limit.has_value() || !std::isfinite(*cap_limit) || *cap_limit <= 0.0) {
    return std::nullopt;
  }
  const double cap_limit_pf = convertLibCapToPf(lib_cell, *cap_limit);
  return std::isfinite(cap_limit_pf) && cap_limit_pf > 0.0 ? std::optional<double>{cap_limit_pf} : std::nullopt;
}

auto Wrapper::queryCellOutPinCapTableAxisMax(const std::string& cell_master) const -> std::optional<double>
{
  return queryBufferTableAxisMax(findLibertyCell(cell_master), {idb::LibLutTableTemplate::Variable::TOTAL_OUTPUT_NET_CAPACITANCE,
                                                                idb::LibLutTableTemplate::Variable::EQUAL_OR_OPPOSITE_OUTPUT_NET_CAPACITANCE});
}

auto Wrapper::queryClockSourceDriveCapLimit(const ClockSourceDriveCapLimitInput& input) const -> std::optional<double>
{
  const auto* clock_source = input.clock_source;
  if (clock_source == nullptr) {
    return std::nullopt;
  }

  if (auto* inst = clock_source->get_inst(); inst != nullptr) {
    const auto& cell_master = inst->get_cell_master();
    auto* lib_cell = findLibertyCell(cell_master);
    const auto lib_cap_limit_pf = queryLibOutputPinCapLimitPf(lib_cell, clock_source);
    if (lib_cap_limit_pf.has_value()) {
      return lib_cap_limit_pf;
    }

    return queryConfiguredMaxCapBoundaryPf(input.configured_max_cap_pf, clock_source);
  }

  const auto configured_cap_limit_pf = queryConfiguredMaxCapBoundaryPf(input.configured_max_cap_pf, clock_source);
  if (configured_cap_limit_pf.has_value()) {
    return configured_cap_limit_pf;
  }
  if (!clock_source->is_io()) {
    CTSLOG.warn(Loc::current(), "Clock-source drive-cap query skipped: CTS pin \"", Design::getPinFullName(clock_source),
                "\" has no owning inst and is not marked as top-level IO.");
  }
  return std::nullopt;
}

auto Wrapper::queryClockSourceDriveCapLimit(const Config& config, const Pin* clock_source) const -> std::optional<double>
{
  return queryClockSourceDriveCapLimit(ClockSourceDriveCapLimitInput{
      .clock_source = clock_source,
      .configured_max_cap_pf = config.has_max_cap() && config.get_max_cap() > 0.0 ? std::optional<double>{config.get_max_cap()} : std::nullopt,
  });
}

auto Wrapper::queryCellInPinSlewLimit(const std::string& cell_master) const -> std::optional<double>
{
  auto* lib_cell = findLibertyCell(cell_master);
  if (lib_cell == nullptr) {
    return std::nullopt;
  }

  idb::LibPort* input = nullptr;
  idb::LibPort* output = nullptr;
  lib_cell->bufferPorts(input, output);
  if (input == nullptr) {
    return std::nullopt;
  }

  auto slew_limit = input->get_port_slew_limit(idb::AnalysisMode::kMax);
  if (!slew_limit.has_value() || !std::isfinite(*slew_limit) || *slew_limit <= 0.0) {
    return std::nullopt;
  }
  const double slew_limit_ns = convertLibTimeToNs(lib_cell, *slew_limit);
  return std::isfinite(slew_limit_ns) && slew_limit_ns > 0.0 ? std::optional<double>{slew_limit_ns} : std::nullopt;
}

auto Wrapper::queryCellInPinSlewTableAxisMax(const std::string& cell_master) const -> std::optional<double>
{
  return queryBufferTableAxisMax(findLibertyCell(cell_master),
                                 {idb::LibLutTableTemplate::Variable::INPUT_NET_TRANSITION, idb::LibLutTableTemplate::Variable::RELATED_PIN_TRANSITION,
                                  idb::LibLutTableTemplate::Variable::INPUT_TRANSITION_TIME, idb::LibLutTableTemplate::Variable::CONSTRAINED_PIN_TRANSITION});
}

auto Wrapper::queryPinSlewLimit(const PinSlewLimitInput& input) const -> std::optional<double>
{
  const auto* pin = input.pin;
  if (pin == nullptr) {
    return std::nullopt;
  }

  auto* inst = pin->get_inst();
  if (inst == nullptr) {
    const double configured_limit_ns = input.configured_max_sink_tran_ns;
    return std::isfinite(configured_limit_ns) && configured_limit_ns > 0.0 ? std::optional<double>{configured_limit_ns} : std::nullopt;
  }

  const auto& cell_master = inst->get_cell_master();
  if (cell_master.empty()) {
    const double configured_limit_ns = input.configured_max_sink_tran_ns;
    return std::isfinite(configured_limit_ns) && configured_limit_ns > 0.0 ? std::optional<double>{configured_limit_ns} : std::nullopt;
  }

  auto* lib_cell = findLibertyCell(cell_master);
  if (lib_cell == nullptr) {
    const double configured_limit_ns = input.configured_max_sink_tran_ns;
    return std::isfinite(configured_limit_ns) && configured_limit_ns > 0.0 ? std::optional<double>{configured_limit_ns} : std::nullopt;
  }

  const auto port_name = normalizePortName(pin->get_name());
  auto* lib_port = lib_cell->get_cell_port_or_port_bus(port_name.c_str());
  if (lib_port != nullptr && lib_port->isInput() != 0U) {
    if (auto slew_limit = lib_port->get_port_slew_limit(idb::AnalysisMode::kMax); slew_limit.has_value() && std::isfinite(*slew_limit) && *slew_limit > 0.0) {
      const double slew_limit_ns = convertLibTimeToNs(lib_cell, *slew_limit);
      if (std::isfinite(slew_limit_ns) && slew_limit_ns > 0.0) {
        return slew_limit_ns;
      }
    }
  }

  auto* owner_lib = lib_cell->get_owner_lib();
  if (owner_lib != nullptr) {
    const auto default_max_transition = owner_lib->get_default_max_transition();
    const auto default_max_transition_ns = resolvePositiveMax({default_max_transition});
    if (default_max_transition_ns.has_value()) {
      const double converted_limit_ns = convertLibTimeToNs(lib_cell, *default_max_transition_ns);
      if (std::isfinite(converted_limit_ns) && converted_limit_ns > 0.0) {
        return converted_limit_ns;
      }
    }
  }

  const double configured_limit_ns = input.configured_max_sink_tran_ns;
  return std::isfinite(configured_limit_ns) && configured_limit_ns > 0.0 ? std::optional<double>{configured_limit_ns} : std::nullopt;
}

auto Wrapper::queryPinSlewLimit(const Config& config, const Pin* pin) const -> std::optional<double>
{
  return queryPinSlewLimit(PinSlewLimitInput{
      .pin = pin,
      .configured_max_sink_tran_ns = config.get_max_sink_tran(),
  });
}

auto Wrapper::queryCellHeightUm(const std::string& cell_master) const -> std::optional<double>
{
  if (_idb_layout == nullptr || _idb_layout->get_cell_master_list() == nullptr || _idb_layout->get_units() == nullptr) {
    return std::nullopt;
  }

  auto* idb_master = _idb_layout->get_cell_master_list()->find_cell_master(cell_master);
  if (idb_master == nullptr) {
    return std::nullopt;
  }

  const int dbu_per_micron = _idb_layout->get_units()->get_micron_dbu();
  if (dbu_per_micron <= 0) {
    return std::nullopt;
  }

  const double height_um = static_cast<double>(idb_master->get_height()) / static_cast<double>(dbu_per_micron);
  return std::isfinite(height_um) && height_um > 0.0 ? std::optional<double>{height_um} : std::nullopt;
}

auto Wrapper::queryCellAreaUm2(const std::string& cell_master) const -> std::optional<double>
{
  if (_idb_layout == nullptr || _idb_layout->get_cell_master_list() == nullptr || _idb_layout->get_units() == nullptr) {
    return std::nullopt;
  }

  auto* idb_master = _idb_layout->get_cell_master_list()->find_cell_master(cell_master);
  if (idb_master == nullptr) {
    return std::nullopt;
  }

  const int dbu_per_micron = _idb_layout->get_units()->get_micron_dbu();
  if (dbu_per_micron <= 0) {
    return std::nullopt;
  }

  const auto dbu_per_micron_double = static_cast<double>(dbu_per_micron);
  const double area_um2
      = (static_cast<double>(idb_master->get_width()) * static_cast<double>(idb_master->get_height())) / (dbu_per_micron_double * dbu_per_micron_double);
  return std::isfinite(area_um2) && area_um2 > 0.0 ? std::optional<double>{area_um2} : std::nullopt;
}

auto Wrapper::queryCharInputPinCap(const std::string& cell_master) const -> std::optional<double>
{
  auto* lib_cell = findLibertyCell(cell_master);
  if (lib_cell == nullptr) {
    return std::nullopt;
  }
  idb::LibPort* input = nullptr;
  idb::LibPort* output = nullptr;
  lib_cell->bufferPorts(input, output);
  if (input == nullptr) {
    return std::nullopt;
  }
  return queryLibPortCapacitancePf(lib_cell, input);
}

auto Wrapper::queryPinCapacitance(const Pin* pin) const -> std::optional<double>
{
  if (pin == nullptr) {
    return std::nullopt;
  }

  auto* inst = pin->get_inst();
  if (inst == nullptr) {
    return std::nullopt;
  }

  const auto& cell_master = inst->get_cell_master();
  if (cell_master.empty()) {
    return std::nullopt;
  }

  auto* lib_cell = findLibertyCell(cell_master);
  if (lib_cell == nullptr) {
    return std::nullopt;
  }

  const auto port_name = normalizePortName(pin->get_name());
  auto* lib_port = lib_cell->get_cell_port_or_port_bus(port_name.c_str());
  if (lib_port == nullptr) {
    return std::nullopt;
  }
  return queryLibPortCapacitancePf(lib_cell, lib_port);
}

auto Wrapper::queryPinCapacitance(const Pin* pin, bool early, WrapperTimingTransition transition) const -> std::optional<double>
{
  if (pin == nullptr || pin->get_inst() == nullptr) {
    return std::nullopt;
  }
  auto* lib_cell = findLibertyCell(pin->get_inst()->get_cell_master());
  auto* lib_port = lib_cell == nullptr ? nullptr : lib_cell->get_cell_port_or_port_bus(normalizePortName(pin->get_name()).c_str());
  if (lib_port == nullptr || lib_port->isInput() == 0U) {
    return std::nullopt;
  }
  const auto analysis = early ? idb::AnalysisMode::kMin : idb::AnalysisMode::kMax;
  const auto trans_type = transition == WrapperTimingTransition::kRise ? idb::TransType::kRise : idb::TransType::kFall;
  const auto cap = lib_port->get_port_cap(analysis, trans_type);
  if (cap.has_value()) {
    return std::isfinite(*cap) && *cap >= 0.0 ? cap : std::nullopt;
  }
  const auto nominal = lib_port->get_port_cap();
  return lib_port->has_port_cap() && std::isfinite(nominal) && nominal >= 0.0 ? std::optional<double>{nominal} : std::nullopt;
}

auto Wrapper::queryRootDriverCostDirect(const std::string& cell_master, double input_slew_ns, double output_load_pf, double clock_period_ns) const
    -> RootDriverCost
{
  RootDriverCost cost{
      .method = "direct",
      .cell_master = cell_master,
      .input_slew_ns = input_slew_ns,
      .output_load_pf = output_load_pf,
  };
  if (cell_master.empty() || !std::isfinite(input_slew_ns) || !std::isfinite(output_load_pf) || !std::isfinite(clock_period_ns) || input_slew_ns < 0.0
      || output_load_pf < 0.0 || clock_period_ns <= 0.0) {
    return cost;
  }
  auto* lib_cell = findLibertyCell(cell_master);
  if (lib_cell == nullptr) {
    return cost;
  }

  idb::LibPort* input_port = nullptr;
  idb::LibPort* output_port = nullptr;
  lib_cell->bufferPorts(input_port, output_port);
  if (input_port == nullptr || output_port == nullptr) {
    return cost;
  }

  const auto timing_arc_set = findBufferArcSet(lib_cell);
  auto power_arc_set = lib_cell->findLibertyPowerArcSet(input_port->get_port_name(), output_port->get_port_name());
  if (!timing_arc_set.has_value() || *timing_arc_set == nullptr) {
    return cost;
  }
  const auto cell_delay_ns = lookupRootCellDelayNs(lib_cell, *timing_arc_set, input_slew_ns, output_load_pf);
  const auto output_slew_ns = lookupRootCellOutputSlewNs(lib_cell, *timing_arc_set, input_slew_ns, output_load_pf);
  if (!cell_delay_ns.has_value() || !output_slew_ns.has_value()) {
    return cost;
  }
  std::optional<double> internal_power_w;
  if (power_arc_set.has_value() && *power_arc_set != nullptr) {
    internal_power_w = lookupInternalPowerW(lib_cell, *power_arc_set, input_slew_ns, output_load_pf, clock_period_ns);
  }
  const auto leakage_power_w = lookupLeakagePowerW(lib_cell);
  cost.valid = true;
  cost.cell_delay_ns = *cell_delay_ns;
  cost.output_slew_ns = *output_slew_ns;
  if (internal_power_w.has_value() && leakage_power_w.has_value()) {
    cost.power_available = true;
    cost.internal_power_w = *internal_power_w;
    cost.leakage_power_w = *leakage_power_w;
    cost.cell_power_w = *internal_power_w + *leakage_power_w;
  }
  return cost;
}

auto Wrapper::queryBufferPorts(const std::string& cell_master) const -> std::optional<BufferPorts>
{
  auto* lib_cell = findLibertyCell(cell_master);
  if (lib_cell == nullptr) {
    return std::nullopt;
  }
  idb::LibPort* input = nullptr;
  idb::LibPort* output = nullptr;
  lib_cell->bufferPorts(input, output);
  if (input == nullptr || output == nullptr) {
    return std::nullopt;
  }
  BufferPorts ports{.input = input->get_port_name(), .output = output->get_port_name()};
  if (ports.input.empty() || ports.output.empty() || ports.input == ports.output) {
    return std::nullopt;
  }
  return ports;
}

}  // namespace icts
