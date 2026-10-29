// SPDX-License-Identifier: MulanPSL-2.0
#include "ReadLiberty.hpp"

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "Lib.hh"
#include "Logger.hpp"
#include "Monitor.hpp"
#include "Utility.hpp"
#include "idm.h"
#include "liberty/LibertyLibrary.hpp"
#include "network/Network.hpp"

namespace ista {
namespace {
ThreeStateEvent threeStateEvent(idb::LibArc* arc)
{
  using Type = idb::LibArc::TimingType;
  switch (arc->get_timing_type()) {
    case Type::kThreeStateEnable:
    case Type::kThreeStateEnableRise:
    case Type::kThreeStateEnableFall:
      return ThreeStateEvent::kEnable;
    case Type::kThreeStateDisable:
    case Type::kThreeStateDisableRise:
    case Type::kThreeStateDisableFall:
      return ThreeStateEvent::kDisable;
    default:
      return ThreeStateEvent::kNone;
  }
}

double nominalVoltage(idb::LibLibrary& library)
{
  double scale = 1.;
  if (const auto& unit = library.get_voltage_unit_name()) {
    std::istringstream input(*unit);
    std::string suffix, extra;
    if (!(input >> scale >> suffix) || (input >> extra) || !std::isfinite(scale) || scale <= 0.)
      throw std::invalid_argument("invalid Liberty voltage_unit: " + *unit);
    if (suffix == "mV" || suffix == "mv")
      scale *= 1e-3;
    else if (suffix == "uV" || suffix == "uv")
      scale *= 1e-6;
    else if (suffix != "V" && suffix != "v")
      throw std::invalid_argument("unsupported Liberty voltage_unit: " + *unit);
  }
  return library.get_nom_voltage() * scale;
}
}  // namespace

void ReadLiberty::read()
{
  Monitor monitor;
  const auto conversion_start = std::chrono::steady_clock::now();
  auto liberty_generation = dmInst->get_liberty_generation();
  std::vector<idb::LibLibrary*> lib_list;
  int32_t configured_workers = 0;
  size_t active_workers = 0U;
  if (liberty_generation != nullptr) {
    configured_workers = liberty_generation->get_configured_workers();
    active_workers = liberty_generation->get_active_workers();
    lib_list.reserve(liberty_generation->get_libraries().size());
    for (const auto& library : liberty_generation->get_libraries()) {
      if (library != nullptr) {
        lib_list.push_back(library.get());
      }
    }
  }
  STALOG.info(Loc::current(), "Liberty conversion starting: libraries=", lib_list.size(), ", configured_workers=", configured_workers,
              ", active_workers=", active_workers, ".");
  readCells(lib_list);
  readLibraryInfo(lib_list);
  const double conversion_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - conversion_start).count();
  STALOG.info(Loc::current(), "Liberty conversion completed: libraries=", lib_list.size(), ", configured_workers=", configured_workers,
              ", active_workers=", active_workers, ", conversion_seconds=", conversion_seconds, monitor.getStatsInfo());
}

void ReadLiberty::readCells(const std::vector<idb::LibLibrary*>& lib_list)
{
  _liberty.get_cell_map().clear();
  for (idb::LibLibrary* lib : lib_list) {
    for (std::unique_ptr<idb::LibCell>& lib_cell : lib->get_cells()) {
      readCell(lib_cell.get());
    }
  }
}

