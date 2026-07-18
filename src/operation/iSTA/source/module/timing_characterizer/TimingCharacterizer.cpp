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
#include "TimingCharacterizer.hpp"

#include "DataManager.hpp"
#include "Logger.hpp"
#include "Monitor.hpp"
#include "TCCheckArcCandidate.hpp"
#include "TCDelayArcCandidate.hpp"
#include "Utility.hpp"

namespace ista {

// public

void TimingCharacterizer::initInst()
{
  if (_tc_instance == nullptr) {
    _tc_instance = new TimingCharacterizer();
  }
}

TimingCharacterizer& TimingCharacterizer::getInst()
{
  if (_tc_instance == nullptr) {
    STALOG.error(Loc::current(), "The instance not initialized!");
  }
  return *_tc_instance;
}

void TimingCharacterizer::destroyInst()
{
  if (_tc_instance != nullptr) {
    delete _tc_instance;
    _tc_instance = nullptr;
  }
}

// function

void TimingCharacterizer::characterize()
{
  Monitor monitor;
  STALOG.info(Loc::current(), "Starting...");

  outputLibFileList();

  STALOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

// private

TimingCharacterizer* TimingCharacterizer::_tc_instance = nullptr;

void TimingCharacterizer::outputLibFileList()
{
  outputLibFile(AnalysisType::kMax);
  outputLibFile(AnalysisType::kMin);
}

void TimingCharacterizer::outputLibFile(AnalysisType analysis_type)
{
  Database& database = STADM.getDatabase();
  std::string lib_file_path = getLibFilePath(database.get_design_name(), analysis_type);
  std::unique_ptr<idb::LibLibrary> timing_model = buildTimingModel(analysis_type);
  timing_model->printLibertyLibrary(lib_file_path.c_str());
  STALOG.info(Loc::current(), "Output iSTA extracted lib: ", lib_file_path);
}

std::unique_ptr<idb::LibLibrary> TimingCharacterizer::buildTimingModel(AnalysisType analysis_type)
{
  Database& database = STADM.getDatabase();
  std::unique_ptr<idb::LibLibrary> timing_model = std::make_unique<idb::LibLibrary>(database.get_design_name().c_str());
  buildTimingModelHeader(*timing_model);
  timing_model->addLibertyCell(buildDesignCell(*timing_model, analysis_type));
  return timing_model;
}

void TimingCharacterizer::buildTimingModelHeader(idb::LibLibrary& timing_model)
{
  TimingLibrary& timing_library = STADM.getDatabase().get_timing_library();
  if (timing_library.get_has_library_info()) {
    if (timing_library.get_comment()) {
      timing_model.set_comment(*timing_library.get_comment());
    }
    if (timing_library.get_simulation()) {
      timing_model.set_simulation(*timing_library.get_simulation());
    }
    for (std::string& library_feature : timing_library.get_library_feature_list()) {
      timing_model.add_library_feature(library_feature);
    }
    if (timing_library.get_leakage_power_unit()) {
      timing_model.set_leakage_power_unit(*timing_library.get_leakage_power_unit());
    }
    if (timing_library.get_current_unit_name()) {
      timing_model.set_current_unit_name(*timing_library.get_current_unit_name());
    }
    if (timing_library.get_voltage_unit_name()) {
      timing_model.set_voltage_unit_name(*timing_library.get_voltage_unit_name());
    }
    timing_model.set_cap_unit(getLibCapacitiveUnit(timing_library));
    timing_model.set_resistance_unit(getLibResistanceUnit(timing_library));
    timing_model.set_time_unit(getLibTimeUnit(timing_library));
    if (timing_library.get_default_max_transition()) {
      timing_model.set_default_max_transition(*timing_library.get_default_max_transition());
    }
    if (timing_library.get_default_max_fanout()) {
      timing_model.set_default_max_fanout(*timing_library.get_default_max_fanout());
    }
    if (timing_library.get_default_fanout_load()) {
      timing_model.set_default_fanout_load(*timing_library.get_default_fanout_load());
    }
    if (timing_library.get_nom_process()) {
      timing_model.set_nom_process(*timing_library.get_nom_process());
    }
    timing_model.set_nom_voltage(timing_library.get_nom_voltage());
    if (timing_library.get_nom_temperature()) {
      timing_model.set_nom_temperature(*timing_library.get_nom_temperature());
    }
    timing_model.set_slew_lower_threshold_pct_rise(timing_library.get_slew_lower_threshold_pct_rise() * 100.0);
    timing_model.set_slew_upper_threshold_pct_rise(timing_library.get_slew_upper_threshold_pct_rise() * 100.0);
    timing_model.set_slew_lower_threshold_pct_fall(timing_library.get_slew_lower_threshold_pct_fall() * 100.0);
    timing_model.set_slew_upper_threshold_pct_fall(timing_library.get_slew_upper_threshold_pct_fall() * 100.0);
    timing_model.set_input_threshold_pct_rise(timing_library.get_input_threshold_pct_rise() * 100.0);
    timing_model.set_output_threshold_pct_rise(timing_library.get_output_threshold_pct_rise() * 100.0);
    timing_model.set_input_threshold_pct_fall(timing_library.get_input_threshold_pct_fall() * 100.0);
    timing_model.set_output_threshold_pct_fall(timing_library.get_output_threshold_pct_fall() * 100.0);
    timing_model.set_slew_derate_from_library(timing_library.get_slew_derate_from_library());
  } else {
    timing_model.set_simulation(false);
    timing_model.set_time_unit(idb::TimeUnit::kNS);
    timing_model.set_cap_unit(idb::CapacitiveUnit::kPF);
    timing_model.set_resistance_unit(idb::ResistanceUnit::kkOHM);
  }
  if (timing_model.get_library_features().empty()) {
    timing_model.add_library_feature("report_delay_calculation");
  }
}

idb::CapacitiveUnit TimingCharacterizer::getLibCapacitiveUnit(TimingLibrary& timing_library)
{
  if (timing_library.get_cap_unit() == TimingCapacitiveUnit::kFF) {
    return idb::CapacitiveUnit::kFF;
  }
  if (timing_library.get_cap_unit() == TimingCapacitiveUnit::kF) {
    return idb::CapacitiveUnit::kF;
  }
  return idb::CapacitiveUnit::kPF;
}

idb::ResistanceUnit TimingCharacterizer::getLibResistanceUnit(TimingLibrary& timing_library)
{
  if (timing_library.get_resistance_unit() == TimingResistanceUnit::kOHM) {
    return idb::ResistanceUnit::kOHM;
  }
  return idb::ResistanceUnit::kkOHM;
}

idb::TimeUnit TimingCharacterizer::getLibTimeUnit(TimingLibrary& timing_library)
{
  if (timing_library.get_time_unit() == TimingTimeUnit::kPS) {
    return idb::TimeUnit::kPS;
  }
  if (timing_library.get_time_unit() == TimingTimeUnit::kFS) {
    return idb::TimeUnit::kFS;
  }
  return idb::TimeUnit::kNS;
}

std::unique_ptr<idb::LibCell> TimingCharacterizer::buildDesignCell(idb::LibLibrary& timing_model, AnalysisType analysis_type)
{
  Database& database = STADM.getDatabase();
  std::unique_ptr<idb::LibCell> design_cell = std::make_unique<idb::LibCell>(database.get_design_name().c_str(), &timing_model);
  design_cell->set_is_macro();
  design_cell->set_cell_area(getDesignArea());
  buildPortList(*design_cell);
  buildClockPathArcList(*design_cell);
  buildCheckArcList(*design_cell, analysis_type);
  buildDelayArcList(*design_cell, analysis_type);
  return design_cell;
}

double TimingCharacterizer::getDesignArea()
{
  Database& database = STADM.getDatabase();
  double design_area = 0.0;
  for (std::pair<const std::string, Instance>& instance_pair : database.get_instance_map()) {
    Instance& instance = instance_pair.second;
    if (database.get_timing_library().get_cell_map().count(instance.get_cell_name()) == 0) {
      continue;
    }
    design_area += database.get_timing_library().get_cell_map()[instance.get_cell_name()].get_area();
  }
  return design_area;
}

void TimingCharacterizer::buildPortList(idb::LibCell& design_cell)
{
  Database& database = STADM.getDatabase();
  for (std::pair<const std::string, Pin>& pin_pair : database.get_pin_map()) {
    Pin& pin = pin_pair.second;
    if (pin.get_is_port()) {
      buildPort(design_cell, pin);
    }
  }
}

void TimingCharacterizer::buildPort(idb::LibCell& design_cell, Pin& pin)
{
  std::unique_ptr<idb::LibPort> lib_port = std::make_unique<idb::LibPort>(pin.get_pin_name().c_str());
  lib_port->set_ower_cell(&design_cell);
  lib_port->set_port_type(getLibPortType(pin));
  lib_port->set_is_clock(isClockPort(pin.get_full_name()));
  lib_port->set_port_cap(getPortCapacitance(pin));
  design_cell.addLibertyPort(std::move(lib_port));
}

idb::LibPort::LibertyPortType TimingCharacterizer::getLibPortType(Pin& pin)
{
  if (pin.get_direction() == PinDirection::kInput) {
    return idb::LibPort::LibertyPortType::kInput;
  }
  if (pin.get_direction() == PinDirection::kOutput) {
    return idb::LibPort::LibertyPortType::kOutput;
  }
  if (pin.get_direction() == PinDirection::kInout) {
    return idb::LibPort::LibertyPortType::kInOut;
  }
  return idb::LibPort::LibertyPortType::kDefault;
}

double TimingCharacterizer::getPortCapacitance(Pin& pin)
{
  Database& database = STADM.getDatabase();
  if (pin.get_net_name().empty() || database.get_net_map().count(pin.get_net_name()) == 0) {
    return 0.0;
  }
  Net& net = database.get_net_map()[pin.get_net_name()];
  double port_capacitance = 0.0;
  for (std::string& load_pin_name : net.get_load_pin_list()) {
    if (database.get_pin_map().count(load_pin_name) == 0) {
      continue;
    }
    Pin& load_pin = database.get_pin_map()[load_pin_name];
    if (load_pin.get_is_port() || database.get_instance_map().count(load_pin.get_instance_name()) == 0) {
      continue;
    }
    Instance& load_instance = database.get_instance_map()[load_pin.get_instance_name()];
    if (database.get_timing_library().get_cell_map().count(load_instance.get_cell_name()) == 0) {
      continue;
    }
    TimingCell& timing_cell = database.get_timing_library().get_cell_map()[load_instance.get_cell_name()];
    if (timing_cell.get_port_map().count(load_pin.get_pin_name()) == 0) {
      continue;
    }
    port_capacitance += timing_cell.get_port_map()[load_pin.get_pin_name()].get_capacitance();
  }
  return port_capacitance;
}

void TimingCharacterizer::buildClockPathArcList(idb::LibCell& design_cell)
{
  Database& database = STADM.getDatabase();
  for (std::pair<const std::string, TimingClock>& clock_pair : database.get_timing_constraint().get_clock_map()) {
    for (std::string& clock_port_name : clock_pair.second.get_source_list()) {
      if (isClockPort(clock_port_name)) {
        buildClockPathArc(design_cell, clock_port_name, "min_clock_tree_path");
        buildClockPathArc(design_cell, clock_port_name, "max_clock_tree_path");
      }
    }
  }
}

void TimingCharacterizer::buildClockPathArc(idb::LibCell& design_cell, std::string& clock_port_name, std::string timing_type)
{
  std::string empty_source_port;
  std::unique_ptr<idb::LibArc> lib_arc = makeLibArc(empty_source_port, clock_port_name, timing_type);
  lib_arc->set_timing_sense("positive_unate");
  std::unique_ptr<idb::LibDelayTableModel> delay_model = std::make_unique<idb::LibDelayTableModel>();
  delay_model->addTable(makeScalarTable(idb::LibTable::TableType::kCellRise, 0.0));
  delay_model->addTable(makeScalarTable(idb::LibTable::TableType::kCellFall, 0.0));
  lib_arc->set_table_model(std::move(delay_model));
  lib_arc->set_owner_cell(&design_cell);
  design_cell.addLibertyArc(std::move(lib_arc));
}

void TimingCharacterizer::buildCheckArcList(idb::LibCell& design_cell, AnalysisType analysis_type)
{
  std::map<std::tuple<std::string, std::string, std::string>, TCCheckArcCandidate> candidate_map;
  for (TimingPath* timing_path : getTimingPathList(analysis_type)) {
    if (isInputPort(timing_path->get_start_point()) && isRegisterEndPoint(timing_path->get_end_point())) {
      std::string timing_type = getTimingCheckType(*timing_path, analysis_type);
      std::string related_clock_port = getRelatedClockPort(*timing_path);
      if (timing_type.empty() || related_clock_port.empty()) {
        continue;
      }
      std::string sink_port = timing_path->get_start_point();
      std::tuple<std::string, std::string, std::string> candidate_key = std::make_tuple(related_clock_port, sink_port, timing_type);
      TCCheckArcCandidate& candidate = candidate_map[candidate_key];
      candidate.set_source_port(related_clock_port);
      candidate.set_sink_port(sink_port);
      candidate.set_timing_type(timing_type);
      double constraint = getCheckConstraint(*timing_path, analysis_type);
      if (timing_path->get_trans_type() == TransType::kFall) {
        candidate.set_fall_constraint(candidate.get_has_fall_constraint() ? std::max(candidate.get_fall_constraint(), constraint) : constraint);
        candidate.set_has_fall_constraint(true);
      } else {
        candidate.set_rise_constraint(candidate.get_has_rise_constraint() ? std::max(candidate.get_rise_constraint(), constraint) : constraint);
        candidate.set_has_rise_constraint(true);
      }
    }
  }
  for (std::pair<const std::tuple<std::string, std::string, std::string>, TCCheckArcCandidate>& candidate_pair : candidate_map) {
    TCCheckArcCandidate& candidate = candidate_pair.second;
    buildCheckArc(design_cell, candidate.get_source_port(), candidate.get_sink_port(), candidate.get_timing_type(), candidate.get_rise_constraint(),
                  candidate.get_fall_constraint(), candidate.get_has_rise_constraint(), candidate.get_has_fall_constraint());
  }
}

void TimingCharacterizer::buildCheckArc(idb::LibCell& design_cell, std::string& source_port, std::string& sink_port, std::string& timing_type,
                                        double rise_constraint, double fall_constraint, bool has_rise_constraint, bool has_fall_constraint)
{
  if (source_port.empty() || sink_port.empty() || timing_type.empty() || (!has_rise_constraint && !has_fall_constraint)) {
    return;
  }
  std::unique_ptr<idb::LibArc> lib_arc = makeLibArc(source_port, sink_port, timing_type);
  std::unique_ptr<idb::LibCheckTableModel> check_model = std::make_unique<idb::LibCheckTableModel>();
  if (has_rise_constraint) {
    check_model->addTable(makeScalarTable(idb::LibTable::TableType::kRiseConstrain, rise_constraint));
  }
  if (has_fall_constraint) {
    check_model->addTable(makeScalarTable(idb::LibTable::TableType::kFallConstrain, fall_constraint));
  }
  lib_arc->set_table_model(std::move(check_model));
  lib_arc->set_owner_cell(&design_cell);
  design_cell.addLibertyArc(std::move(lib_arc));
}

std::string TimingCharacterizer::getTimingCheckType(TimingPath& timing_path, AnalysisType analysis_type)
{
  TimingCheckArc* timing_check_arc = getTimingCheckArc(timing_path, analysis_type);
  std::string timing_type_suffix = getTimingCheckTypeSuffix(timing_check_arc == nullptr ? TransType::kRise : timing_check_arc->get_clock_trans_type());
  if (analysis_type == AnalysisType::kMax) {
    if (timing_path.get_check_type() == TimingCheckType::kRecovery) {
      return STAUTIL.getString(GetTimingCheckTypeName()(TimingCheckType::kRecovery), "_", timing_type_suffix);
    }
    return STAUTIL.getString(GetTimingCheckTypeName()(TimingCheckType::kSetup), "_", timing_type_suffix);
  }
  if (timing_path.get_check_type() == TimingCheckType::kRemoval) {
    return STAUTIL.getString(GetTimingCheckTypeName()(TimingCheckType::kRemoval), "_", timing_type_suffix);
  }
  return STAUTIL.getString(GetTimingCheckTypeName()(TimingCheckType::kHold), "_", timing_type_suffix);
}

TimingCheckArc* TimingCharacterizer::getTimingCheckArc(TimingPath& timing_path, AnalysisType analysis_type)
{
  Database& database = STADM.getDatabase();
  if (database.get_pin_map().count(timing_path.get_end_point()) == 0) {
    return nullptr;
  }
  Pin& pin = database.get_pin_map()[timing_path.get_end_point()];
  if (pin.get_is_port() || database.get_instance_map().count(pin.get_instance_name()) == 0) {
    return nullptr;
  }
  Instance& instance = database.get_instance_map()[pin.get_instance_name()];
  for (TimingCheckArc& timing_check_arc : instance.get_check_arc_list()) {
    if (timing_check_arc.get_data_port() == timing_path.get_end_point() && isMatchCheckType(timing_check_arc, analysis_type)) {
      return &timing_check_arc;
    }
  }
  return nullptr;
}

bool TimingCharacterizer::isMatchCheckType(TimingCheckArc& timing_check_arc, AnalysisType analysis_type)
{
  if (analysis_type == AnalysisType::kMin) {
    return timing_check_arc.get_check_type() == TimingCheckType::kHold || timing_check_arc.get_check_type() == TimingCheckType::kRemoval;
  }
  return timing_check_arc.get_check_type() == TimingCheckType::kSetup || timing_check_arc.get_check_type() == TimingCheckType::kRecovery;
}

std::string TimingCharacterizer::getTimingCheckTypeSuffix(TransType clock_trans_type)
{
  return GetTransTypeLibName()(clock_trans_type);
}

std::string TimingCharacterizer::getRelatedClockPort(TimingPath& timing_path)
{
  Database& database = STADM.getDatabase();
  std::string clock_name = timing_path.get_clock_name();
  if (clock_name.empty() && !database.get_timing_constraint().get_clock_map().empty()) {
    clock_name = database.get_timing_constraint().get_clock_map().begin()->first;
  }
  if (database.get_timing_constraint().get_clock_map().count(clock_name) == 0) {
    return "";
  }
  TimingClock& timing_clock = database.get_timing_constraint().get_clock_map()[clock_name];
  for (std::string& source_name : timing_clock.get_source_list()) {
    if (isClockPort(source_name)) {
      return source_name;
    }
  }
  return "";
}

double TimingCharacterizer::getCheckConstraint(TimingPath& timing_path, AnalysisType analysis_type)
{
  if (analysis_type == AnalysisType::kMin) {
    return std::fabs(timing_path.get_check_time());
  }
  return timing_path.get_check_time();
}

void TimingCharacterizer::buildDelayArcList(idb::LibCell& design_cell, AnalysisType analysis_type)
{
  std::map<std::tuple<std::string, std::string, std::string>, TCDelayArcCandidate> candidate_map;
  for (TimingPath* timing_path : getTimingPathList(analysis_type)) {
    if (isOutputPort(timing_path->get_end_point())) {
      std::string source_port = getDelayArcRelatedPin(*timing_path);
      if (source_port.empty()) {
        continue;
      }
      std::string sink_port = timing_path->get_end_point();
      std::string timing_type = getDelayArcTimingType(*timing_path);
      std::tuple<std::string, std::string, std::string> candidate_key = std::make_tuple(source_port, sink_port, timing_type);
      TCDelayArcCandidate& candidate = candidate_map[candidate_key];
      candidate.set_source_port(source_port);
      candidate.set_sink_port(sink_port);
      candidate.set_timing_type(timing_type);
      candidate.set_timing_sense(getDelayArcTimingSense(*timing_path));
      double delay = getDelayArcValue(*timing_path);
      if (timing_path->get_trans_type() == TransType::kFall) {
        candidate.set_fall_delay(getWorseDelay(candidate.get_fall_delay(), delay, candidate.get_has_fall_delay(), analysis_type));
        candidate.set_has_fall_delay(true);
      } else {
        candidate.set_rise_delay(getWorseDelay(candidate.get_rise_delay(), delay, candidate.get_has_rise_delay(), analysis_type));
        candidate.set_has_rise_delay(true);
      }
    }
  }
  for (std::pair<const std::tuple<std::string, std::string, std::string>, TCDelayArcCandidate>& candidate_pair : candidate_map) {
    TCDelayArcCandidate& candidate = candidate_pair.second;
    buildDelayArc(design_cell, candidate.get_source_port(), candidate.get_sink_port(), candidate.get_timing_type(), candidate.get_timing_sense(),
                  candidate.get_rise_delay(), candidate.get_fall_delay(), candidate.get_has_rise_delay(), candidate.get_has_fall_delay());
  }
}

double TimingCharacterizer::getWorseDelay(double current_delay, double delay, bool has_delay, AnalysisType analysis_type)
{
  if (!has_delay) {
    return delay;
  }
  if (analysis_type == AnalysisType::kMin) {
    return std::min(current_delay, delay);
  }
  return std::max(current_delay, delay);
}

void TimingCharacterizer::buildDelayArc(idb::LibCell& design_cell, std::string& source_port, std::string& sink_port, std::string& timing_type,
                                        std::string& timing_sense, double rise_delay, double fall_delay, bool has_rise_delay, bool has_fall_delay)
{
  if (source_port.empty() || sink_port.empty() || timing_type.empty() || (!has_rise_delay && !has_fall_delay)) {
    return;
  }
  std::unique_ptr<idb::LibArc> lib_arc = makeLibArc(source_port, sink_port, timing_type);
  lib_arc->set_timing_sense(timing_sense.c_str());
  std::unique_ptr<idb::LibDelayTableModel> delay_model = std::make_unique<idb::LibDelayTableModel>();
  if (has_rise_delay) {
    delay_model->addTable(makeScalarTable(idb::LibTable::TableType::kCellRise, rise_delay));
    delay_model->addTable(makeScalarTable(idb::LibTable::TableType::kRiseTransition, 0.0));
  }
  if (has_fall_delay) {
    delay_model->addTable(makeScalarTable(idb::LibTable::TableType::kCellFall, fall_delay));
    delay_model->addTable(makeScalarTable(idb::LibTable::TableType::kFallTransition, 0.0));
  }
  lib_arc->set_table_model(std::move(delay_model));
  lib_arc->set_owner_cell(&design_cell);
  design_cell.addLibertyArc(std::move(lib_arc));
}

std::string TimingCharacterizer::getDelayArcRelatedPin(TimingPath& timing_path)
{
  if (isInputPort(timing_path.get_start_point())) {
    return timing_path.get_start_point();
  }
  if (isRegisterStartPoint(timing_path.get_start_point())) {
    return getRelatedClockPort(timing_path);
  }
  return "";
}

std::string TimingCharacterizer::getDelayArcTimingType(TimingPath& timing_path)
{
  if (isRegisterStartPoint(timing_path.get_start_point())) {
    return GetTransTypeLibEdgeName()(getRegisterClockTransType(timing_path));
  }
  return "combinational";
}

TransType TimingCharacterizer::getRegisterClockTransType(TimingPath& timing_path)
{
  Database& database = STADM.getDatabase();
  Pin& pin = database.get_pin_map()[timing_path.get_start_point()];
  Instance& instance = database.get_instance_map()[pin.get_instance_name()];
  if (instance.get_clock_to_q_arc().get_timing_arc_list().empty()) {
    return TransType::kRise;
  }
  TimingArc& timing_arc = instance.get_clock_to_q_arc().get_timing_arc_list().front();
  if (timing_arc.get_trigger_trans_type() == TransType::kFall) {
    return TransType::kFall;
  }
  return TransType::kRise;
}

std::string TimingCharacterizer::getDelayArcTimingSense(TimingPath& timing_path)
{
  if (timing_path.get_point_list().empty()) {
    return "positive_unate";
  }
  if (timing_path.get_trans_type() == timing_path.get_point_list().front().get_trans_type()) {
    return "positive_unate";
  }
  return "negative_unate";
}

double TimingCharacterizer::getDelayArcValue(TimingPath& timing_path)
{
  return timing_path.get_path_delay();
}

std::unique_ptr<idb::LibArc> TimingCharacterizer::makeLibArc(std::string& source_port, std::string& sink_port, std::string& timing_type)
{
  std::unique_ptr<idb::LibArc> lib_arc = std::make_unique<idb::LibArc>();
  lib_arc->set_src_port(source_port.c_str());
  lib_arc->set_snk_port(sink_port.c_str());
  lib_arc->set_timing_type(timing_type.c_str());
  return lib_arc;
}

std::unique_ptr<idb::LibTable> TimingCharacterizer::makeScalarTable(idb::LibTable::TableType table_type, double value)
{
  std::unique_ptr<idb::LibTable> table = std::make_unique<idb::LibTable>(table_type, nullptr);
  table->addTableValue(std::make_unique<idb::LibFloatValue>(value));
  return table;
}

std::string TimingCharacterizer::getLibFilePath(std::string& design_name, AnalysisType analysis_type)
{
  return STAUTIL.getString(STADM.getConfig().tc_temp_directory_path, design_name, "_", GetAnalysisTypeName()(analysis_type), ".lib");
}

std::vector<TimingPath*> TimingCharacterizer::getTimingPathList(AnalysisType analysis_type)
{
  Database& database = STADM.getDatabase();
  std::vector<TimingPath*> timing_path_list;
  for (TimingPathGroup& timing_path_group : database.get_timing_path_group_list()) {
    for (std::pair<const std::string, TimingPathEnd>& timing_path_end_pair : timing_path_group.get_timing_path_end_map()) {
      for (TimingPath& timing_path : timing_path_end_pair.second.get_timing_path_list()) {
        if (isMatchAnalysisType(timing_path, analysis_type)) {
          timing_path_list.push_back(&timing_path);
        }
      }
    }
  }
  return timing_path_list;
}

bool TimingCharacterizer::isMatchAnalysisType(TimingPath& timing_path, AnalysisType analysis_type)
{
  return timing_path.get_analysis_type() == analysis_type;
}

bool TimingCharacterizer::isPort(std::string& pin_name)
{
  Database& database = STADM.getDatabase();
  return database.get_pin_map().count(pin_name) > 0 && database.get_pin_map()[pin_name].get_is_port();
}

bool TimingCharacterizer::isInputPort(std::string& pin_name)
{
  Database& database = STADM.getDatabase();
  return isPort(pin_name)
         && (database.get_pin_map()[pin_name].get_direction() == PinDirection::kInput
             || database.get_pin_map()[pin_name].get_direction() == PinDirection::kInout);
}

bool TimingCharacterizer::isOutputPort(std::string& pin_name)
{
  Database& database = STADM.getDatabase();
  return isPort(pin_name)
         && (database.get_pin_map()[pin_name].get_direction() == PinDirection::kOutput
             || database.get_pin_map()[pin_name].get_direction() == PinDirection::kInout);
}

bool TimingCharacterizer::isClockPort(std::string& pin_name)
{
  Database& database = STADM.getDatabase();
  if (!isPort(pin_name)) {
    return false;
  }
  for (std::pair<const std::string, TimingClock>& clock_pair : database.get_timing_constraint().get_clock_map()) {
    for (std::string& clock_source : clock_pair.second.get_source_list()) {
      if (clock_source == pin_name) {
        return true;
      }
    }
  }
  return false;
}

bool TimingCharacterizer::isRegisterStartPoint(std::string& pin_name)
{
  Database& database = STADM.getDatabase();
  if (database.get_pin_map().count(pin_name) == 0) {
    return false;
  }
  Pin& pin = database.get_pin_map()[pin_name];
  if (pin.get_is_port() || database.get_instance_map().count(pin.get_instance_name()) == 0) {
    return false;
  }
  Instance& instance = database.get_instance_map()[pin.get_instance_name()];
  return instance.get_is_sequential() && pin_name == instance.get_output_pin_name();
}

bool TimingCharacterizer::isRegisterEndPoint(std::string& pin_name)
{
  Database& database = STADM.getDatabase();
  if (database.get_pin_map().count(pin_name) == 0) {
    return false;
  }
  Pin& pin = database.get_pin_map()[pin_name];
  if (pin.get_is_port() || database.get_instance_map().count(pin.get_instance_name()) == 0) {
    return false;
  }
  Instance& instance = database.get_instance_map()[pin.get_instance_name()];
  return instance.get_is_sequential() && !instance.get_check_arc_list().empty();
}

}  // namespace ista
