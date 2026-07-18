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
// WHETHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "DataManager.hpp"

#include "Logger.hpp"
#include "Monitor.hpp"
#include "STAInterface.hpp"
#include "Utility.hpp"
#include "idm.h"
#include "liberty/Lib.hh"
#include "spef/SpefParser.hh"

namespace ista {

// public

void DataManager::initInst()
{
  if (_dm_instance == nullptr) {
    _dm_instance = new DataManager();
  }
}

DataManager& DataManager::getInst()
{
  if (_dm_instance == nullptr) {
    STALOG.error(Loc::current(), "The instance not initialized!");
  }
  return *_dm_instance;
}

void DataManager::destroyInst()
{
  if (_dm_instance != nullptr) {
    delete _dm_instance;
    _dm_instance = nullptr;
  }
}

// function

void DataManager::input(std::map<std::string, std::any>& config_map)
{
  Monitor monitor;
  STALOG.info(Loc::current(), "Starting...");
  STAI.input(config_map);
  buildConfig();
  buildDatabase();
  printConfig();
  printDatabase();
  STALOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void DataManager::output()
{
  Monitor monitor;
  STALOG.info(Loc::current(), "Starting...");
  STAI.output();
  STALOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

// private

DataManager* DataManager::_dm_instance = nullptr;

#if 1  // build

void DataManager::buildConfig()
{
  /////////////////////////////////////////////
  // **********        STA        ********** //
  _config.temp_directory_path = std::filesystem::absolute(_config.temp_directory_path);
  _config.temp_directory_path += "/";
  _config.log_file_path = _config.temp_directory_path + "sta.log";
  _config.path_report_number = 10000;
  // **********    DataManager    ********** //
  _config.dm_temp_directory_path = _config.temp_directory_path + "data_manager/";
  // **********   GraphBuilder    ********** //
  _config.gb_temp_directory_path = _config.temp_directory_path + "graph_builder/";
  // ********* DelayCalculator   ********* //
  _config.dc_temp_directory_path = _config.temp_directory_path + "delay_calculator/";
  // ******** ClockPropagator    ********* //
  _config.cp_temp_directory_path = _config.temp_directory_path + "clock_propagator/";
  // ********* TimingPropagator   ********* //
  _config.tp_temp_directory_path = _config.temp_directory_path + "timing_propagator/";
  // ********** TimingAnalyzer   ********* //
  _config.ta_temp_directory_path = _config.temp_directory_path + "timing_analyzer/";
  // ******* TimingCharacterizer ******* //
  _config.tc_temp_directory_path = _config.temp_directory_path + "timing_characterizer/";
  // **********  TimingReporter   ********** //
  _config.tr_temp_directory_path = _config.temp_directory_path + "timing_reporter/";
  // ************  SDFWriter  ************* //
  _config.sw_temp_directory_path = _config.temp_directory_path + "sdf_writer/";
  /////////////////////////////////////////////
  // **********        STA        ********** //
  STAUTIL.removeDir(_config.temp_directory_path);
  STAUTIL.createDir(_config.temp_directory_path);
  STAUTIL.createDirByFile(_config.log_file_path);
  // **********    DataManager    ********** //
  STAUTIL.createDir(_config.dm_temp_directory_path);
  // **********   GraphBuilder    ********** //
  STAUTIL.createDir(_config.gb_temp_directory_path);
  // ********* DelayCalculator   ********* //
  STAUTIL.createDir(_config.dc_temp_directory_path);
  // ******** ClockPropagator    ********* //
  STAUTIL.createDir(_config.cp_temp_directory_path);
  // ********* TimingPropagator   ********* //
  STAUTIL.createDir(_config.tp_temp_directory_path);
  // ********** TimingAnalyzer   ********* //
  STAUTIL.createDir(_config.ta_temp_directory_path);
  // ******* TimingCharacterizer ******* //
  STAUTIL.createDir(_config.tc_temp_directory_path);
  // **********  TimingReporter   ********** //
  STAUTIL.createDir(_config.tr_temp_directory_path);
  // ************  SDFWriter  ************* //
  STAUTIL.createDir(_config.sw_temp_directory_path);
  /////////////////////////////////////////////
  STALOG.openLogFileStream(_config.log_file_path);
}

void DataManager::buildDatabase()
{
  buildTimingLibrary();
  buildInstanceList();
  buildNetList();
  buildInstanceTimingInfo();
  buildParasiticLibrary();
  readConstraint();
}

void DataManager::buildTimingLibrary()
{
  Monitor monitor;
  STALOG.info(Loc::current(), "Starting...");
  bool old_silent_output = idb::Lib::isSilentOutput();
  idb::Lib::setSilentOutput(true);
  std::vector<std::unique_ptr<idb::LibLibrary>> lib_list;
  for (idb::LibertyReader& liberty_reader : dmInst->get_lib_readers()) {
    liberty_reader.linkLib();
    idb::LibBuilder* lib_builder = liberty_reader.get_library_builder();
    lib_list.push_back(lib_builder->takeLib());
    delete lib_builder;
    liberty_reader.set_library_builder(nullptr);
  }
  buildTimingCellMap(lib_list);
  buildTimingLibraryInfo(lib_list);
  idb::Lib::setSilentOutput(old_silent_output);
  STALOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void DataManager::buildTimingCellMap(std::vector<std::unique_ptr<idb::LibLibrary>>& lib_list)
{
  Database& database = _database;
  database.get_timing_library().get_cell_map().clear();
  for (std::unique_ptr<idb::LibLibrary>& lib : lib_list) {
    for (std::unique_ptr<idb::LibCell>& lib_cell : lib->get_cells()) {
      makeTimingCell(lib_cell.get());
    }
  }
}

void DataManager::buildTimingLibraryInfo(std::vector<std::unique_ptr<idb::LibLibrary>>& lib_list)
{
  Database& database = _database;
  idb::LibLibrary* reference_lib = getReferenceLib(lib_list);
  if (reference_lib == nullptr) {
    return;
  }
  TimingLibrary& timing_library = database.get_timing_library();
  timing_library.set_has_library_info(true);
  timing_library.set_comment(reference_lib->get_comment());
  timing_library.set_simulation(reference_lib->get_simulation());
  timing_library.set_library_feature_list(reference_lib->get_library_features());
  timing_library.set_leakage_power_unit(reference_lib->get_leakage_power_unit());
  timing_library.set_current_unit_name(reference_lib->get_current_unit_name());
  timing_library.set_voltage_unit_name(reference_lib->get_voltage_unit_name());
  timing_library.set_cap_unit(getTimingCapacitiveUnit(reference_lib));
  timing_library.set_resistance_unit(getTimingResistanceUnit(reference_lib));
  timing_library.set_time_unit(getTimingTimeUnit(reference_lib));
  timing_library.set_default_max_transition(reference_lib->get_default_max_transition());
  timing_library.set_default_max_fanout(reference_lib->get_default_max_fanout());
  timing_library.set_default_fanout_load(reference_lib->get_default_fanout_load());
  timing_library.set_nom_process(reference_lib->get_nom_process());
  timing_library.set_nom_voltage(reference_lib->get_nom_voltage());
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

idb::LibLibrary* DataManager::getReferenceLib(std::vector<std::unique_ptr<idb::LibLibrary>>& lib_list)
{
  Database& database = _database;
  std::map<idb::LibLibrary*, std::pair<int32_t, int32_t>> lib_usage_map;
  for (std::pair<const std::string, Instance>& instance_pair : database.get_instance_map()) {
    Instance& instance = instance_pair.second;
    for (std::unique_ptr<idb::LibLibrary>& lib : lib_list) {
      idb::LibCell* lib_cell = lib->findCell(instance.get_cell_name().c_str());
      if (lib_cell == nullptr) {
        continue;
      }
      lib_usage_map[lib.get()].first++;
      if (!lib_cell->isMacroCell()) {
        lib_usage_map[lib.get()].second++;
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
    return lib_list.front().get();
  }
  return nullptr;
}

TimingCapacitiveUnit DataManager::getTimingCapacitiveUnit(idb::LibLibrary* lib_library)
{
  if (lib_library->get_cap_unit() == idb::CapacitiveUnit::kFF) {
    return TimingCapacitiveUnit::kFF;
  }
  if (lib_library->get_cap_unit() == idb::CapacitiveUnit::kF) {
    return TimingCapacitiveUnit::kF;
  }
  return TimingCapacitiveUnit::kPF;
}

TimingResistanceUnit DataManager::getTimingResistanceUnit(idb::LibLibrary* lib_library)
{
  if (lib_library->get_resistance_unit() == idb::ResistanceUnit::kOHM) {
    return TimingResistanceUnit::kOHM;
  }
  return TimingResistanceUnit::kkOHM;
}

TimingTimeUnit DataManager::getTimingTimeUnit(idb::LibLibrary* lib_library)
{
  if (lib_library->get_time_unit() == idb::TimeUnit::kPS) {
    return TimingTimeUnit::kPS;
  }
  if (lib_library->get_time_unit() == idb::TimeUnit::kFS) {
    return TimingTimeUnit::kFS;
  }
  return TimingTimeUnit::kNS;
}

void DataManager::makeTimingCell(idb::LibCell* lib_cell)
{
  Database& database = _database;
  idb::LibLibrary* lib_library = lib_cell->get_owner_lib();
  TimingCell timing_cell;
  timing_cell.set_cell_name(lib_cell->get_cell_name());
  timing_cell.set_library_name(lib_library->get_lib_name());
  timing_cell.set_area(lib_cell->get_cell_area());
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
    makeTimingCellPort(timing_cell, lib_port.get());
  }

  for (std::unique_ptr<idb::LibArcSet>& lib_arc_set : lib_cell->get_cell_arcs()) {
    makeTimingCellArc(timing_cell, lib_arc_set.get());
  }

  updateTimingCell(timing_cell);
  database.get_timing_library().get_cell_map()[timing_cell.get_cell_name()] = timing_cell;
}

void DataManager::makeTimingCellPort(TimingCell& timing_cell, idb::LibPort* lib_port)
{
  TimingCellPort timing_cell_port;
  timing_cell_port.set_port_name(lib_port->get_port_name());
  timing_cell_port.set_capacitance(lib_port->get_port_cap());
  timing_cell_port.set_drive_resistance(lib_port->driveResistance());
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
  timing_cell.get_port_map()[timing_cell_port.get_port_name()] = timing_cell_port;
}

void DataManager::makeTimingCellArc(TimingCell& timing_cell, idb::LibArcSet* lib_arc_set)
{
  idb::LibArc* lib_arc = lib_arc_set->front();
  if (isSDFDelayArc(lib_arc)) {
    TimingCellArc timing_cell_arc = makeDelayArc(lib_arc_set);
    timing_cell_arc.set_is_timing_graph_arc(lib_arc->isDelayArc());
    timing_cell.get_cell_arc_list().push_back(timing_cell_arc);
    if (lib_arc->isClearPresetArc()) {
      updateClearPresetArc(timing_cell, lib_arc);
    }
    return;
  }
  if (isSDFCheckArc(lib_arc)) {
    TimingCheckArc timing_check_arc = makeCheckArc(lib_arc_set);
    timing_cell.get_sdf_check_arc_list().push_back(timing_check_arc);
    if (!lib_arc->isCheckArc()) {
      return;
    }
    timing_cell.get_check_arc_list().push_back(timing_check_arc);
    if (timing_check_arc.get_check_type() == TimingCheckType::kSetup) {
      timing_cell.get_setup_arc_list().push_back(timing_check_arc);
    }
  }
}

bool DataManager::isSDFDelayArc(idb::LibArc* lib_arc)
{
  if (lib_arc->isDelayArc() || lib_arc->isClearPresetArc()) {
    return true;
  }
  idb::LibArc::TimingType timing_type = lib_arc->get_timing_type();
  return timing_type == idb::LibArc::TimingType::kThreeStateEnable || timing_type == idb::LibArc::TimingType::kThreeStateEnableRise
         || timing_type == idb::LibArc::TimingType::kThreeStateEnableFall || timing_type == idb::LibArc::TimingType::kThreeStateDisable
         || timing_type == idb::LibArc::TimingType::kThreeStateDisableRise || timing_type == idb::LibArc::TimingType::kThreeStateDisableFall;
}

bool DataManager::isSDFCheckArc(idb::LibArc* lib_arc)
{
  return lib_arc->isCheckTableArc();
}

TimingCellArc DataManager::makeDelayArc(idb::LibArcSet* lib_arc_set)
{
  idb::LibArc* lib_arc = lib_arc_set->front();
  TimingCellArc timing_cell_arc;
  timing_cell_arc.set_source_port(lib_arc->get_src_port());
  timing_cell_arc.set_sink_port(lib_arc->get_snk_port());
  double delay = lib_arc->isDelayArc() ? lib_arc->getDelayOrConstrainCheckNs(idb::TransType::kRise, 0.0, 0.0) : 0.0;
  timing_cell_arc.set_delay(delay);
  timing_cell_arc.set_delay_max(delay);
  timing_cell_arc.set_delay_min(delay);
  timing_cell_arc.set_timing_arc_list(makeTimingArcList(lib_arc_set));
  timing_cell_arc.set_is_clock_arc(lib_arc->isRisingTriggerArc() || lib_arc->isFallingTriggerArc());
  timing_cell_arc.set_is_disable_arc(lib_arc->isDisableArc());
  return timing_cell_arc;
}

void DataManager::updateClearPresetArc(TimingCell& timing_cell, idb::LibArc* lib_arc)
{
  if (lib_arc->get_timing_type() == idb::LibArc::TimingType::kClear) {
    timing_cell.set_has_clear_arc(true);
  } else if (lib_arc->get_timing_type() == idb::LibArc::TimingType::kPreset) {
    timing_cell.set_has_preset_arc(true);
  }
}

TimingCheckArc DataManager::makeCheckArc(idb::LibArcSet* lib_arc_set)
{
  idb::LibArc* lib_arc = lib_arc_set->front();
  TimingCheckArc timing_check_arc;
  timing_check_arc.set_clock_port(lib_arc->get_src_port());
  timing_check_arc.set_data_port(lib_arc->get_snk_port());
  timing_check_arc.set_check_type(getTimingCheckType(lib_arc));
  if (lib_arc->isCheckArc()) {
    timing_check_arc.set_check_time(lib_arc->getDelayOrConstrainCheckNs(idb::TransType::kRise, 0.0, 0.0));
  }
  timing_check_arc.set_timing_arc_list(makeTimingArcList(lib_arc_set));
  timing_check_arc.set_clock_trans_type(getCheckTransType(lib_arc));
  if (timing_check_arc.get_check_type() == TimingCheckType::kSetup) {
    timing_check_arc.set_setup_time(timing_check_arc.get_check_time());
  }
  return timing_check_arc;
}

std::vector<TimingArc> DataManager::makeTimingArcList(idb::LibArcSet* lib_arc_set)
{
  std::vector<TimingArc> timing_arc_list;
  int32_t arc_idx = 0;
  for (std::unique_ptr<idb::LibArc>& lib_arc : lib_arc_set->get_arcs()) {
    if (lib_arc->isDisableArc()) {
      continue;
    }
    TimingArc timing_arc = makeTimingArc(lib_arc.get());
    timing_arc.set_arc_idx(arc_idx++);
    timing_arc_list.push_back(timing_arc);
  }
  return timing_arc_list;
}

TimingArc DataManager::makeTimingArc(idb::LibArc* lib_arc)
{
  TimingArc timing_arc;
  idb::LibLibrary* lib_library = lib_arc->get_owner_cell()->get_owner_lib();
  timing_arc.set_sense(getTimingArcSense(lib_arc));
  timing_arc.set_trigger_trans_type(getTriggerTransType(lib_arc));
  timing_arc.set_check_trans_type(getCheckTransType(lib_arc));
  timing_arc.set_library_name(lib_library->get_lib_name());
  timing_arc.set_sdf_cond(lib_arc->get_sdf_cond());
  timing_arc.set_time_unit_scale(getLibTimeUnitScale(lib_library));
  timing_arc.set_cap_unit_scale(getLibCapUnitScale(lib_library));
  timing_arc.set_slew_derate(lib_library->get_slew_derate_from_library());
  timing_arc.set_slew_lower_threshold_pct_rise(lib_library->get_slew_lower_threshold_pct_rise());
  timing_arc.set_slew_upper_threshold_pct_rise(lib_library->get_slew_upper_threshold_pct_rise());
  timing_arc.set_slew_lower_threshold_pct_fall(lib_library->get_slew_lower_threshold_pct_fall());
  timing_arc.set_slew_upper_threshold_pct_fall(lib_library->get_slew_upper_threshold_pct_fall());
  timing_arc.set_input_threshold_pct_rise(lib_library->get_input_threshold_pct_rise());
  timing_arc.set_output_threshold_pct_rise(lib_library->get_output_threshold_pct_rise());
  timing_arc.set_input_threshold_pct_fall(lib_library->get_input_threshold_pct_fall());
  timing_arc.set_output_threshold_pct_fall(lib_library->get_output_threshold_pct_fall());
  makeTimingArcTable(timing_arc, lib_arc);
  return timing_arc;
}

void DataManager::makeTimingArcTable(TimingArc& timing_arc, idb::LibArc* lib_arc)
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
      timing_arc.get_delay_table_map()[TransType::kRise] = makeTimingTable(rise_delay_table);
    }
    if (fall_delay_table != nullptr) {
      timing_arc.get_delay_table_map()[TransType::kFall] = makeTimingTable(fall_delay_table);
    }
    if (rise_slew_table != nullptr) {
      timing_arc.get_slew_table_map()[TransType::kRise] = makeTimingTable(rise_slew_table);
    }
    if (fall_slew_table != nullptr) {
      timing_arc.get_slew_table_map()[TransType::kFall] = makeTimingTable(fall_slew_table);
    }
    return;
  }
  if (!table_model->isCheckModel()) {
    return;
  }
  idb::LibTable* rise_check_table = table_model->getTable(CAST_TYPE_TO_INDEX(idb::LibTable::TableType::kRiseConstrain));
  idb::LibTable* fall_check_table = table_model->getTable(CAST_TYPE_TO_INDEX(idb::LibTable::TableType::kFallConstrain));
  if (rise_check_table != nullptr) {
    timing_arc.get_check_table_map()[TransType::kRise] = makeTimingTable(rise_check_table);
  }
  if (fall_check_table != nullptr) {
    timing_arc.get_check_table_map()[TransType::kFall] = makeTimingTable(fall_check_table);
  }
}

TimingTable DataManager::makeTimingTable(idb::LibTable* lib_table)
{
  TimingTable timing_table;
  timing_table.set_variable_type1(getTimingTableVariableType(lib_table, true));
  timing_table.set_variable_type2(getTimingTableVariableType(lib_table, false));
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

TimingTableVariableType DataManager::getTimingTableVariableType(idb::LibTable* lib_table, bool is_first_variable)
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
  if (*variable == idb::LibLutTableTemplate::Variable::INPUT_NET_TRANSITION
      || *variable == idb::LibLutTableTemplate::Variable::RELATED_PIN_TRANSITION
      || *variable == idb::LibLutTableTemplate::Variable::INPUT_TRANSITION_TIME) {
    return TimingTableVariableType::kInputTransition;
  }
  return TimingTableVariableType::kNone;
}

double DataManager::getLibTimeUnitScale(idb::LibLibrary* lib_library)
{
  if (lib_library->get_time_unit() == idb::TimeUnit::kPS) {
    return 1e3;
  }
  if (lib_library->get_time_unit() == idb::TimeUnit::kFS) {
    return 1e6;
  }
  return 1.0;
}

double DataManager::getLibCapUnitScale(idb::LibLibrary* lib_library)
{
  if (lib_library->get_cap_unit() == idb::CapacitiveUnit::kFF) {
    return static_cast<double>(idb::g_pf2ff);
  }
  return 1.0;
}

TimingArcSense DataManager::getTimingArcSense(idb::LibArc* lib_arc)
{
  if (lib_arc->isNegativeArc()) {
    return TimingArcSense::kNegative;
  }
  if (lib_arc->isNonUnateArc()) {
    return TimingArcSense::kNonUnate;
  }
  return TimingArcSense::kPositive;
}

TransType DataManager::getTriggerTransType(idb::LibArc* lib_arc)
{
  if (lib_arc->isFallingTriggerArc()) {
    return TransType::kFall;
  }
  if (lib_arc->isRisingTriggerArc()) {
    return TransType::kRise;
  }
  return TransType::kNone;
}

TransType DataManager::getCheckTransType(idb::LibArc* lib_arc)
{
  if (lib_arc->isFallingEdgeCheck()) {
    return TransType::kFall;
  }
  return TransType::kRise;
}

TimingCheckType DataManager::getTimingCheckType(idb::LibArc* lib_arc)
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

void DataManager::updateTimingCell(TimingCell& timing_cell)
{
  if (!timing_cell.get_check_arc_list().empty()) {
    timing_cell.set_is_sequential(true);
  }
}

void DataManager::buildInstanceList()
{
  makeInstanceList();
}

void DataManager::makeInstanceList()
{
  Database& database = _database;
  for (std::pair<const std::string, Instance>& instance_pair : database.get_instance_map()) {
    instance_pair.second.get_pin_name_list().clear();
  }

  for (std::pair<const std::string, Pin>& pin_pair : database.get_pin_map()) {
    Pin& pin = pin_pair.second;
    if (!isInstancePin(pin)) {
      continue;
    }

    makeUniqueName(database.get_instance_map()[pin.get_instance_name()].get_pin_name_list(), pin_pair.first);
  }
}

void DataManager::buildInstanceTimingInfo()
{
  Database& database = _database;
  for (std::pair<const std::string, Instance>& instance_pair : database.get_instance_map()) {
    makeInstanceTimingInfo(instance_pair.second);
  }
}

void DataManager::makeInstanceTimingInfo(Instance& instance)
{
  Database& database = _database;
  std::map<std::string, TimingCell>& timing_cell_map = database.get_timing_library().get_cell_map();
  if (timing_cell_map.count(instance.get_cell_name()) == 0) {
    return;
  }

  TimingCell& timing_cell = timing_cell_map[instance.get_cell_name()];
  instance.set_is_sequential(timing_cell.get_is_sequential());
  instance.set_has_clear_arc(timing_cell.get_has_clear_arc());
  instance.set_has_preset_arc(timing_cell.get_has_preset_arc());
  TimingCellArc* clock_to_q_arc = findClockToQArc(timing_cell);
  if (clock_to_q_arc != nullptr) {
    instance.set_output_pin_name(getInstancePinName(instance, clock_to_q_arc->get_sink_port()));
    instance.set_clock_to_q_delay(clock_to_q_arc->get_delay());
    instance.set_clock_to_q_arc(*clock_to_q_arc);
  } else {
    instance.set_output_pin_name(findOutputPinName(instance, timing_cell));
  }
  if (timing_cell.get_setup_arc_list().empty()) {
    return;
  }

  TimingCheckArc& setup_arc = timing_cell.get_setup_arc_list().front();
  instance.set_clock_pin_name(getInstancePinName(instance, setup_arc.get_clock_port()));
  instance.set_data_pin_name(getInstancePinName(instance, setup_arc.get_data_port()));
  instance.set_setup_time(setup_arc.get_setup_time());
  instance.get_check_arc_list().clear();
  for (TimingCheckArc& timing_check_arc : timing_cell.get_check_arc_list()) {
    instance.get_check_arc_list().push_back(makeInstanceTimingCheckArc(instance, timing_check_arc));
  }
}

TimingCheckArc DataManager::makeInstanceTimingCheckArc(Instance& instance, TimingCheckArc& timing_check_arc)
{
  TimingCheckArc instance_timing_check_arc;
  instance_timing_check_arc.set_clock_port(getInstancePinName(instance, timing_check_arc.get_clock_port()));
  instance_timing_check_arc.set_data_port(getInstancePinName(instance, timing_check_arc.get_data_port()));
  instance_timing_check_arc.set_setup_time(timing_check_arc.get_setup_time());
  instance_timing_check_arc.set_check_type(timing_check_arc.get_check_type());
  instance_timing_check_arc.set_check_time(timing_check_arc.get_check_time());
  instance_timing_check_arc.set_timing_arc_list(timing_check_arc.get_timing_arc_list());
  instance_timing_check_arc.set_clock_trans_type(timing_check_arc.get_clock_trans_type());
  return instance_timing_check_arc;
}

TimingCellArc* DataManager::findClockToQArc(TimingCell& timing_cell)
{
  for (TimingCellArc& timing_cell_arc : timing_cell.get_cell_arc_list()) {
    if (timing_cell_arc.get_is_clock_arc()) {
      return &timing_cell_arc;
    }
  }
  return nullptr;
}

std::string DataManager::getInstancePinName(Instance& instance, std::string& port_name)
{
  return instance.get_instance_name() + ":" + port_name;
}

std::string DataManager::findOutputPinName(Instance& instance, TimingCell& timing_cell)
{
  for (auto& [port_name, timing_cell_port] : timing_cell.get_port_map()) {
    if (timing_cell_port.get_is_output() && !timing_cell_port.get_is_clock()) {
      return getInstancePinName(instance, timing_cell_port.get_port_name());
    }
  }
  return "";
}

bool DataManager::isInstancePin(Pin& pin)
{
  return !pin.get_is_port();
}

void DataManager::makeUniqueName(std::vector<std::string>& list, const std::string& value)
{
  if (!STAUTIL.exist(list, value)) {
    list.push_back(value);
  }
}

void DataManager::buildNetList()
{
  makeNetList();
}

void DataManager::makeNetList()
{
  Database& database = _database;
  for (std::pair<const std::string, Pin>& pin_pair : database.get_pin_map()) {
    pin_pair.second.get_net_name().clear();
  }

  for (std::pair<const std::string, Net>& net_pair : database.get_net_map()) {
    makeNet(net_pair.first, net_pair.second);
  }
}

void DataManager::makeNet(const std::string& net_name, Net& net)
{
  Database& database = _database;
  net.get_driver_pin().clear();
  net.get_driver_pin_list().clear();
  net.get_load_pin_list().clear();

  for (std::string& pin_name : net.get_pin_name_list()) {
    Pin& pin = database.get_pin_map()[pin_name];
    pin.set_net_name(net_name);
    makeUniqueName(net.get_load_pin_list(), pin_name);
  }
}

void DataManager::buildParasiticLibrary()
{
  Database& database = _database;
  database.get_parasitic_library().set_spef_file_path(dmInst->get_config().get_spef_path());
  database.get_parasitic_library().get_net_map().clear();
  spef::SpefReader* spef_reader = dmInst->get_spef_reader();
  if (spef_reader == nullptr || spef_reader->getSpefFile() == nullptr) {
    return;
  }

  database.get_parasitic_library().set_capacitive_unit(spef_reader->getSpefCapUnit());
  database.get_parasitic_library().set_resistance_unit(spef_reader->getSpefResUnit());

  spef::Exchange* spef_file = spef_reader->getSpefFile();
  for (spef::Net& spef_net : spef_file->nets) {
    buildParasiticNetMap(spef_net);
  }
}

void DataManager::buildParasiticNetMap(spef::Net& spef_net)
{
  Database& database = _database;
  ParasiticNet parasitic_net;
  parasitic_net.set_net_name(spef_net.name);
  parasitic_net.set_lumped_capacitance(getParasiticCapacitance(spef_net.lcap));
  for (spef::ConnEntry& spef_conn : spef_net.conns) {
    makeParasiticConnection(parasitic_net, spef_conn);
  }
  for (spef::ResCap& spef_res : spef_net.ress) {
    makeParasiticResistance(parasitic_net, spef_res);
  }
  for (spef::ResCap& spef_cap : spef_net.caps) {
    makeParasiticCapacitance(parasitic_net, spef_cap);
  }
  database.get_parasitic_library().get_net_map()[parasitic_net.get_net_name()] = parasitic_net;
}

void DataManager::makeParasiticConnection(ParasiticNet& parasitic_net, spef::ConnEntry& spef_conn)
{
  ParasiticNode& parasitic_node = getParasiticNode(parasitic_net, spef_conn.pin_port_name);
  parasitic_node.set_x(spef_conn.coordinate.x);
  parasitic_node.set_y(spef_conn.coordinate.y);
}

void DataManager::makeParasiticCapacitance(ParasiticNet& parasitic_net, spef::ResCap& spef_cap)
{
  double capacitance = getParasiticCapacitance(spef_cap.res_or_cap);
  ParasiticNode& parasitic_node = getParasiticNode(parasitic_net, spef_cap.node1);
  parasitic_node.set_capacitance(parasitic_node.get_capacitance() + capacitance);

  if (spef_cap.node2.empty()) {
    return;
  }
  if (parasitic_net.get_node_map().count(spef_cap.node2) > 0) {
    ParasiticNode& coupled_node = parasitic_net.get_node_map()[spef_cap.node2];
    coupled_node.set_capacitance(coupled_node.get_capacitance() + capacitance);
  }
}

void DataManager::makeParasiticResistance(ParasiticNet& parasitic_net, spef::ResCap& spef_res)
{
  ParasiticResistor parasitic_resistor;
  parasitic_resistor.set_source_node(spef_res.node1);
  parasitic_resistor.set_sink_node(spef_res.node2);
  parasitic_resistor.set_resistance(getParasiticResistance(spef_res.res_or_cap));
  parasitic_net.get_resistor_list().push_back(parasitic_resistor);
  getParasiticNode(parasitic_net, spef_res.node1);
  getParasiticNode(parasitic_net, spef_res.node2);
}

double DataManager::getParasiticCapacitance(double spef_capacitance)
{
  Database& database = _database;
  std::string spef_unit = database.get_parasitic_library().get_capacitive_unit();
  std::string target_unit = "PF";
  return spef_capacitance * getSpefUnitScale(spef_unit, target_unit);
}

double DataManager::getParasiticResistance(double spef_resistance)
{
  Database& database = _database;
  std::string spef_unit = database.get_parasitic_library().get_resistance_unit();
  std::string target_unit = "OHM";
  return spef_resistance * getSpefUnitScale(spef_unit, target_unit);
}

double DataManager::getSpefUnitScale(std::string& spef_unit, std::string& target_unit)
{
  double unit_value = 1.0;
  std::string unit_name;
  std::stringstream spef_unit_stream(spef_unit);
  spef_unit_stream >> unit_value >> unit_name;
  std::transform(unit_name.begin(), unit_name.end(), unit_name.begin(), ::toupper);
  std::transform(target_unit.begin(), target_unit.end(), target_unit.begin(), ::toupper);

  if (unit_name == target_unit) {
    return unit_value;
  }
  if (unit_name == "FF" && target_unit == "PF") {
    return unit_value * 1E-3;
  }
  if (unit_name == "PF" && target_unit == "FF") {
    return unit_value * 1E3;
  }
  if (unit_name == "F" && target_unit == "PF") {
    return unit_value * 1E12;
  }
  if (unit_name == "PF" && target_unit == "F") {
    return unit_value * 1E-12;
  }
  if (unit_name == "KOHM" && target_unit == "OHM") {
    return unit_value * 1E3;
  }
  if (unit_name == "OHM" && target_unit == "KOHM") {
    return unit_value * 1E-3;
  }
  return unit_value;
}

ParasiticNode& DataManager::getParasiticNode(ParasiticNet& parasitic_net, const std::string& node_name)
{
  ParasiticNode& parasitic_node = parasitic_net.get_node_map()[node_name];
  parasitic_node.set_node_name(node_name);
  return parasitic_node;
}

void DataManager::readConstraint()
{
  Database& database = _database;
  std::string sdc_file_path = dmInst->get_config().get_sdc_path();
  database.get_timing_constraint().set_sdc_file_path(sdc_file_path);
  database.get_timing_constraint().get_clock_map().clear();
  database.get_timing_constraint().get_port_constraint_map().clear();
  if (sdc_file_path.empty()) {
    return;
  }

  std::vector<std::vector<std::string>> command_list = readCommandList(sdc_file_path);
  for (std::vector<std::string>& token_list : command_list) {
    parseCommand(token_list);
  }
}

std::vector<std::vector<std::string>> DataManager::readCommandList(std::string& sdc_file_path)
{
  std::ifstream sdc_file(sdc_file_path);
  std::string content;
  std::string line;
  while (std::getline(sdc_file, line)) {
    std::string command_line = removeComment(line);
    bool is_continue = !command_line.empty() && command_line.back() == '\\';
    if (is_continue) {
      command_line.pop_back();
    }
    content += command_line;
    content += is_continue ? " " : "\n";
  }

  std::vector<std::string> token_list = tokenizeSdc(content);
  std::vector<std::vector<std::string>> command_list;
  std::vector<std::string> command_token_list;
  for (std::string& token : token_list) {
    if (token == "\n" || token == ";") {
      if (!command_token_list.empty()) {
        command_list.push_back(command_token_list);
        command_token_list.clear();
      }
    } else {
      command_token_list.push_back(token);
    }
  }
  if (!command_token_list.empty()) {
    command_list.push_back(command_token_list);
  }
  return resolveCommandList(command_list);
}

std::vector<std::vector<std::string>> DataManager::resolveCommandList(std::vector<std::vector<std::string>>& command_list)
{
  std::map<std::string, std::string> variable_map;
  std::vector<std::vector<std::string>> resolved_command_list;
  for (std::vector<std::string>& token_list : command_list) {
    if (token_list.empty()) {
      continue;
    }
    if (token_list.front() == "set") {
      updateVariableMap(token_list, variable_map);
      continue;
    }
    std::vector<std::string> resolved_token_list = resolveCommandTokenList(token_list, variable_map);
    if (!resolved_token_list.empty()) {
      resolved_command_list.push_back(resolved_token_list);
    }
  }
  return resolved_command_list;
}

std::vector<std::string> DataManager::resolveCommandTokenList(std::vector<std::string>& token_list, std::map<std::string, std::string>& variable_map)
{
  std::vector<std::string> resolved_token_list;
  for (std::size_t i = 0; i < token_list.size(); i++) {
    if (token_list[i].empty()) {
      continue;
    }
    if (token_list[i].front() == '[') {
      resolved_token_list.push_back(resolveBracketCommand(token_list, i, variable_map));
      continue;
    }
    resolved_token_list.push_back(resolveVariableToken(token_list[i], variable_map));
  }
  return resolved_token_list;
}

void DataManager::updateVariableMap(std::vector<std::string>& token_list, std::map<std::string, std::string>& variable_map)
{
  if (token_list.size() < 3) {
    return;
  }
  std::string variable_name = token_list[1];
  std::string variable_value;
  if (!token_list[2].empty() && token_list[2].front() == '[') {
    std::size_t token_idx = 2;
    variable_value = resolveBracketCommand(token_list, token_idx, variable_map);
  } else {
    variable_value = getTokenListString(token_list, 2);
    variable_value = resolveVariableToken(variable_value, variable_map);
  }
  variable_map[variable_name] = variable_value;
}

std::string DataManager::resolveBracketCommand(std::vector<std::string>& token_list, std::size_t& token_idx, std::map<std::string, std::string>& variable_map)
{
  std::vector<std::string> bracket_token_list = getBracketTokenList(token_list, token_idx, variable_map);
  if (bracket_token_list.empty()) {
    return "";
  }
  if (bracket_token_list.front() == "expr") {
    return evalExpr(bracket_token_list);
  }
  if (bracket_token_list.front() == "get_ports" || bracket_token_list.front() == "get_pins" || bracket_token_list.front() == "get_clocks") {
    return getTokenListString(bracket_token_list, 1);
  }
  return getTokenListString(bracket_token_list, 0);
}

std::vector<std::string> DataManager::getBracketTokenList(std::vector<std::string>& token_list, std::size_t& token_idx,
                                                          std::map<std::string, std::string>& variable_map)
{
  std::vector<std::string> bracket_token_list;
  for (std::size_t i = token_idx; i < token_list.size(); i++) {
    std::string token = token_list[i];
    std::size_t left_bracket_num = std::count(token.begin(), token.end(), '[');
    std::size_t right_bracket_num = std::count(token.begin(), token.end(), ']');
    bool is_end = right_bracket_num > left_bracket_num;
    if (i == token_idx && !token.empty() && token.front() == '[') {
      token.erase(token.begin());
    }
    if (is_end) {
      token.pop_back();
    }
    if (!token.empty()) {
      bracket_token_list.push_back(resolveVariableToken(token, variable_map));
    }
    token_idx = i;
    if (is_end) {
      break;
    }
  }
  return bracket_token_list;
}

std::string DataManager::evalExpr(std::vector<std::string>& expr_token_list)
{
  return getExprValueString(calcExprValue(expr_token_list));
}

double DataManager::calcExprValue(std::vector<std::string>& expr_token_list)
{
  std::vector<double> value_list;
  std::vector<std::string> operator_list;
  for (std::size_t i = 1; i < expr_token_list.size(); i++) {
    if (isExprOperator(expr_token_list[i])) {
      operator_list.push_back(expr_token_list[i]);
    } else {
      value_list.push_back(std::stod(expr_token_list[i]));
    }
  }
  calcExprMulDiv(value_list, operator_list);
  if (value_list.empty()) {
    return 0.0;
  }
  double result = value_list.front();
  for (std::size_t i = 0; i < operator_list.size() && i + 1 < value_list.size(); i++) {
    if (operator_list[i] == "+") {
      result += value_list[i + 1];
    } else if (operator_list[i] == "-") {
      result -= value_list[i + 1];
    }
  }
  return result;
}

void DataManager::calcExprMulDiv(std::vector<double>& value_list, std::vector<std::string>& operator_list)
{
  for (std::size_t i = 0; i < operator_list.size() && i + 1 < value_list.size();) {
    if (operator_list[i] == "*" || operator_list[i] == "/") {
      if (operator_list[i] == "*") {
        value_list[i] *= value_list[i + 1];
      } else {
        value_list[i] /= value_list[i + 1];
      }
      value_list.erase(value_list.begin() + i + 1);
      operator_list.erase(operator_list.begin() + i);
      continue;
    }
    i++;
  }
}

std::string DataManager::getExprValueString(const double value)
{
  std::ostringstream oss;
  oss << std::setprecision(15) << value;
  return oss.str();
}

bool DataManager::isExprOperator(std::string& token)
{
  return token == "+" || token == "-" || token == "*" || token == "/";
}

std::string DataManager::resolveVariableToken(std::string token, std::map<std::string, std::string>& variable_map)
{
  if (token.empty()) {
    return token;
  }
  std::string resolved_token;
  for (std::size_t i = 0; i < token.size(); i++) {
    if (token[i] != '$') {
      resolved_token.push_back(token[i]);
      continue;
    }
    std::string variable_name;
    i++;
    while (i < token.size() && (std::isalnum(static_cast<unsigned char>(token[i])) || token[i] == '_')) {
      variable_name.push_back(token[i]);
      i++;
    }
    i--;
    if (variable_map.count(variable_name) > 0) {
      resolved_token += variable_map[variable_name];
    } else {
      resolved_token += "$" + variable_name;
    }
  }
  return resolved_token;
}

std::string DataManager::getTokenListString(std::vector<std::string>& token_list, std::size_t begin_idx)
{
  std::string token_list_string;
  for (std::size_t i = begin_idx; i < token_list.size(); i++) {
    if (!token_list_string.empty()) {
      token_list_string += " ";
    }
    token_list_string += token_list[i];
  }
  return token_list_string;
}

std::vector<std::string> DataManager::tokenizeSdc(std::string& content)
{
  std::vector<std::string> token_list;
  std::string token;
  bool in_brace = false;
  bool in_quote = false;
  for (char ch : content) {
    if (in_brace) {
      if (ch == '}') {
        token_list.push_back(token);
        token.clear();
        in_brace = false;
      } else {
        token.push_back(ch);
      }
      continue;
    }
    if (in_quote) {
      if (ch == '"') {
        token_list.push_back(token);
        token.clear();
        in_quote = false;
      } else {
        token.push_back(ch);
      }
      continue;
    }
    if (ch == '{') {
      if (!token.empty()) {
        token_list.push_back(token);
        token.clear();
      }
      in_brace = true;
    } else if (ch == '"') {
      if (!token.empty()) {
        token_list.push_back(token);
        token.clear();
      }
      in_quote = true;
    } else if (std::isspace(static_cast<unsigned char>(ch)) || ch == ';') {
      if (!token.empty()) {
        token_list.push_back(token);
        token.clear();
      }
      if (ch == '\n' || ch == ';') {
        token_list.emplace_back(ch == '\n' ? "\n" : ";");
      }
    } else {
      token.push_back(ch);
    }
  }
  if (!token.empty()) {
    token_list.push_back(token);
  }
  return token_list;
}

std::string DataManager::removeComment(std::string& line)
{
  std::string result;
  bool in_brace = false;
  bool in_quote = false;
  for (char ch : line) {
    if (ch == '{' && !in_quote) {
      in_brace = true;
    } else if (ch == '}' && !in_quote) {
      in_brace = false;
    } else if (ch == '"' && !in_brace) {
      in_quote = !in_quote;
    }
    if (ch == '#' && !in_brace && !in_quote) {
      break;
    }
    result.push_back(ch);
  }
  return result;
}

void DataManager::parseCommand(std::vector<std::string>& token_list)
{
  if (token_list.empty()) {
    return;
  }
  if (token_list.front() == "create_clock") {
    parseCreateClock(token_list);
  } else if (token_list.front() == "set_input_delay") {
    parseSetInputDelay(token_list);
  } else if (token_list.front() == "set_output_delay") {
    parseSetOutputDelay(token_list);
  } else if (token_list.front() == "set_input_transition") {
    parseSetInputTransition(token_list);
  } else if (token_list.front() == "set_load") {
    parseSetLoad(token_list);
  }
}

void DataManager::parseCreateClock(std::vector<std::string>& token_list)
{
  TimingClock timing_clock;
  timing_clock.set_clock_name(getOptionValue(token_list, "-name"));
  timing_clock.set_period(getOptionDoubleValue(token_list, "-period", 0.0));
  std::vector<std::string> object_list = getObjectList(token_list);
  std::vector<std::string> source_list = resolveObjectList(object_list);
  if (timing_clock.get_clock_name().empty() && !source_list.empty()) {
    timing_clock.set_clock_name(source_list.front());
  }
  timing_clock.set_source_list(source_list);
  timing_clock.set_rise_edge(0.0);
  timing_clock.set_fall_edge(timing_clock.get_period() / 2.0);
  updateClock(timing_clock);
}

void DataManager::parseSetInputDelay(std::vector<std::string>& token_list)
{
  const double delay_value = getCommandDoubleValue(token_list);
  const bool set_min = hasOption(token_list, "-min");
  const bool set_max = hasOption(token_list, "-max");
  std::string clock_name = getClockName(token_list);
  std::vector<std::string> object_list = getObjectList(token_list);
  for (std::string& port_name : resolveObjectList(object_list)) {
    TimingPortConstraint& port_constraint = getPortConstraint(port_name);
    port_constraint.set_clock_name(clock_name);
    if (set_min && !set_max) {
      port_constraint.set_input_delay_min(delay_value);
      port_constraint.set_has_input_delay_min(true);
    } else if (set_max && !set_min) {
      port_constraint.set_input_delay_max(delay_value);
      port_constraint.set_has_input_delay_max(true);
    } else {
      port_constraint.set_input_delay_min(delay_value);
      port_constraint.set_input_delay_max(delay_value);
      port_constraint.set_has_input_delay_min(true);
      port_constraint.set_has_input_delay_max(true);
    }
  }
}

void DataManager::parseSetOutputDelay(std::vector<std::string>& token_list)
{
  const double delay_value = getCommandDoubleValue(token_list);
  const bool set_min = hasOption(token_list, "-min");
  const bool set_max = hasOption(token_list, "-max");
  std::string clock_name = getClockName(token_list);
  std::vector<std::string> object_list = getObjectList(token_list);
  for (std::string& port_name : resolveObjectList(object_list)) {
    TimingPortConstraint& port_constraint = getPortConstraint(port_name);
    port_constraint.set_clock_name(clock_name);
    if (set_min && !set_max) {
      port_constraint.set_output_delay_min(delay_value);
      port_constraint.set_has_output_delay_min(true);
    } else if (set_max && !set_min) {
      port_constraint.set_output_delay_max(delay_value);
      port_constraint.set_has_output_delay_max(true);
    } else {
      port_constraint.set_output_delay_min(delay_value);
      port_constraint.set_output_delay_max(delay_value);
      port_constraint.set_has_output_delay_min(true);
      port_constraint.set_has_output_delay_max(true);
    }
  }
}

void DataManager::parseSetInputTransition(std::vector<std::string>& token_list)
{
  const double transition_value = getCommandDoubleValue(token_list);
  std::vector<std::string> object_list = getObjectList(token_list);
  for (std::string& port_name : resolveObjectList(object_list)) {
    TimingPortConstraint& port_constraint = getPortConstraint(port_name);
    port_constraint.set_input_transition(transition_value);
    port_constraint.set_has_input_transition(true);
  }
}

void DataManager::parseSetLoad(std::vector<std::string>& token_list)
{
  const double load_value = getCommandDoubleValue(token_list);
  std::vector<std::string> object_list = getObjectList(token_list);
  for (std::string& port_name : resolveObjectList(object_list)) {
    TimingPortConstraint& port_constraint = getPortConstraint(port_name);
    port_constraint.set_load(load_value);
    port_constraint.set_has_load(true);
  }
}

double DataManager::getCommandDoubleValue(std::vector<std::string>& token_list)
{
  for (size_t i = 1; i < token_list.size(); i++) {
    if (token_list[i].empty() || token_list[i].front() == '-') {
      if (token_list[i] == "-clock" || token_list[i] == "-name") {
        i++;
      }
      continue;
    }
    char* end = nullptr;
    double value = std::strtod(token_list[i].c_str(), &end);
    if (end != token_list[i].c_str() && *end == '\0') {
      return value;
    }
  }
  return 0.0;
}

std::string DataManager::getOptionValue(std::vector<std::string>& token_list, const std::string& option)
{
  for (size_t i = 0; i + 1 < token_list.size(); i++) {
    if (token_list[i] == option) {
      return token_list[i + 1];
    }
  }
  return "";
}

double DataManager::getOptionDoubleValue(std::vector<std::string>& token_list, const std::string& option, double default_value)
{
  std::string option_value = getOptionValue(token_list, option);
  if (option_value.empty()) {
    return default_value;
  }
  return std::stod(option_value);
}

bool DataManager::hasOption(std::vector<std::string>& token_list, const std::string& option)
{
  return STAUTIL.exist(token_list, option);
}

std::string DataManager::getClockName(std::vector<std::string>& token_list)
{
  for (std::size_t i = 0; i + 1 < token_list.size(); i++) {
    if (token_list[i] != "-clock") {
      continue;
    }
    if (isClockCollectionCommand(token_list[i + 1])) {
      return getCollectionName(token_list, i + 1);
    }
    std::string clock_name = token_list[i + 1];
    if (!clock_name.empty() && clock_name.back() == ']') {
      clock_name.pop_back();
    }
    return clock_name;
  }
  return "";
}

std::string DataManager::getCollectionName(std::vector<std::string>& token_list, std::size_t collection_idx)
{
  std::vector<std::string> name_list;
  for (std::size_t i = collection_idx + 1; i < token_list.size(); i++) {
    std::string object_name = token_list[i];
    bool is_end = false;
    if (object_name == "]") {
      break;
    }
    pushObjectName(name_list, object_name);
    if (is_end) {
      break;
    }
  }
  return name_list.empty() ? "" : name_list.front();
}

std::vector<std::string> DataManager::getObjectList(std::vector<std::string>& token_list)
{
  for (std::size_t i = 1; i < token_list.size(); i++) {
    if (!isCollectionCommand(token_list[i])) {
      continue;
    }

    std::vector<std::string> object_list;
    for (std::size_t j = i + 1; j < token_list.size(); j++) {
      std::string object_name = token_list[j];
      bool is_end = false;
      if (object_name == "]") {
        break;
      }
      pushObjectName(object_list, object_name);
      if (is_end) {
        break;
      }
    }
    return object_list;
  }

  for (auto iter = token_list.rbegin(); iter != token_list.rend(); ++iter) {
    std::size_t token_idx = std::distance(iter, token_list.rend()) - 1;
    if (!iter->empty() && iter->front() != '-' && !isCommandOptionValue(token_list, token_idx)) {
      std::vector<std::string> object_list;
      pushObjectName(object_list, *iter);
      return object_list;
    }
  }
  return {};
}

void DataManager::pushObjectName(std::vector<std::string>& object_list, std::string object_name)
{
  std::istringstream iss(object_name);
  std::string split_object_name;
  while (iss >> split_object_name) {
    object_list.push_back(getObjectName(split_object_name));
  }
}

std::string DataManager::getObjectName(std::string& object_name)
{
  if (!object_name.empty() && object_name.front() == '\\') {
    object_name.erase(object_name.begin());
  }
  return object_name;
}

bool DataManager::isCollectionCommand(std::string& token)
{
  return token == "[get_ports" || token == "get_ports" || token == "[get_pins" || token == "get_pins";
}

bool DataManager::isClockCollectionCommand(std::string& token)
{
  return token == "[get_clocks" || token == "get_clocks";
}

bool DataManager::isCommandOptionValue(std::vector<std::string>& token_list, std::size_t token_idx)
{
  if (token_idx == 0 || token_idx >= token_list.size()) {
    return false;
  }
  std::string& prev_token = token_list[token_idx - 1];
  if (prev_token == "-name" || prev_token == "-clock" || prev_token == "-period") {
    return true;
  }
  char* end = nullptr;
  std::strtod(token_list[token_idx].c_str(), &end);
  return end != token_list[token_idx].c_str() && *end == '\0';
}

std::vector<std::string> DataManager::resolveObjectList(std::vector<std::string>& object_list)
{
  Database& database = _database;
  std::vector<std::string> resolved_object_list;
  for (std::string& object_name : object_list) {
    std::string resolved_object_name = object_name;
    if (resolved_object_name.rfind("[get_ports", 0) == 0) {
      resolved_object_name = resolved_object_name.substr(10);
    }
    if (resolved_object_name.rfind("[get_pins", 0) == 0) {
      resolved_object_name = resolved_object_name.substr(9);
      std::replace(resolved_object_name.begin(), resolved_object_name.end(), '/', ':');
    }
    if (database.get_pin_map().count(resolved_object_name) > 0) {
      resolved_object_list.push_back(resolved_object_name);
      continue;
    }
    if (!resolved_object_name.empty() && resolved_object_name.back() == ']') {
      std::string trim_object_name = resolved_object_name;
      trim_object_name.pop_back();
      if (database.get_pin_map().count(trim_object_name) > 0) {
        resolved_object_list.push_back(trim_object_name);
        continue;
      }
    }
    std::replace(resolved_object_name.begin(), resolved_object_name.end(), '/', ':');
    if (database.get_pin_map().count(resolved_object_name) > 0) {
      resolved_object_list.push_back(resolved_object_name);
    }
  }
  return resolved_object_list;
}

void DataManager::updateClock(TimingClock& timing_clock)
{
  Database& database = _database;
  if (timing_clock.get_clock_name().empty()) {
    return;
  }
  database.get_timing_constraint().get_clock_map()[timing_clock.get_clock_name()] = timing_clock;
}

TimingPortConstraint& DataManager::getPortConstraint(const std::string& port_name)
{
  Database& database = _database;
  TimingPortConstraint& port_constraint = database.get_timing_constraint().get_port_constraint_map()[port_name];
  port_constraint.set_port_name(port_name);
  return port_constraint;
}

void DataManager::printConfig()
{
  /////////////////////////////////////////////
  // **********        STA        ********** //
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(0), "STA_CONFIG_INPUT");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "temp_directory_path");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), _config.temp_directory_path);
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "thread_number");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), _config.thread_number);
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "path_report_number");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), _config.path_report_number);
  // **********        STA        ********** //
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(0), "STA_CONFIG_BUILD");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "log_file_path");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), _config.log_file_path);
  // **********    DataManager    ********** //
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "DataManager");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), "dm_temp_directory_path");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(3), _config.dm_temp_directory_path);
  // **********   GraphBuilder    ********** //
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "GraphBuilder");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), "gb_temp_directory_path");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(3), _config.gb_temp_directory_path);
  // ********* DelayCalculator   ********* //
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "DelayCalculator");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), "dc_temp_directory_path");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(3), _config.dc_temp_directory_path);
  // ******** ClockPropagator    ********* //
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "ClockPropagator");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), "cp_temp_directory_path");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(3), _config.cp_temp_directory_path);
  // ********* TimingPropagator   ********* //
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "TimingPropagator");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), "tp_temp_directory_path");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(3), _config.tp_temp_directory_path);
  // ********** TimingAnalyzer   ********* //
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "TimingAnalyzer");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), "ta_temp_directory_path");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(3), _config.ta_temp_directory_path);
  // ******* TimingCharacterizer ******* //
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "TimingCharacterizer");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), "tc_temp_directory_path");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(3), _config.tc_temp_directory_path);
  // **********  TimingReporter   ********** //
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "TimingReporter");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), "tr_temp_directory_path");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(3), _config.tr_temp_directory_path);
  /////////////////////////////////////////////
}

void DataManager::printDatabase()
{
  std::size_t port_num = 0;
  for (std::pair<const std::string, Pin>& pin_pair : _database.get_pin_map()) {
    if (pin_pair.second.get_is_port()) {
      port_num++;
    }
  }
  /////////////////////////////////////////////
  // **********        STA        ********** //
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(0), "STA_DATABASE");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "design_name");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), _database.get_design_name());
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "instance_num");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), _database.get_instance_map().size());
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "port_num");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), port_num);
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "pin_num");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), _database.get_pin_map().size());
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "net_num");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), _database.get_net_map().size());
  /////////////////////////////////////////////
}

#endif

}  // namespace ista