void ReadLiberty::readLibraryInfo(const std::vector<idb::LibLibrary*>& lib_list)
{
  std::vector<std::string> library_name_list;
  for (idb::LibLibrary* lib : lib_list) {
    if (!STAUTIL.exist(library_name_list, lib->get_lib_name())) {
      library_name_list.push_back(lib->get_lib_name());
    }
  }
  _liberty.set_library_name_list(library_name_list);
  idb::LibLibrary* reference_lib = findReferenceLibrary(lib_list);
  if (reference_lib == nullptr) {
    return;
  }
  LibertyLibrary& timing_library = _liberty;
  timing_library.set_has_library_info(true);
  timing_library.set_comment(reference_lib->get_comment());
  timing_library.set_simulation(reference_lib->get_simulation());
  timing_library.set_library_feature_list(reference_lib->get_library_features());
  timing_library.set_default_operating_conditions(reference_lib->get_default_operating_conditions());
  timing_library.set_default_wire_load(reference_lib->get_default_wire_load());
  timing_library.set_current_unit_name(reference_lib->get_current_unit_name());
  timing_library.set_voltage_unit_name(reference_lib->get_voltage_unit_name());
  if (reference_lib->get_voltage_unit_name())
    timing_library.set_voltage_unit_name("1V");
  timing_library.set_cap_unit(capacitanceUnit(reference_lib));
  timing_library.set_resistance_unit(resistanceUnit(reference_lib));
  timing_library.set_time_unit(timeUnit(reference_lib));
  timing_library.set_default_max_transition(reference_lib->get_default_max_transition());
  timing_library.set_default_max_fanout(reference_lib->get_default_max_fanout());
  timing_library.set_default_fanout_load(reference_lib->get_default_fanout_load());
  timing_library.set_nom_process(reference_lib->get_nom_process());
  timing_library.set_nom_voltage(nominalVoltage(*reference_lib));
  timing_library.set_nom_temperature(reference_lib->get_nom_temperature());
  timing_library.set_slew_lower_threshold_pct_rise(reference_lib->get_slew_lower_threshold_pct_rise());
  timing_library.set_slew_upper_threshold_pct_rise(reference_lib->get_slew_upper_threshold_pct_rise());
  timing_library.set_slew_lower_threshold_pct_fall(reference_lib->get_slew_lower_threshold_pct_fall());
  timing_library.set_slew_upper_threshold_pct_fall(reference_lib->get_slew_upper_threshold_pct_fall());
  timing_library.set_input_threshold_pct_rise(reference_lib->get_input_threshold_pct_rise());
  timing_library.set_output_threshold_pct_rise(reference_lib->get_output_threshold_pct_rise());
  timing_library.set_input_threshold_pct_fall(reference_lib->get_input_threshold_pct_fall());
  timing_library.set_output_threshold_pct_fall(reference_lib->get_output_threshold_pct_fall());
  timing_library.set_slew_derate_from_library(reference_lib->get_slew_derate_from_library());
}

idb::LibLibrary* ReadLiberty::findReferenceLibrary(const std::vector<idb::LibLibrary*>& lib_list)
{
  std::map<idb::LibLibrary*, std::pair<int32_t, int32_t>> lib_usage_map;
  for (const std::pair<const std::string, Instance>& instance_pair : _network.instances()) {
    const Instance& instance = instance_pair.second;
    for (idb::LibLibrary* lib : lib_list) {
      idb::LibCell* lib_cell = lib->findCell(instance.get_cell_name().c_str());
      if (lib_cell == nullptr) {
        continue;
      }
      lib_usage_map[lib].first++;
      if (!lib_cell->isMacroCell()) {
        lib_usage_map[lib].second++;
      }
    }
  }

  idb::LibLibrary* reference_lib = nullptr;
  std::tuple<int32_t, int32_t, std::string> reference_key;
  for (std::pair<idb::LibLibrary* const, std::pair<int32_t, int32_t>>& usage_pair : lib_usage_map) {
    idb::LibLibrary* lib = usage_pair.first;
    std::pair<int32_t, int32_t>& usage = usage_pair.second;
    std::tuple<int32_t, int32_t, std::string> lib_key = std::make_tuple(usage.second, usage.first, lib->get_lib_name());
    if (reference_lib == nullptr || lib_key > reference_key) {
      reference_lib = lib;
      reference_key = lib_key;
    }
  }
  if (reference_lib != nullptr) {
    return reference_lib;
  }
  if (!lib_list.empty()) {
    return lib_list.front();
  }
  return nullptr;
}

TimingCapacitiveUnit ReadLiberty::capacitanceUnit(idb::LibLibrary* lib_library)
{
  if (lib_library->get_cap_unit() == idb::CapacitiveUnit::kFF) {
    return TimingCapacitiveUnit::kFF;
  }
  if (lib_library->get_cap_unit() == idb::CapacitiveUnit::kF) {
    return TimingCapacitiveUnit::kF;
  }
  return TimingCapacitiveUnit::kPF;
}

TimingResistanceUnit ReadLiberty::resistanceUnit(idb::LibLibrary* lib_library)
{
  if (lib_library->get_resistance_unit() == idb::ResistanceUnit::kOHM) {
    return TimingResistanceUnit::kOHM;
  }
  return TimingResistanceUnit::kkOHM;
}

TimingTimeUnit ReadLiberty::timeUnit(idb::LibLibrary* lib_library)
{
  if (lib_library->get_time_unit() == idb::TimeUnit::kPS) {
    return TimingTimeUnit::kPS;
  }
  if (lib_library->get_time_unit() == idb::TimeUnit::kFS) {
    return TimingTimeUnit::kFS;
  }
  return TimingTimeUnit::kNS;
}

void ReadLiberty::readCell(idb::LibCell* lib_cell)
{
  idb::LibLibrary* lib_library = lib_cell->get_owner_lib();
  LibertyCell timing_cell;
  timing_cell.set_cell_name(lib_cell->get_cell_name());
  timing_cell.set_library_name(lib_library->get_lib_name());
  timing_cell.set_nom_voltage(nominalVoltage(*lib_library));
  timing_cell.set_area(lib_cell->get_cell_area());
  const double power_scale = lib_library->get_power_unit_mw_scale();
  if (power_scale > 0.0) {
    double leakage_mw = lib_cell->has_cell_leakage_power() ? lib_cell->get_cell_leakage_power() : 0.0;
    if (leakage_mw <= 0.0) {
      double positive_sum = 0.0;
      int positive_count = 0;
      for (idb::LibLeakagePower* power : lib_cell->getLeakagePowerList()) {
        if (power != nullptr && std::isfinite(power->get_value()) && power->get_value() > 0.0) {
          positive_sum += power->get_value();
          ++positive_count;
        }
      }
      if (positive_count > 0)
        leakage_mw = positive_sum / positive_count;
    }
    timing_cell.set_leakage_power(leakage_mw / power_scale);
  }
  timing_cell.set_is_sequential(lib_cell->isSequentialCell());
  timing_cell.set_is_clock_gating(lib_cell->isICG());
  timing_cell.set_is_macro(lib_cell->isMacroCell());
  timing_cell.set_slew_lower_threshold_pct_rise(lib_library->get_slew_lower_threshold_pct_rise());
  timing_cell.set_slew_upper_threshold_pct_rise(lib_library->get_slew_upper_threshold_pct_rise());
  timing_cell.set_slew_lower_threshold_pct_fall(lib_library->get_slew_lower_threshold_pct_fall());
  timing_cell.set_slew_upper_threshold_pct_fall(lib_library->get_slew_upper_threshold_pct_fall());
  timing_cell.set_input_threshold_pct_rise(lib_library->get_input_threshold_pct_rise());
  timing_cell.set_output_threshold_pct_rise(lib_library->get_output_threshold_pct_rise());
  timing_cell.set_input_threshold_pct_fall(lib_library->get_input_threshold_pct_fall());
  timing_cell.set_output_threshold_pct_fall(lib_library->get_output_threshold_pct_fall());
  timing_cell.set_slew_derate_from_library(lib_library->get_slew_derate_from_library());

  for (std::unique_ptr<idb::LibPort>& lib_port : lib_cell->get_cell_ports()) {
    readPort(timing_cell, lib_port.get());
  }
  for (std::unique_ptr<idb::LibPortBus>& lib_bus : lib_cell->get_cell_buses()) {
    for (std::unique_ptr<idb::LibPort>& lib_port : lib_bus->get_ports()) {
      readPort(timing_cell, lib_port.get());
    }
  }

  for (std::unique_ptr<idb::LibArcSet>& lib_arc_set : lib_cell->get_cell_arcs()) {
    readCellArcs(timing_cell, lib_arc_set.get());
  }

  // Preserve the Liberty latch data identity for the early/min consumer.
  // A latch's data_in expression names the transparent data source; its
  // non-clock timing arc is the only cell arc that may remain transparent
  // during an early latch propagation.  Keep this identity attached to the
  // arc instead of asking the consumer to infer it from master or pin names.
  for (const auto& latch : lib_cell->get_latches()) {
    if (latch.data_in.empty())
      continue;
    for (auto& arc : timing_cell.get_cell_arc_list()) {
      if (!arc.get_is_clock_arc() && arc.get_source_port() == latch.data_in)
        arc.set_is_latch_data_arc(true);
    }
  }

  readCellInfo(timing_cell);
  _liberty.get_cell_map()[timing_cell.get_cell_name()] = timing_cell;
}

void ReadLiberty::readPort(LibertyCell& timing_cell, idb::LibPort* lib_port)
{
  LibertyPort timing_cell_port;
  timing_cell_port.set_port_name(lib_port->get_port_name());
  timing_cell_port.set_declaration_index(static_cast<int32_t>(timing_cell.get_port_map().size()));
  timing_cell_port.set_capacitance(lib_port->get_port_cap());
  timing_cell_port.set_drive_resistance(lib_port->driveResistance());
  idb::LibLibrary* library = lib_port->get_ower_cell()->get_owner_lib();
  timing_cell_port.set_fanout_load(lib_port->get_fanout_load().value_or(library->get_default_fanout_load().value_or(1.0)));
  timing_cell_port.set_max_fanout(lib_port->get_max_fanout() ? lib_port->get_max_fanout() : library->get_default_max_fanout());
  timing_cell_port.set_max_capacitance(lib_port->get_port_cap_limit(idb::AnalysisMode::kMax));
  const auto max_transition = lib_port->get_port_slew_limit(idb::AnalysisMode::kMax);
  timing_cell_port.set_max_transition(max_transition ? max_transition : library->get_default_max_transition());
  for (idb::AnalysisMode analysis_mode : {idb::AnalysisMode::kMax, idb::AnalysisMode::kMin}) {
    for (idb::TransType trans_type : {idb::TransType::kRise, idb::TransType::kFall}) {
      std::optional<double> port_cap = lib_port->get_port_cap(analysis_mode, trans_type);
      if (port_cap) {
        AnalysisType sta_analysis_type = analysis_mode == idb::AnalysisMode::kMin ? AnalysisType::kMin : AnalysisType::kMax;
        TransType sta_trans_type = trans_type == idb::TransType::kFall ? TransType::kFall : TransType::kRise;
        timing_cell_port.get_trans_capacitance_map()[sta_analysis_type][sta_trans_type] = *port_cap;
      }
    }
  }
  timing_cell_port.set_is_input(lib_port->isInput());
  timing_cell_port.set_is_output(lib_port->isOutput());
  timing_cell_port.set_is_clock(lib_port->isClock() || lib_port->get_is_clock_pin() || lib_port->get_is_clock());
  std::string function_string = lib_port->get_func_expr_str();
  timing_cell_port.set_function_expression(readLogicExpression(function_string));
  timing_cell_port.set_three_state_expression(readLogicExpression(lib_port->get_three_state_expr_str()));
  timing_cell.get_port_map()[timing_cell_port.get_port_name()] = timing_cell_port;
  readClockChecks(timing_cell, lib_port);
}

void ReadLiberty::readClockChecks(LibertyCell& cell, idb::LibPort* port)
{
  // Scalar pin attributes and table groups feed the same clock check evaluator.
  // Parser scalars are already in ns; their synthetic tables therefore use scale 1.
  for (auto type : {TimingCheckType::kPeriod, TimingCheckType::kWidth}) {
    TimingArc table_arc;
    for (auto edge : {TransType::kRise, TransType::kFall}) {
      const auto value = type == TimingCheckType::kPeriod ? port->get_min_period()
                                                          : port->get_min_pulse_width(edge == TransType::kRise ? idb::TransType::kRise : idb::TransType::kFall);
      if (value)
        table_arc.get_check_table_map()[edge].set_value_list({*value});
    }
    if (table_arc.get_check_table_map().empty())
      continue;
    TimingCheckArc check;
    check.set_clock_port(port->get_port_name());
    check.set_data_port(port->get_port_name());
    check.set_check_type(type);
    check.get_timing_arc_list().push_back(std::move(table_arc));
    cell.get_sdf_check_arc_list().push_back(std::move(check));
  }
}

LogicExpression ReadLiberty::readLogicExpression(const std::string& expression_string)
{
  LogicExpression logic_expression;
  if (expression_string.empty()) {
    return logic_expression;
  }
  idb::LibertyExprBuilder expression_builder(expression_string.c_str());
  expression_builder.execute();
  LibertyExpr* liberty_expr = expression_builder.get_result_expr();
  if (liberty_expr == nullptr) {
    return logic_expression;
  }
  readLogicTerms(logic_expression, liberty_expr);
  liberty_free_expr(liberty_expr);
  return logic_expression;
}

void ReadLiberty::readLogicTerms(LogicExpression& logic_expression, LibertyExpr* liberty_expr)
{
  if (liberty_expr == nullptr) {
    return;
  }
  LibertyExpr* left_expr = liberty_get_expr_left(liberty_expr);
  LibertyExpr* right_expr = liberty_get_expr_right(liberty_expr);
  readLogicTerms(logic_expression, left_expr);
  readLogicTerms(logic_expression, right_expr);
  if (left_expr != nullptr) {
    liberty_free_expr(left_expr);
  }
  if (right_expr != nullptr) {
    liberty_free_expr(right_expr);
  }
  LogicExpressionTerm logic_expression_term;
  LogicOperationType operation_type = logicOperation(static_cast<int32_t>(liberty_expr->op));
  logic_expression_term.set_operation_type(operation_type);
  if (operation_type == LogicOperationType::kPort && liberty_expr->port_name != nullptr) {
    logic_expression_term.set_port_name(liberty_expr->port_name);
  }
  logic_expression.get_term_list().push_back(logic_expression_term);
}

LogicOperationType ReadLiberty::logicOperation(const int32_t liberty_expr_op)
{
  switch (static_cast<LibertyExprOp>(liberty_expr_op)) {
    case LibertyExprOp::kBuffer:
      return LogicOperationType::kPort;
    case LibertyExprOp::kOne:
      return LogicOperationType::kOne;
    case LibertyExprOp::kZero:
      return LogicOperationType::kZero;
    case LibertyExprOp::kNot:
      return LogicOperationType::kNot;
    case LibertyExprOp::kOr:
    case LibertyExprOp::kPlus:
      return LogicOperationType::kOr;
    case LibertyExprOp::kAnd:
    case LibertyExprOp::kMult:
      return LogicOperationType::kAnd;
    case LibertyExprOp::kXor:
      return LogicOperationType::kXor;
    default:
      STALOG.error(Loc::current(), "Unrecognized type!");
      break;
  }
  return LogicOperationType::kNone;
}

void ReadLiberty::readCellArcs(LibertyCell& timing_cell, idb::LibArcSet* lib_arc_set)
{
  idb::LibArc* lib_arc = lib_arc_set->front();
  if (isSdfDelayArc(lib_arc)) {
    TimingCellArc timing_cell_arc = readDelayArc(lib_arc_set);
    timing_cell_arc.set_is_timing_graph_arc(lib_arc->isDelayArc() || timing_cell_arc.isThreeStateArc());
    timing_cell_arc.set_is_clear_preset_arc(lib_arc->isClearPresetArc());
    timing_cell.get_cell_arc_list().push_back(timing_cell_arc);
    if (lib_arc->isClearPresetArc()) {
      readClearPresetArc(timing_cell, lib_arc);
    }
    return;
  }
  if (isSdfCheckArc(lib_arc)) {
    TimingCheckArc timing_check_arc = readCheckArc(lib_arc_set);
    timing_cell.get_sdf_check_arc_list().push_back(timing_check_arc);
    if (!lib_arc->isCheckArc()) {
      return;
    }
    timing_cell.get_check_arc_list().push_back(timing_check_arc);
  }
}

bool ReadLiberty::isSdfDelayArc(idb::LibArc* lib_arc)
{
  if (lib_arc->isDelayArc() || lib_arc->isClearPresetArc()) {
    return true;
  }
  idb::LibArc::TimingType timing_type = lib_arc->get_timing_type();
  return timing_type == idb::LibArc::TimingType::kThreeStateEnable || timing_type == idb::LibArc::TimingType::kThreeStateEnableRise
         || timing_type == idb::LibArc::TimingType::kThreeStateEnableFall || timing_type == idb::LibArc::TimingType::kThreeStateDisable
         || timing_type == idb::LibArc::TimingType::kThreeStateDisableRise || timing_type == idb::LibArc::TimingType::kThreeStateDisableFall;
}

bool ReadLiberty::isSdfCheckArc(idb::LibArc* lib_arc)
{
  return lib_arc->isCheckTableArc();
}

TimingCellArc ReadLiberty::readDelayArc(idb::LibArcSet* lib_arc_set)
{
  idb::LibArc* lib_arc = lib_arc_set->front();
  TimingCellArc timing_cell_arc;
  timing_cell_arc.set_source_port(lib_arc->get_src_port());
  timing_cell_arc.set_sink_port(lib_arc->get_snk_port());
  double delay = lib_arc->isDelayArc() ? lib_arc->getDelayOrConstrainCheckNs(idb::TransType::kRise, 0.0, 0.0) : 0.0;
  timing_cell_arc.set_delay(delay);
  timing_cell_arc.set_delay_max(delay);
  timing_cell_arc.set_delay_min(delay);
  timing_cell_arc.set_timing_arc_list(readArcs(lib_arc_set));
  timing_cell_arc.set_is_clock_arc(lib_arc->isRisingTriggerArc() || lib_arc->isFallingTriggerArc());
  timing_cell_arc.set_is_disable_arc(lib_arc->isDisableArc());
  // Three-state enable/disable arcs are represented as SDF delay arcs, but
  // Liberty does not classify them as ordinary delay arcs.  Keep that role
  // explicit so the graph builder can retain the enable path (OE -> PAD)
  // while still excluding the disable path from data propagation.
  timing_cell_arc.set_is_control_arc(!lib_arc->isDelayArc() && !lib_arc->isClearPresetArc());
  return timing_cell_arc;
}

void ReadLiberty::readClearPresetArc(LibertyCell& timing_cell, idb::LibArc* lib_arc)
{
  if (lib_arc->get_timing_type() == idb::LibArc::TimingType::kClear) {
    timing_cell.set_has_clear_arc(true);
  } else if (lib_arc->get_timing_type() == idb::LibArc::TimingType::kPreset) {
    timing_cell.set_has_preset_arc(true);
  }
}

TimingCheckArc ReadLiberty::readCheckArc(idb::LibArcSet* lib_arc_set)
{
  idb::LibArc* lib_arc = lib_arc_set->front();
  TimingCheckArc timing_check_arc;
  timing_check_arc.set_clock_port(lib_arc->get_src_port());
  timing_check_arc.set_data_port(lib_arc->get_snk_port());
  timing_check_arc.set_check_type(checkType(lib_arc));
  if (lib_arc->isCheckArc()) {
    timing_check_arc.set_check_time(lib_arc->getDelayOrConstrainCheckNs(idb::TransType::kRise, 0.0, 0.0));
  }
  timing_check_arc.set_timing_arc_list(readArcs(lib_arc_set));
  timing_check_arc.set_clock_trans_type(checkTransition(lib_arc));
  return timing_check_arc;
}

std::vector<TimingArc> ReadLiberty::readArcs(idb::LibArcSet* lib_arc_set)
{
  std::vector<TimingArc> timing_arc_list;
  int32_t arc_idx = 0;
  for (std::unique_ptr<idb::LibArc>& lib_arc : lib_arc_set->get_arcs()) {
    if (lib_arc->isDisableArc()) {
      continue;
    }
    TimingArc timing_arc = readArc(lib_arc.get());
    timing_arc.set_arc_idx(arc_idx++);
    timing_arc_list.push_back(timing_arc);
    auto* model = dynamic_cast<idb::LibDelayTableModel*>(lib_arc->get_table_model());
    if (!model)
      continue;
    TimingArc retaining = timing_arc;
    retaining.setRetaining(true);
    retaining.get_delay_table_map().clear();
    retaining.get_slew_table_map().clear();
    using Type = idb::LibTable::TableType;
    for (const auto edge : {TransType::kRise, TransType::kFall}) {
      if (auto* table = model->retainingTable(edge == TransType::kRise ? Type::kRetainingRise : Type::kRetainingFall))
        retaining.get_delay_table_map()[edge] = readTable(table);
      if (auto* table = model->retainingTable(edge == TransType::kRise ? Type::kRetainRiseSlew : Type::kRetainFallSlew))
        retaining.get_slew_table_map()[edge] = readTable(table);
    }
    if (!retaining.get_delay_table_map().empty()) {
      retaining.set_arc_idx(arc_idx++);
      timing_arc_list.push_back(std::move(retaining));
    }
  }
  return timing_arc_list;
}

TimingArc ReadLiberty::readArc(idb::LibArc* lib_arc)
{
  TimingArc timing_arc;
  idb::LibLibrary* lib_library = lib_arc->get_owner_cell()->get_owner_lib();
  timing_arc.set_sense(arcSense(lib_arc));
  timing_arc.setThreeStateEvent(threeStateEvent(lib_arc));
  timing_arc.set_when_expression(readLogicExpression(lib_arc->get_when()));
  timing_arc.set_trigger_trans_type(triggerTransition(lib_arc));
  timing_arc.set_check_trans_type(checkTransition(lib_arc));
  timing_arc.set_library_name(lib_library->get_lib_name());
  timing_arc.set_nom_voltage(nominalVoltage(*lib_library));
  timing_arc.set_sdf_cond(lib_arc->get_sdf_cond());
  timing_arc.set_time_unit_scale(timeUnitScale(lib_library));
  timing_arc.set_cap_unit_scale(capacitanceUnitScale(lib_library));
  timing_arc.set_slew_derate(lib_library->get_slew_derate_from_library());
  timing_arc.set_slew_lower_threshold_pct_rise(lib_library->get_slew_lower_threshold_pct_rise());
  timing_arc.set_slew_upper_threshold_pct_rise(lib_library->get_slew_upper_threshold_pct_rise());
  timing_arc.set_slew_lower_threshold_pct_fall(lib_library->get_slew_lower_threshold_pct_fall());
  timing_arc.set_slew_upper_threshold_pct_fall(lib_library->get_slew_upper_threshold_pct_fall());
  timing_arc.set_input_threshold_pct_rise(lib_library->get_input_threshold_pct_rise());
  timing_arc.set_output_threshold_pct_rise(lib_library->get_output_threshold_pct_rise());
  timing_arc.set_input_threshold_pct_fall(lib_library->get_input_threshold_pct_fall());
  timing_arc.set_output_threshold_pct_fall(lib_library->get_output_threshold_pct_fall());
  readArcTables(timing_arc, lib_arc);
  // Liberty *_rise/*_fall restrict output lanes even if a producer provides
  // an extra table. The control edge is independent of that output lane.
  using Type = idb::LibArc::TimingType;
  const auto type = lib_arc->get_timing_type();
  if (type == Type::kThreeStateEnableRise || type == Type::kThreeStateDisableRise) {
    timing_arc.get_delay_table_map().erase(TransType::kFall);
    timing_arc.get_slew_table_map().erase(TransType::kFall);
  } else if (type == Type::kThreeStateEnableFall || type == Type::kThreeStateDisableFall) {
    timing_arc.get_delay_table_map().erase(TransType::kRise);
    timing_arc.get_slew_table_map().erase(TransType::kRise);
  }
  return timing_arc;
}

void ReadLiberty::readArcTables(TimingArc& timing_arc, idb::LibArc* lib_arc)
{
  idb::LibTableModel* table_model = lib_arc->get_table_model();
  if (table_model == nullptr) {
    return;
  }
  if (table_model->isDelayModel()) {
    idb::LibTable* rise_delay_table = table_model->getTable(CAST_TYPE_TO_INDEX(idb::LibTable::TableType::kCellRise));
    idb::LibTable* fall_delay_table = table_model->getTable(CAST_TYPE_TO_INDEX(idb::LibTable::TableType::kCellFall));
    idb::LibTable* rise_slew_table = table_model->getTable(CAST_TYPE_TO_INDEX(idb::LibTable::TableType::kRiseTransition));
    idb::LibTable* fall_slew_table = table_model->getTable(CAST_TYPE_TO_INDEX(idb::LibTable::TableType::kFallTransition));
    if (rise_delay_table != nullptr) {
      timing_arc.get_delay_table_map()[TransType::kRise] = readTable(rise_delay_table);
    }
    if (fall_delay_table != nullptr) {
      timing_arc.get_delay_table_map()[TransType::kFall] = readTable(fall_delay_table);
    }
    if (rise_slew_table != nullptr) {
      timing_arc.get_slew_table_map()[TransType::kRise] = readTable(rise_slew_table);
    }
    if (fall_slew_table != nullptr) {
      timing_arc.get_slew_table_map()[TransType::kFall] = readTable(fall_slew_table);
    }
    return;
  }
  if (!table_model->isCheckModel()) {
    return;
  }
  idb::LibTable* rise_check_table = table_model->getTable(CAST_TYPE_TO_INDEX(idb::LibTable::TableType::kRiseConstrain));
  idb::LibTable* fall_check_table = table_model->getTable(CAST_TYPE_TO_INDEX(idb::LibTable::TableType::kFallConstrain));
  if (rise_check_table != nullptr) {
    timing_arc.get_check_table_map()[TransType::kRise] = readTable(rise_check_table);
  }
  if (fall_check_table != nullptr) {
    timing_arc.get_check_table_map()[TransType::kFall] = readTable(fall_check_table);
  }
}

TimingTable ReadLiberty::readTable(idb::LibTable* lib_table)
{
  TimingTable timing_table;
  timing_table.set_variable_type1(tableVariableType(lib_table, true));
  timing_table.set_variable_type2(tableVariableType(lib_table, false));
  std::vector<std::vector<double>> axis_list;
  for (std::unique_ptr<idb::LibAxis>& lib_axis : lib_table->get_axes()) {
    std::vector<double> axis_value_list;
    for (std::unique_ptr<idb::LibAttrValue>& axis_value : lib_axis->get_axis_values()) {
      axis_value_list.push_back(axis_value->getFloatValue());
    }
    axis_list.push_back(axis_value_list);
  }
  std::vector<double> value_list;
  for (std::unique_ptr<idb::LibAttrValue>& lib_value : lib_table->get_table_values()) {
    value_list.push_back(lib_value->getFloatValue());
  }
  timing_table.set_axis_list(axis_list);
  timing_table.set_value_list(value_list);
  return timing_table;
}

TimingTableVariableType ReadLiberty::tableVariableType(idb::LibTable* lib_table, bool is_first_variable)
{
  idb::LibLutTableTemplate* table_template = lib_table->get_table_template();
  if (table_template == nullptr) {
    return TimingTableVariableType::kNone;
  }
  std::optional<idb::LibLutTableTemplate::Variable> variable
      = is_first_variable ? table_template->get_template_variable1() : table_template->get_template_variable2();
  if (!variable) {
    return TimingTableVariableType::kNone;
  }
  if (*variable == idb::LibLutTableTemplate::Variable::TOTAL_OUTPUT_NET_CAPACITANCE) {
    return TimingTableVariableType::kOutputCapacitance;
  }
  if (*variable == idb::LibLutTableTemplate::Variable::CONSTRAINED_PIN_TRANSITION) {
    return TimingTableVariableType::kConstrainedTransition;
  }
  if (*variable == idb::LibLutTableTemplate::Variable::INPUT_NET_TRANSITION || *variable == idb::LibLutTableTemplate::Variable::RELATED_PIN_TRANSITION
      || *variable == idb::LibLutTableTemplate::Variable::INPUT_TRANSITION_TIME) {
    return TimingTableVariableType::kInputTransition;
  }
  return TimingTableVariableType::kNone;
}

double ReadLiberty::timeUnitScale(idb::LibLibrary* lib_library)
{
  if (lib_library->get_time_unit() == idb::TimeUnit::kPS) {
    return 1e3;
  }
  if (lib_library->get_time_unit() == idb::TimeUnit::kFS) {
    return 1e6;
  }
  return 1.0;
}

double ReadLiberty::capacitanceUnitScale(idb::LibLibrary* lib_library)
{
  if (lib_library->get_cap_unit() == idb::CapacitiveUnit::kFF) {
    return static_cast<double>(idb::g_pf2ff);
  }
  return 1.0;
}

TimingArcSense ReadLiberty::arcSense(idb::LibArc* lib_arc)
{
  if (lib_arc->isNegativeArc()) {
    return TimingArcSense::kNegative;
  }
  if (lib_arc->isNonUnateArc()) {
    return TimingArcSense::kNonUnate;
  }
  return TimingArcSense::kPositive;
}

TransType ReadLiberty::triggerTransition(idb::LibArc* lib_arc)
{
  const auto event = threeStateEvent(lib_arc);
  if (event != ThreeStateEvent::kNone) {
    using Sense = idb::LibArc::TimingSense;
    switch (lib_arc->get_timing_sense()) {
      case Sense::kPositiveUnate:
        return TransType::kRise;
      case Sense::kNegativeUnate:
        return TransType::kFall;
      case Sense::kNonUnate:
        return TransType::kNone;
      case Sense::kDefault: {
        auto* output = lib_arc->get_owner_cell()->get_cell_port_or_port_bus(lib_arc->get_snk_port());
        if (output != nullptr) {
          const auto disabled = readLogicExpression(output->get_three_state_expr_str());
          const auto low = disabled.evaluate_constant({{lib_arc->get_src_port(), false}});
          const auto high = disabled.evaluate_constant({{lib_arc->get_src_port(), true}});
          if (low && high && *low != *high)
            return (*high == (event == ThreeStateEvent::kDisable)) ? TransType::kRise : TransType::kFall;
        }
        // A compound/unknown enable expression can admit either control edge.
        return TransType::kNone;
      }
    }
  }
  if (lib_arc->isFallingTriggerArc()) {
    return TransType::kFall;
  }
  if (lib_arc->isRisingTriggerArc()) {
    return TransType::kRise;
  }
  return TransType::kNone;
}

TransType ReadLiberty::checkTransition(idb::LibArc* lib_arc)
{
  if (lib_arc->isFallingEdgeCheck()) {
    return TransType::kFall;
  }
  return TransType::kRise;
}

TimingCheckType ReadLiberty::checkType(idb::LibArc* lib_arc)
{
  if (lib_arc->isSetupArc()) {
    return TimingCheckType::kSetup;
  }
  if (lib_arc->isHoldArc()) {
    return TimingCheckType::kHold;
  }
  if (lib_arc->isRecoveryArc()) {
    return TimingCheckType::kRecovery;
  }
  if (lib_arc->isRemovalArc()) {
    return TimingCheckType::kRemoval;
  }
  if (lib_arc->isMpwArc()) {
    return TimingCheckType::kWidth;
  }
  if (lib_arc->get_timing_type() == idb::LibArc::TimingType::kMinimunPeriod) {
    return TimingCheckType::kPeriod;
  }
  return TimingCheckType::kNone;
}

void ReadLiberty::readCellInfo(LibertyCell& timing_cell)
{
  if (!timing_cell.get_check_arc_list().empty()) {
    timing_cell.set_is_sequential(true);
  }
}

}  // namespace ista
