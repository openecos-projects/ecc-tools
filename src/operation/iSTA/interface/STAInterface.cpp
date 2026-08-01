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
#include "STAInterface.hpp"

#include "DataManager.hpp"
#include "DelayCalculator.hpp"
#include "ClockPropagator.hpp"
#include "GraphBuilder.hpp"
#include "Logger.hpp"
#include "Monitor.hpp"
#include "PowerAnalyzer.hpp"
#include "PowerPropagator.hpp"
#include "PowerReporter.hpp"
#include "SDFWriter.hpp"
#include "STAHeader.hpp"
#include "TCModel.hpp"
#include "TimingCharacterizer.hpp"
#include "TimingAnalyzer.hpp"
#include "TimingPropagator.hpp"
#include "TimingReporter.hpp"
#include "Utility.hpp"
#include "VcdParser.hh"
#include "idm.h"
#include "Lib.hh"
#include "spef/SpefParser.hh"

namespace ista {

// public

STAInterface& STAInterface::getInst()
{
  if (_sta_interface_instance == nullptr) {
    _sta_interface_instance = new STAInterface();
  }
  return *_sta_interface_instance;
}

void STAInterface::destroyInst()
{
  if (_sta_interface_instance != nullptr) {
    delete _sta_interface_instance;
    _sta_interface_instance = nullptr;
  }
}

#if 1  // 外部调用STA的API

#if 1  // iSTA

void STAInterface::initSTA(std::map<std::string, std::any> config_map)
{
  Logger::initInst();
  // clang-format off
  STALOG.info(Loc::current(), ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>");
  STALOG.info(Loc::current(), "_____ _______________________       _______________________ ________ ________ ");
  STALOG.info(Loc::current(), "___(_)__  ___/___  __/___    |      __  ___/___  __/___    |___  __ \\___  __/");
  STALOG.info(Loc::current(), "__  / _____ \\ __  /   __  /| |      _____ \\ __  /   __  /| |__  /_/ /__  /  ");
  STALOG.info(Loc::current(), "_  /  ____/ / _  /    _  ___ |      ____/ / _  /    _  ___ |_  _, _/ _  /     ");
  STALOG.info(Loc::current(), "/_/   /____/  /_/     /_/  |_|      /____/  /_/     /_/  |_|/_/ |_|  /_/      ");
  STALOG.info(Loc::current(), ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>");
  // clang-format on
  STALOG.printLogFilePath();
  //////////////////////////////////////////////////////
  //////////////////////////////////////////////////////
  //////////////////////////////////////////////////////
  Monitor monitor;
  STALOG.info(Loc::current(), "Starting...");

  DataManager::initInst();
  STADM.input(config_map);
  DelayCalculator::initInst();

  STALOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void STAInterface::runSTA()
{
  Monitor monitor;
  STALOG.info(Loc::current(), "Starting...");

  STADC.init();

  GraphBuilder::initInst();
  STAGB.build();
  GraphBuilder::destroyInst();

  ClockPropagator::initInst();
  STACP.propagate();
  ClockPropagator::destroyInst();

  TimingPropagator::initInst();
  STATP.propagate();
  TimingPropagator::destroyInst();

  PowerPropagator::initInst();
  STAPP.propagate();
  PowerPropagator::destroyInst();

  TimingAnalyzer::initInst();
  STATA.analyze();
  TimingAnalyzer::destroyInst();

  PowerAnalyzer::initInst();
  STAPA.analyze();
  PowerAnalyzer::destroyInst();

  TimingReporter::initInst();
  STATR.report();
  TimingReporter::destroyInst();

  PowerReporter::initInst();
  STAPR.report();
  PowerReporter::destroyInst();

  SDFWriter::initInst();
  STASW.write();
  SDFWriter::destroyInst();

  STADC.destroy();

  STALOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void STAInterface::extractLib()
{
  Monitor monitor;
  STALOG.info(Loc::current(), "Starting...");

  STADC.init();

  GraphBuilder::initInst();
  STAGB.build();
  GraphBuilder::destroyInst();

  ClockPropagator::initInst();
  STACP.propagate();
  ClockPropagator::destroyInst();

  TimingPropagator::initInst();
  STATP.propagate();
  TimingPropagator::destroyInst();

  TimingAnalyzer::initInst();
  STATA.analyze();
  TimingAnalyzer::destroyInst();

  TimingCharacterizer::initInst();
  STATC.characterize();
  TimingCharacterizer::destroyInst();

  STADC.destroy();

  STALOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void STAInterface::destroySTA()
{
  Monitor monitor;
  STALOG.info(Loc::current(), "Starting...");

  DelayCalculator::destroyInst();
  STADM.output();
  DataManager::destroyInst();

  STALOG.info(Loc::current(), "Completed", monitor.getStatsInfo());

  STALOG.printLogFilePath();
  // clang-format off
  STALOG.info(Loc::current(), ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>");
  STALOG.info(Loc::current(), "_____ _______________________       _______________________   ________________________  __  ");
  STALOG.info(Loc::current(), "___(_)__  ___/___  __/___    |      ___  ____/____  _/___  | / /____  _/__  ___/___  / / /  ");
  STALOG.info(Loc::current(), "__  / _____ \\ __  /   __  /| |      __  /_     __  /  __   |/ /  __  /  _____ \\ __  /_/ / ");
  STALOG.info(Loc::current(), "_  /  ____/ / _  /    _  ___ |      _  __/    __/ /   _  /|  /  __/ /   ____/ / _  __  /    ");
  STALOG.info(Loc::current(), "/_/   /____/  /_/     /_/  |_|      /_/       /___/   /_/ |_/   /___/   /____/  /_/ /_/     ");
  STALOG.info(Loc::current(), ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>");
  // clang-format on
  Logger::destroyInst();
}

#endif

#endif

#if 1  // STA调用外部的API

#if 1  // TopData

#if 1  // input

void STAInterface::input(std::map<std::string, std::any>& config_map)
{
  wrapConfig(config_map);
  wrapDatabase();
}

void STAInterface::wrapConfig(std::map<std::string, std::any>& config_map)
{
  /////////////////////////////////////////////
  STADM.getConfig().temp_directory_path = STAUTIL.getConfigValue<std::string>(config_map, "-temp_directory_path", "./sta_temp_directory");
  STADM.getConfig().thread_number = STAUTIL.getConfigValue<int32_t>(config_map, "-thread_number", 128);
  STADM.getConfig().output_timing_reports = STAUTIL.getConfigValue<int32_t>(config_map, "-output_timing_reports", 1);
  STADM.getConfig().output_timing_features = STAUTIL.getConfigValue<int32_t>(config_map, "-output_timing_features", 1);
  STADM.getConfig().timing_path_limit = STAUTIL.getConfigValue<int32_t>(config_map, "-timing_path_limit", 20);
  STADM.getConfig().timing_corner = STAUTIL.getConfigValue<std::string>(config_map, "-timing_corner", "");
  STADM.getConfig().is_path_report_number_specified = STAUTIL.exist(config_map, std::string("-max_paths"))
                                                       || STAUTIL.exist(config_map, std::string("-max_path"))
                                                       || STAUTIL.exist(config_map, std::string("-path_report_number"));
  STADM.getConfig().path_report_number = STAUTIL.getConfigValue<int32_t>(config_map, "-max_paths", 1);
  if (STAUTIL.exist(config_map, std::string("-max_path"))) {
    STADM.getConfig().path_report_number = std::any_cast<int32_t>(config_map["-max_path"]);
  }
  if (STAUTIL.exist(config_map, std::string("-path_report_number"))) {
    STADM.getConfig().path_report_number = std::any_cast<int32_t>(config_map["-path_report_number"]);
  }
  STADM.getConfig().endpoint_path_report_number = STAUTIL.getConfigValue<int32_t>(config_map, "-nworst", 1);
  if (!STADM.getConfig().is_path_report_number_specified && STADM.getConfig().endpoint_path_report_number > 1) {
    STADM.getConfig().path_report_number = STADM.getConfig().endpoint_path_report_number;
  }
  STADM.getConfig().timing_report_delay_type = STAUTIL.getConfigValue<std::string>(config_map, "-delay_type", "max");
  STADM.getConfig().timing_report_start_end_type = STAUTIL.getConfigValue<std::string>(config_map, "-start_end_type", "all");
  STADM.getConfig().has_timing_report_slack_lesser_than = STAUTIL.exist(config_map, std::string("-slack_lesser_than"));
  if (STADM.getConfig().has_timing_report_slack_lesser_than) {
    STADM.getConfig().timing_report_slack_lesser_than = std::any_cast<double>(config_map["-slack_lesser_than"]);
  }
  STADM.getConfig().has_timing_report_slack_greater_than = STAUTIL.exist(config_map, std::string("-slack_greater_than"));
  if (STADM.getConfig().has_timing_report_slack_greater_than) {
    STADM.getConfig().timing_report_slack_greater_than = std::any_cast<double>(config_map["-slack_greater_than"]);
  }
  omp_set_num_threads(std::max(STADM.getConfig().thread_number, 1));
  /////////////////////////////////////////////
}

void STAInterface::wrapDatabase()
{
  wrapDBInfo();
  wrapInstanceList();
  wrapPortList();
  wrapNetList();
  wrapTimingLibrary();
  wrapParasiticLibrary();
  wrapVcdActivity();
}

void STAInterface::wrapVcdActivity()
{
  Database& database = STADM.getDatabase();
  database.get_vcd_activity_map().clear();
  vcd::VcdReader* vcd_reader = dmInst->get_vcd_reader();
  if (vcd_reader == nullptr) {
    return;
  }
  for (std::pair<const std::string, vcd::VcdSignalActivity>& activity_pair : vcd_reader->get_signal_activity_map()) {
    std::string vcd_signal_name = activity_pair.first;
    std::string pin_name = wrapVcdPinName(vcd_signal_name);
    if (pin_name.empty() || database.get_pin_map().count(pin_name) == 0) {
      continue;
    }
    PowerActivity activity;
    activity.set_transition_density(activity_pair.second.get_transition_density());
    activity.set_static_probability(activity_pair.second.get_static_probability());
    activity.set_origin(PowerActivityOrigin::kVcd);
    activity.set_is_valid(true);
    database.get_vcd_activity_map()[pin_name] = activity;
  }
}

std::string STAInterface::wrapVcdPinName(std::string& vcd_signal_name)
{
  Database& database = STADM.getDatabase();
  std::string top_scope_path = database.get_design_name() + "/";
  std::size_t top_scope_pos = vcd_signal_name.find(top_scope_path);
  if (top_scope_pos == std::string::npos || (top_scope_pos != 0 && vcd_signal_name[top_scope_pos - 1] != '/')) {
    return "";
  }
  std::string pin_name = vcd_signal_name.substr(top_scope_pos + top_scope_path.size());
  std::replace(pin_name.begin(), pin_name.end(), '/', ':');
  return pin_name;
}

void STAInterface::wrapDBInfo()
{
  STADM.getDatabase().set_design_name(dmInst->get_idb_design()->get_design_name());
  wrapConstraintFilePath();
}

void STAInterface::wrapConstraintFilePath()
{
  STADM.getDatabase().get_timing_constraint().set_sdc_file_path(dmInst->get_config().get_sdc_path());
}

void STAInterface::wrapInstanceList()
{
  idb::IdbDesign* idb_design = dmInst->get_idb_design();
  for (idb::IdbInstance* idb_instance : idb_design->get_instance_list()->get_instance_list()) {
    wrapInstance(idb_instance);
    wrapInstancePinList(idb_instance);
  }
}

void STAInterface::wrapInstance(idb::IdbInstance* idb_instance)
{
  Instance instance;
  instance.set_instance_id(idb_instance->get_id());
  instance.set_instance_name(idb_instance->get_name());
  instance.set_cell_name(idb_instance->get_cell_master()->get_name());
  instance.set_is_io_cell(idb_instance->get_cell_master()->is_io_cell());
  STADM.getDatabase().get_instance_map()[instance.get_instance_name()] = instance;
}

void STAInterface::wrapInstancePinList(idb::IdbInstance* idb_instance)
{
  for (idb::IdbPin* idb_pin : idb_instance->get_pin_list()->get_pin_list()) {
    wrapInstancePin(idb_instance, idb_pin);
  }
}

void STAInterface::wrapInstancePin(idb::IdbInstance* idb_instance, idb::IdbPin* idb_pin)
{
  if (!wrapSignalConnectType(idb_pin->get_term()->get_type())) {
    return;
  }

  std::string full_name = wrapInstancePinName(idb_instance, idb_pin);
  Pin pin;
  pin.set_pin_name(idb_pin->get_pin_name());
  pin.set_full_name(full_name);
  pin.set_instance_name(idb_instance->get_name());
  pin.set_direction(wrapPinDirection(idb_pin->get_term()->get_direction()));
  wrapPinCoordinate(pin, idb_pin);
  STADM.getDatabase().get_pin_map()[full_name] = pin;
}

bool STAInterface::wrapSignalConnectType(idb::IdbConnectType connect_type)
{
  return connect_type == idb::IdbConnectType::kNone || connect_type == idb::IdbConnectType::kSignal || connect_type == idb::IdbConnectType::kClock
         || connect_type == idb::IdbConnectType::kReset || connect_type == idb::IdbConnectType::kScan || connect_type == idb::IdbConnectType::kTieOff;
}

std::string STAInterface::wrapInstancePinName(idb::IdbInstance* idb_instance, idb::IdbPin* idb_pin)
{
  return idb_instance->get_name() + ":" + idb_pin->get_pin_name();
}

PinDirection STAInterface::wrapPinDirection(idb::IdbConnectDirection idb_direction)
{
  switch (idb_direction) {
    case idb::IdbConnectDirection::kInput:
      return PinDirection::kInput;
    case idb::IdbConnectDirection::kOutput:
    case idb::IdbConnectDirection::kOutputTriState:
      return PinDirection::kOutput;
    case idb::IdbConnectDirection::kInOut:
      return PinDirection::kInout;
    default:
      STALOG.error(Loc::current(), "Unrecognized type!");
      break;
  }
  return PinDirection::kNone;
}

void STAInterface::wrapPinCoordinate(Pin& pin, idb::IdbPin* idb_pin)
{
  idb::IdbCoordinate<int32_t>* coordinate = idb_pin->get_average_coordinate();
  pin.set_x(coordinate->get_x());
  pin.set_y(coordinate->get_y());
}

void STAInterface::wrapPortList()
{
  idb::IdbDesign* idb_design = dmInst->get_idb_design();
  for (idb::IdbPin* idb_pin : idb_design->get_io_pin_list()->get_pin_list()) {
    wrapPortPin(idb_pin);
  }
}

void STAInterface::wrapPortPin(idb::IdbPin* idb_pin)
{
  if (!wrapSignalConnectType(idb_pin->get_term()->get_type())) {
    return;
  }

  std::string full_name = wrapPinName(idb_pin);
  Pin pin;
  pin.set_pin_name(idb_pin->get_pin_name());
  pin.set_full_name(full_name);
  pin.set_direction(wrapPinDirection(idb_pin->get_term()->get_direction()));
  pin.set_is_port(true);
  wrapPinCoordinate(pin, idb_pin);
  STADM.getDatabase().get_pin_map()[full_name] = pin;
}

std::string STAInterface::wrapPinName(idb::IdbPin* idb_pin)
{
  return idb_pin->get_pin_name();
}

void STAInterface::wrapNetList()
{
  idb::IdbDesign* idb_design = dmInst->get_idb_design();
  for (idb::IdbNet* idb_net : idb_design->get_net_list()->get_net_list()) {
    wrapNet(idb_net);
  }
}

void STAInterface::wrapNet(idb::IdbNet* idb_net)
{
  if (!wrapSignalConnectType(idb_net->get_connect_type())) {
    return;
  }

  Net net;
  net.set_net_name(idb_net->get_net_name());
  wrapNetPinList(idb_net, net);
  wrapNetToDatabase(net);
}

void STAInterface::wrapNetPinList(idb::IdbNet* idb_net, Net& net)
{
  wrapNetPinList(idb_net->get_io_pins(), idb_net->get_instance_pin_list(), net);
}

void STAInterface::wrapNetPinList(idb::IdbPins* io_pin_list, idb::IdbPins* instance_pin_list, Net& net)
{
  for (idb::IdbPin* idb_pin : io_pin_list->get_pin_list()) {
    wrapNetPin(idb_pin, net);
  }
  for (idb::IdbPin* idb_pin : instance_pin_list->get_pin_list()) {
    wrapNetPin(idb_pin, net);
  }
}

void STAInterface::wrapNetPin(idb::IdbPin* idb_pin, Net& net)
{
  std::string pin_name;
  if (idb_pin->is_io_pin()) {
    pin_name = wrapNetIOPinName(idb_pin);
  } else {
    pin_name = wrapNetInstancePinName(idb_pin);
  }
  wrapNetPinNameList(net, pin_name);
}

std::string STAInterface::wrapNetIOPinName(idb::IdbPin* idb_pin)
{
  return idb_pin->get_pin_name();
}

std::string STAInterface::wrapNetInstancePinName(idb::IdbPin* idb_pin)
{
  return idb_pin->get_instance()->get_name() + ":" + idb_pin->get_pin_name();
}

void STAInterface::wrapNetPinNameList(Net& net, std::string& pin_name)
{
  std::vector<std::string>& pin_name_list = net.get_pin_name_list();
  if (!STAUTIL.exist(pin_name_list, pin_name)) {
    pin_name_list.push_back(pin_name);
  }
}

void STAInterface::wrapNetToDatabase(Net& net)
{
  STADM.getDatabase().get_net_map()[net.get_net_name()] = net;
}

void STAInterface::wrapTimingLibrary()
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
  wrapTimingCellMap(lib_list);
  wrapTimingLibraryInfo(lib_list);
  idb::Lib::setSilentOutput(old_silent_output);
  STALOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void STAInterface::wrapTimingCellMap(std::vector<std::unique_ptr<idb::LibLibrary>>& lib_list)
{
  Database& database = STADM.getDatabase();
  database.get_timing_library().get_cell_map().clear();
  for (std::unique_ptr<idb::LibLibrary>& lib : lib_list) {
    for (std::unique_ptr<idb::LibCell>& lib_cell : lib->get_cells()) {
      wrapTimingCell(lib_cell.get());
    }
  }
}

void STAInterface::wrapTimingLibraryInfo(std::vector<std::unique_ptr<idb::LibLibrary>>& lib_list)
{
  Database& database = STADM.getDatabase();
  std::vector<std::string> library_name_list;
  for (std::unique_ptr<idb::LibLibrary>& lib : lib_list) {
    if (!STAUTIL.exist(library_name_list, lib->get_lib_name())) {
      library_name_list.push_back(lib->get_lib_name());
    }
  }
  database.get_timing_library().set_library_name_list(library_name_list);
  idb::LibLibrary* reference_lib = wrapReferenceLib(lib_list);
  if (reference_lib == nullptr) {
    return;
  }
  TimingLibrary& timing_library = database.get_timing_library();
  timing_library.set_has_library_info(true);
  timing_library.set_comment(reference_lib->get_comment());
  timing_library.set_simulation(reference_lib->get_simulation());
  timing_library.set_library_feature_list(reference_lib->get_library_features());
  timing_library.set_default_operating_conditions(reference_lib->get_default_operating_conditions());
  timing_library.set_default_wire_load(reference_lib->get_default_wire_load());
  timing_library.set_leakage_power_unit(reference_lib->get_leakage_power_unit());
  timing_library.set_current_unit_name(reference_lib->get_current_unit_name());
  timing_library.set_voltage_unit_name(reference_lib->get_voltage_unit_name());
  timing_library.set_cap_unit(wrapTimingCapacitiveUnit(reference_lib));
  timing_library.set_resistance_unit(wrapTimingResistanceUnit(reference_lib));
  timing_library.set_time_unit(wrapTimingTimeUnit(reference_lib));
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

idb::LibLibrary* STAInterface::wrapReferenceLib(std::vector<std::unique_ptr<idb::LibLibrary>>& lib_list)
{
  Database& database = STADM.getDatabase();
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

TimingCapacitiveUnit STAInterface::wrapTimingCapacitiveUnit(idb::LibLibrary* lib_library)
{
  if (lib_library->get_cap_unit() == idb::CapacitiveUnit::kFF) {
    return TimingCapacitiveUnit::kFF;
  }
  if (lib_library->get_cap_unit() == idb::CapacitiveUnit::kF) {
    return TimingCapacitiveUnit::kF;
  }
  return TimingCapacitiveUnit::kPF;
}

TimingResistanceUnit STAInterface::wrapTimingResistanceUnit(idb::LibLibrary* lib_library)
{
  if (lib_library->get_resistance_unit() == idb::ResistanceUnit::kOHM) {
    return TimingResistanceUnit::kOHM;
  }
  return TimingResistanceUnit::kkOHM;
}

TimingTimeUnit STAInterface::wrapTimingTimeUnit(idb::LibLibrary* lib_library)
{
  if (lib_library->get_time_unit() == idb::TimeUnit::kPS) {
    return TimingTimeUnit::kPS;
  }
  if (lib_library->get_time_unit() == idb::TimeUnit::kFS) {
    return TimingTimeUnit::kFS;
  }
  return TimingTimeUnit::kNS;
}

void STAInterface::wrapTimingCell(idb::LibCell* lib_cell)
{
  Database& database = STADM.getDatabase();
  idb::LibLibrary* lib_library = lib_cell->get_owner_lib();
  TimingCell timing_cell;
  timing_cell.set_cell_name(lib_cell->get_cell_name());
  timing_cell.set_library_name(lib_library->get_lib_name());
  timing_cell.set_area(lib_cell->get_cell_area());
  timing_cell.set_nom_voltage(lib_library->get_nom_voltage());
  timing_cell.set_cell_leakage_power(lib_cell->get_cell_leakage_power() * 1E-3);
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
    wrapTimingCellPort(timing_cell, lib_port.get());
  }

  wrapTimingCellPower(timing_cell, lib_cell);
  wrapTimingCellLeakagePower(timing_cell, lib_cell);

  for (std::unique_ptr<idb::LibArcSet>& lib_arc_set : lib_cell->get_cell_arcs()) {
    wrapTimingCellArc(timing_cell, lib_arc_set.get());
  }

  wrapTimingCellInfo(timing_cell);
  database.get_timing_library().get_cell_map()[timing_cell.get_cell_name()] = timing_cell;
}

void STAInterface::wrapTimingCellPort(TimingCell& timing_cell, idb::LibPort* lib_port)
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
  std::string function_string = lib_port->get_func_expr_str();
  timing_cell_port.set_function_expression(wrapLogicExpression(function_string));
  timing_cell.get_port_map()[timing_cell_port.get_port_name()] = timing_cell_port;
}


void STAInterface::wrapTimingCellPower(TimingCell& timing_cell, idb::LibCell* lib_cell)
{
  idb::LibLibrary* lib_library = lib_cell->get_owner_lib();
  for (std::unique_ptr<idb::LibPowerArcSet>& lib_power_arc_set : lib_cell->get_cell_power_arcs()) {
    for (std::unique_ptr<idb::LibPowerArc>& lib_power_arc : lib_power_arc_set->get_power_arcs()) {
      timing_cell.get_power_arc_list().push_back(wrapTimingPowerArc(lib_power_arc.get()));
    }
  }
  for (std::unique_ptr<idb::LibPort>& lib_port : lib_cell->get_cell_ports()) {
    std::string port_name = lib_port->get_port_name();
    for (std::unique_ptr<idb::LibInternalPowerInfo>& internal_power_info : lib_port->get_internal_powers()) {
      timing_cell.get_power_arc_list().push_back(wrapTimingPortPowerArc(internal_power_info.get(), port_name, lib_library));
    }
  }
}

void STAInterface::wrapTimingCellLeakagePower(TimingCell& timing_cell, idb::LibCell* lib_cell)
{
  for (std::unique_ptr<idb::LibLeakagePower>& lib_leakage_power : lib_cell->get_leakage_power_list()) {
    timing_cell.get_leakage_power_list().push_back(wrapTimingLeakagePower(lib_leakage_power.get()));
  }
}

TimingPowerArc STAInterface::wrapTimingPowerArc(idb::LibPowerArc* lib_power_arc)
{
  idb::LibInternalPowerInfo* internal_power_info = lib_power_arc->get_internal_power_info().get();
  idb::LibLibrary* lib_library = lib_power_arc->get_owner_cell()->get_owner_lib();
  TimingPowerArc timing_power_arc;
  timing_power_arc.set_source_port(lib_power_arc->get_src_port());
  timing_power_arc.set_sink_port(lib_power_arc->get_snk_port());
  timing_power_arc.set_related_pg_port(internal_power_info->get_related_pg_port());
  std::string when_string = internal_power_info->get_when();
  timing_power_arc.set_when_expression(wrapLogicExpression(when_string));
  timing_power_arc.set_time_unit_scale(wrapLibTimeUnitScale(lib_library));
  timing_power_arc.set_cap_unit_scale(wrapLibCapUnitScale(lib_library));
  wrapTimingPowerArcTable(timing_power_arc, internal_power_info->get_power_table_model());
  return timing_power_arc;
}

TimingPowerArc STAInterface::wrapTimingPortPowerArc(idb::LibInternalPowerInfo* internal_power_info, std::string& port_name,
                                                     idb::LibLibrary* lib_library)
{
  TimingPowerArc timing_power_arc;
  timing_power_arc.set_sink_port(port_name);
  timing_power_arc.set_related_pg_port(internal_power_info->get_related_pg_port());
  std::string when_string = internal_power_info->get_when();
  timing_power_arc.set_when_expression(wrapLogicExpression(when_string));
  timing_power_arc.set_time_unit_scale(wrapLibTimeUnitScale(lib_library));
  timing_power_arc.set_cap_unit_scale(wrapLibCapUnitScale(lib_library));
  wrapTimingPowerArcTable(timing_power_arc, internal_power_info->get_power_table_model());
  return timing_power_arc;
}

void STAInterface::wrapTimingPowerArcTable(TimingPowerArc& timing_power_arc, idb::LibTableModel* power_table_model)
{
  if (power_table_model == nullptr || !power_table_model->isPowerModel()) {
    return;
  }
  idb::LibTable* rise_power_table = power_table_model->getTable(CAST_POWER_TYPE_TO_INDEX(idb::LibTable::TableType::kRisePower));
  idb::LibTable* fall_power_table = power_table_model->getTable(CAST_POWER_TYPE_TO_INDEX(idb::LibTable::TableType::kFallPower));
  if (rise_power_table != nullptr) {
    timing_power_arc.get_energy_table_map()[TransType::kRise] = wrapTimingTable(rise_power_table);
  }
  if (fall_power_table != nullptr) {
    timing_power_arc.get_energy_table_map()[TransType::kFall] = wrapTimingTable(fall_power_table);
  }
}

TimingLeakagePower STAInterface::wrapTimingLeakagePower(idb::LibLeakagePower* lib_leakage_power)
{
  TimingLeakagePower timing_leakage_power;
  timing_leakage_power.set_related_pg_port(lib_leakage_power->get_related_pg_port());
  std::string when_string = lib_leakage_power->get_when();
  timing_leakage_power.set_when_expression(wrapLogicExpression(when_string));
  timing_leakage_power.set_leakage_power(lib_leakage_power->get_value() * 1E-3);
  return timing_leakage_power;
}

LogicExpression STAInterface::wrapLogicExpression(std::string& expression_string)
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
  wrapLogicExpressionTermList(logic_expression, liberty_expr);
  liberty_free_expr(liberty_expr);
  return logic_expression;
}

void STAInterface::wrapLogicExpressionTermList(LogicExpression& logic_expression, LibertyExpr* liberty_expr)
{
  if (liberty_expr == nullptr) {
    return;
  }
  LibertyExpr* left_expr = liberty_get_expr_left(liberty_expr);
  LibertyExpr* right_expr = liberty_get_expr_right(liberty_expr);
  wrapLogicExpressionTermList(logic_expression, left_expr);
  wrapLogicExpressionTermList(logic_expression, right_expr);
  if (left_expr != nullptr) {
    liberty_free_expr(left_expr);
  }
  if (right_expr != nullptr) {
    liberty_free_expr(right_expr);
  }
  LogicExpressionTerm logic_expression_term;
  LogicOperationType operation_type = wrapLogicOperationType(static_cast<int32_t>(liberty_expr->op));
  logic_expression_term.set_operation_type(operation_type);
  if (operation_type == LogicOperationType::kPort && liberty_expr->port_name != nullptr) {
    logic_expression_term.set_port_name(liberty_expr->port_name);
  }
  logic_expression.get_term_list().push_back(logic_expression_term);
}

LogicOperationType STAInterface::wrapLogicOperationType(const int32_t liberty_expr_op)
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

void STAInterface::wrapTimingCellArc(TimingCell& timing_cell, idb::LibArcSet* lib_arc_set)
{
  idb::LibArc* lib_arc = lib_arc_set->front();
  if (isSDFDelayArc(lib_arc)) {
    TimingCellArc timing_cell_arc = wrapDelayArc(lib_arc_set);
    timing_cell_arc.set_is_timing_graph_arc(lib_arc->isDelayArc());
    timing_cell_arc.set_is_clear_preset_arc(lib_arc->isClearPresetArc());
    timing_cell.get_cell_arc_list().push_back(timing_cell_arc);
    if (lib_arc->isClearPresetArc()) {
      wrapClearPresetArc(timing_cell, lib_arc);
    }
    return;
  }
  if (isSDFCheckArc(lib_arc)) {
    TimingCheckArc timing_check_arc = wrapCheckArc(lib_arc_set);
    timing_cell.get_sdf_check_arc_list().push_back(timing_check_arc);
    if (!lib_arc->isCheckArc()) {
      return;
    }
    timing_cell.get_check_arc_list().push_back(timing_check_arc);
  }
}

bool STAInterface::isSDFDelayArc(idb::LibArc* lib_arc)
{
  if (lib_arc->isDelayArc() || lib_arc->isClearPresetArc()) {
    return true;
  }
  idb::LibArc::TimingType timing_type = lib_arc->get_timing_type();
  return timing_type == idb::LibArc::TimingType::kThreeStateEnable || timing_type == idb::LibArc::TimingType::kThreeStateEnableRise
         || timing_type == idb::LibArc::TimingType::kThreeStateEnableFall || timing_type == idb::LibArc::TimingType::kThreeStateDisable
         || timing_type == idb::LibArc::TimingType::kThreeStateDisableRise || timing_type == idb::LibArc::TimingType::kThreeStateDisableFall;
}

bool STAInterface::isSDFCheckArc(idb::LibArc* lib_arc)
{
  return lib_arc->isCheckTableArc();
}

TimingCellArc STAInterface::wrapDelayArc(idb::LibArcSet* lib_arc_set)
{
  idb::LibArc* lib_arc = lib_arc_set->front();
  TimingCellArc timing_cell_arc;
  timing_cell_arc.set_source_port(lib_arc->get_src_port());
  timing_cell_arc.set_sink_port(lib_arc->get_snk_port());
  double delay = lib_arc->isDelayArc() ? lib_arc->getDelayOrConstrainCheckNs(idb::TransType::kRise, 0.0, 0.0) : 0.0;
  timing_cell_arc.set_delay(delay);
  timing_cell_arc.set_delay_max(delay);
  timing_cell_arc.set_delay_min(delay);
  timing_cell_arc.set_timing_arc_list(wrapTimingArcList(lib_arc_set));
  timing_cell_arc.set_is_clock_arc(lib_arc->isRisingTriggerArc() || lib_arc->isFallingTriggerArc());
  timing_cell_arc.set_is_disable_arc(lib_arc->isDisableArc());
  return timing_cell_arc;
}

void STAInterface::wrapClearPresetArc(TimingCell& timing_cell, idb::LibArc* lib_arc)
{
  if (lib_arc->get_timing_type() == idb::LibArc::TimingType::kClear) {
    timing_cell.set_has_clear_arc(true);
  } else if (lib_arc->get_timing_type() == idb::LibArc::TimingType::kPreset) {
    timing_cell.set_has_preset_arc(true);
  }
}

TimingCheckArc STAInterface::wrapCheckArc(idb::LibArcSet* lib_arc_set)
{
  idb::LibArc* lib_arc = lib_arc_set->front();
  TimingCheckArc timing_check_arc;
  timing_check_arc.set_clock_port(lib_arc->get_src_port());
  timing_check_arc.set_data_port(lib_arc->get_snk_port());
  timing_check_arc.set_check_type(wrapTimingCheckType(lib_arc));
  if (lib_arc->isCheckArc()) {
    timing_check_arc.set_check_time(lib_arc->getDelayOrConstrainCheckNs(idb::TransType::kRise, 0.0, 0.0));
  }
  timing_check_arc.set_timing_arc_list(wrapTimingArcList(lib_arc_set));
  timing_check_arc.set_clock_trans_type(wrapCheckTransType(lib_arc));
  return timing_check_arc;
}

std::vector<TimingArc> STAInterface::wrapTimingArcList(idb::LibArcSet* lib_arc_set)
{
  std::vector<TimingArc> timing_arc_list;
  int32_t arc_idx = 0;
  for (std::unique_ptr<idb::LibArc>& lib_arc : lib_arc_set->get_arcs()) {
    if (lib_arc->isDisableArc()) {
      continue;
    }
    TimingArc timing_arc = wrapTimingArc(lib_arc.get());
    timing_arc.set_arc_idx(arc_idx++);
    timing_arc_list.push_back(timing_arc);
  }
  return timing_arc_list;
}

TimingArc STAInterface::wrapTimingArc(idb::LibArc* lib_arc)
{
  TimingArc timing_arc;
  idb::LibLibrary* lib_library = lib_arc->get_owner_cell()->get_owner_lib();
  timing_arc.set_sense(wrapTimingArcSense(lib_arc));
  timing_arc.set_trigger_trans_type(wrapTriggerTransType(lib_arc));
  timing_arc.set_check_trans_type(wrapCheckTransType(lib_arc));
  timing_arc.set_library_name(lib_library->get_lib_name());
  timing_arc.set_sdf_cond(lib_arc->get_sdf_cond());
  timing_arc.set_time_unit_scale(wrapLibTimeUnitScale(lib_library));
  timing_arc.set_cap_unit_scale(wrapLibCapUnitScale(lib_library));
  timing_arc.set_slew_derate(lib_library->get_slew_derate_from_library());
  timing_arc.set_slew_lower_threshold_pct_rise(lib_library->get_slew_lower_threshold_pct_rise());
  timing_arc.set_slew_upper_threshold_pct_rise(lib_library->get_slew_upper_threshold_pct_rise());
  timing_arc.set_slew_lower_threshold_pct_fall(lib_library->get_slew_lower_threshold_pct_fall());
  timing_arc.set_slew_upper_threshold_pct_fall(lib_library->get_slew_upper_threshold_pct_fall());
  timing_arc.set_input_threshold_pct_rise(lib_library->get_input_threshold_pct_rise());
  timing_arc.set_output_threshold_pct_rise(lib_library->get_output_threshold_pct_rise());
  timing_arc.set_input_threshold_pct_fall(lib_library->get_input_threshold_pct_fall());
  timing_arc.set_output_threshold_pct_fall(lib_library->get_output_threshold_pct_fall());
  wrapTimingArcTable(timing_arc, lib_arc);
  return timing_arc;
}

void STAInterface::wrapTimingArcTable(TimingArc& timing_arc, idb::LibArc* lib_arc)
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
      timing_arc.get_delay_table_map()[TransType::kRise] = wrapTimingTable(rise_delay_table);
    }
    if (fall_delay_table != nullptr) {
      timing_arc.get_delay_table_map()[TransType::kFall] = wrapTimingTable(fall_delay_table);
    }
    if (rise_slew_table != nullptr) {
      timing_arc.get_slew_table_map()[TransType::kRise] = wrapTimingTable(rise_slew_table);
    }
    if (fall_slew_table != nullptr) {
      timing_arc.get_slew_table_map()[TransType::kFall] = wrapTimingTable(fall_slew_table);
    }
    return;
  }
  if (!table_model->isCheckModel()) {
    return;
  }
  idb::LibTable* rise_check_table = table_model->getTable(CAST_TYPE_TO_INDEX(idb::LibTable::TableType::kRiseConstrain));
  idb::LibTable* fall_check_table = table_model->getTable(CAST_TYPE_TO_INDEX(idb::LibTable::TableType::kFallConstrain));
  if (rise_check_table != nullptr) {
    timing_arc.get_check_table_map()[TransType::kRise] = wrapTimingTable(rise_check_table);
  }
  if (fall_check_table != nullptr) {
    timing_arc.get_check_table_map()[TransType::kFall] = wrapTimingTable(fall_check_table);
  }
}

TimingTable STAInterface::wrapTimingTable(idb::LibTable* lib_table)
{
  TimingTable timing_table;
  timing_table.set_variable_type1(wrapTimingTableVariableType(lib_table, true));
  timing_table.set_variable_type2(wrapTimingTableVariableType(lib_table, false));
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

TimingTableVariableType STAInterface::wrapTimingTableVariableType(idb::LibTable* lib_table, bool is_first_variable)
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

double STAInterface::wrapLibTimeUnitScale(idb::LibLibrary* lib_library)
{
  if (lib_library->get_time_unit() == idb::TimeUnit::kPS) {
    return 1e3;
  }
  if (lib_library->get_time_unit() == idb::TimeUnit::kFS) {
    return 1e6;
  }
  return 1.0;
}

double STAInterface::wrapLibCapUnitScale(idb::LibLibrary* lib_library)
{
  if (lib_library->get_cap_unit() == idb::CapacitiveUnit::kFF) {
    return static_cast<double>(idb::g_pf2ff);
  }
  return 1.0;
}

TimingArcSense STAInterface::wrapTimingArcSense(idb::LibArc* lib_arc)
{
  if (lib_arc->isNegativeArc()) {
    return TimingArcSense::kNegative;
  }
  if (lib_arc->isNonUnateArc()) {
    return TimingArcSense::kNonUnate;
  }
  return TimingArcSense::kPositive;
}

TransType STAInterface::wrapTriggerTransType(idb::LibArc* lib_arc)
{
  if (lib_arc->isFallingTriggerArc()) {
    return TransType::kFall;
  }
  if (lib_arc->isRisingTriggerArc()) {
    return TransType::kRise;
  }
  return TransType::kNone;
}

TransType STAInterface::wrapCheckTransType(idb::LibArc* lib_arc)
{
  if (lib_arc->isFallingEdgeCheck()) {
    return TransType::kFall;
  }
  return TransType::kRise;
}

TimingCheckType STAInterface::wrapTimingCheckType(idb::LibArc* lib_arc)
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

void STAInterface::wrapTimingCellInfo(TimingCell& timing_cell)
{
  if (!timing_cell.get_check_arc_list().empty()) {
    timing_cell.set_is_sequential(true);
  }
}

void STAInterface::wrapParasiticLibrary()
{
  Database& database = STADM.getDatabase();
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
    wrapParasiticNet(spef_net);
  }
}

void STAInterface::wrapParasiticNet(spef::Net& spef_net)
{
  Database& database = STADM.getDatabase();
  ParasiticNet parasitic_net;
  parasitic_net.set_net_name(spef_net.name);
  parasitic_net.set_lumped_capacitance(wrapParasiticCapacitance(spef_net.lcap));
  for (spef::ConnEntry& spef_conn : spef_net.conns) {
    wrapParasiticConnection(parasitic_net, spef_conn);
  }
  for (spef::ResCap& spef_res : spef_net.ress) {
    wrapParasiticResistance(parasitic_net, spef_res);
  }
  for (spef::ResCap& spef_cap : spef_net.caps) {
    wrapParasiticCapacitance(parasitic_net, spef_cap);
  }
  database.get_parasitic_library().get_net_map()[parasitic_net.get_net_name()] = parasitic_net;
}

void STAInterface::wrapParasiticConnection(ParasiticNet& parasitic_net, spef::ConnEntry& spef_conn)
{
  ParasiticNode& parasitic_node = wrapParasiticNode(parasitic_net, spef_conn.pin_port_name);
  parasitic_node.set_x(spef_conn.coordinate.x);
  parasitic_node.set_y(spef_conn.coordinate.y);
}

void STAInterface::wrapParasiticCapacitance(ParasiticNet& parasitic_net, spef::ResCap& spef_cap)
{
  double capacitance = wrapParasiticCapacitance(spef_cap.res_or_cap);
  ParasiticNode& parasitic_node = wrapParasiticNode(parasitic_net, spef_cap.node1);
  parasitic_node.set_capacitance(parasitic_node.get_capacitance() + capacitance);

  if (spef_cap.node2.empty()) {
    return;
  }
  if (parasitic_net.get_node_map().count(spef_cap.node2) > 0) {
    ParasiticNode& coupled_node = parasitic_net.get_node_map()[spef_cap.node2];
    coupled_node.set_capacitance(coupled_node.get_capacitance() + capacitance);
  }
}

void STAInterface::wrapParasiticResistance(ParasiticNet& parasitic_net, spef::ResCap& spef_res)
{
  ParasiticResistor parasitic_resistor;
  parasitic_resistor.set_source_node(spef_res.node1);
  parasitic_resistor.set_sink_node(spef_res.node2);
  parasitic_resistor.set_resistance(wrapParasiticResistance(spef_res.res_or_cap));
  parasitic_net.get_resistor_list().push_back(parasitic_resistor);
  wrapParasiticNode(parasitic_net, spef_res.node1);
  wrapParasiticNode(parasitic_net, spef_res.node2);
}

double STAInterface::wrapParasiticCapacitance(double spef_capacitance)
{
  Database& database = STADM.getDatabase();
  std::string spef_unit = database.get_parasitic_library().get_capacitive_unit();
  std::string target_unit = "PF";
  return spef_capacitance * wrapSpefUnitScale(spef_unit, target_unit);
}

double STAInterface::wrapParasiticResistance(double spef_resistance)
{
  Database& database = STADM.getDatabase();
  std::string spef_unit = database.get_parasitic_library().get_resistance_unit();
  std::string target_unit = "OHM";
  return spef_resistance * wrapSpefUnitScale(spef_unit, target_unit);
}

double STAInterface::wrapSpefUnitScale(std::string& spef_unit, std::string& target_unit)
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

ParasiticNode& STAInterface::wrapParasiticNode(ParasiticNet& parasitic_net, const std::string& node_name)
{
  ParasiticNode& parasitic_node = parasitic_net.get_node_map()[node_name];
  parasitic_node.set_node_name(node_name);
  return parasitic_node;
}

#endif

#if 1  // output

void STAInterface::output()
{
}

#endif

#endif

#if 1  // Lib

void STAInterface::writeLib(TCModel& tc_model, std::string& output_path)
{
  for (TCLib& tc_lib : tc_model.get_lib_list()) {
    std::string lib_file_path = getLibFilePath(tc_lib, output_path);
    std::unique_ptr<idb::LibLibrary> lib_library = buildLib(tc_lib);
    lib_library->printLibertyLibrary(lib_file_path.c_str());
    STALOG.info(Loc::current(), "Output iSTA extracted lib: ", lib_file_path);
  }
}

std::string STAInterface::getLibFilePath(TCLib& tc_lib, std::string& output_path)
{
  return STAUTIL.getString(output_path, tc_lib.get_design_name(), "_", GetAnalysisTypeName()(tc_lib.get_analysis_type()), ".lib");
}

std::unique_ptr<idb::LibLibrary> STAInterface::buildLib(TCLib& tc_lib)
{
  std::unique_ptr<idb::LibLibrary> lib_library = std::make_unique<idb::LibLibrary>(tc_lib.get_design_name().c_str());
  buildLibHeader(*lib_library);
  lib_library->addLibertyCell(buildLibCell(*lib_library, tc_lib));
  return lib_library;
}

void STAInterface::buildLibHeader(idb::LibLibrary& lib_library)
{
  TimingLibrary& timing_library = STADM.getDatabase().get_timing_library();
  if (timing_library.get_has_library_info()) {
    if (timing_library.get_comment()) {
      lib_library.set_comment(*timing_library.get_comment());
    }
    if (timing_library.get_simulation()) {
      lib_library.set_simulation(*timing_library.get_simulation());
    }
    for (std::string& library_feature : timing_library.get_library_feature_list()) {
      lib_library.add_library_feature(library_feature);
    }
    if (timing_library.get_leakage_power_unit()) {
      lib_library.set_leakage_power_unit(*timing_library.get_leakage_power_unit());
    }
    if (timing_library.get_current_unit_name()) {
      lib_library.set_current_unit_name(*timing_library.get_current_unit_name());
    }
    if (timing_library.get_voltage_unit_name()) {
      lib_library.set_voltage_unit_name(*timing_library.get_voltage_unit_name());
    }
    if (timing_library.get_cap_unit() == TimingCapacitiveUnit::kFF) {
      lib_library.set_cap_unit(idb::CapacitiveUnit::kFF);
    } else if (timing_library.get_cap_unit() == TimingCapacitiveUnit::kF) {
      lib_library.set_cap_unit(idb::CapacitiveUnit::kF);
    } else {
      lib_library.set_cap_unit(idb::CapacitiveUnit::kPF);
    }
    if (timing_library.get_resistance_unit() == TimingResistanceUnit::kOHM) {
      lib_library.set_resistance_unit(idb::ResistanceUnit::kOHM);
    } else {
      lib_library.set_resistance_unit(idb::ResistanceUnit::kkOHM);
    }
    if (timing_library.get_time_unit() == TimingTimeUnit::kPS) {
      lib_library.set_time_unit(idb::TimeUnit::kPS);
    } else if (timing_library.get_time_unit() == TimingTimeUnit::kFS) {
      lib_library.set_time_unit(idb::TimeUnit::kFS);
    } else {
      lib_library.set_time_unit(idb::TimeUnit::kNS);
    }
    if (timing_library.get_default_max_transition()) {
      lib_library.set_default_max_transition(*timing_library.get_default_max_transition());
    }
    if (timing_library.get_default_max_fanout()) {
      lib_library.set_default_max_fanout(*timing_library.get_default_max_fanout());
    }
    if (timing_library.get_default_fanout_load()) {
      lib_library.set_default_fanout_load(*timing_library.get_default_fanout_load());
    }
    if (timing_library.get_nom_process()) {
      lib_library.set_nom_process(*timing_library.get_nom_process());
    }
    lib_library.set_nom_voltage(timing_library.get_nom_voltage());
    if (timing_library.get_nom_temperature()) {
      lib_library.set_nom_temperature(*timing_library.get_nom_temperature());
    }
    lib_library.set_slew_lower_threshold_pct_rise(timing_library.get_slew_lower_threshold_pct_rise() * 100.0);
    lib_library.set_slew_upper_threshold_pct_rise(timing_library.get_slew_upper_threshold_pct_rise() * 100.0);
    lib_library.set_slew_lower_threshold_pct_fall(timing_library.get_slew_lower_threshold_pct_fall() * 100.0);
    lib_library.set_slew_upper_threshold_pct_fall(timing_library.get_slew_upper_threshold_pct_fall() * 100.0);
    lib_library.set_input_threshold_pct_rise(timing_library.get_input_threshold_pct_rise() * 100.0);
    lib_library.set_output_threshold_pct_rise(timing_library.get_output_threshold_pct_rise() * 100.0);
    lib_library.set_input_threshold_pct_fall(timing_library.get_input_threshold_pct_fall() * 100.0);
    lib_library.set_output_threshold_pct_fall(timing_library.get_output_threshold_pct_fall() * 100.0);
    lib_library.set_slew_derate_from_library(timing_library.get_slew_derate_from_library());
  } else {
    lib_library.set_simulation(false);
    lib_library.set_time_unit(idb::TimeUnit::kNS);
    lib_library.set_cap_unit(idb::CapacitiveUnit::kPF);
    lib_library.set_resistance_unit(idb::ResistanceUnit::kkOHM);
  }
  if (lib_library.get_library_features().empty()) {
    lib_library.add_library_feature("report_delay_calculation");
  }
}

std::unique_ptr<idb::LibCell> STAInterface::buildLibCell(idb::LibLibrary& lib_library, TCLib& tc_lib)
{
  std::unique_ptr<idb::LibCell> lib_cell = std::make_unique<idb::LibCell>(tc_lib.get_design_name().c_str(), &lib_library);
  lib_cell->set_is_macro();
  lib_cell->set_cell_area(tc_lib.get_area());
  buildLibPortList(*lib_cell, tc_lib);
  buildLibTimingArcList(*lib_cell, tc_lib);
  return lib_cell;
}

void STAInterface::buildLibPortList(idb::LibCell& lib_cell, TCLib& tc_lib)
{
  for (std::pair<const std::string, TCPort>& port_pair : tc_lib.get_port_map()) {
    buildLibPort(lib_cell, port_pair.second);
  }
}

void STAInterface::buildLibPort(idb::LibCell& lib_cell, TCPort& tc_port)
{
  std::unique_ptr<idb::LibPort> lib_port = std::make_unique<idb::LibPort>(tc_port.get_port_name().c_str());
  lib_port->set_ower_cell(&lib_cell);
  if (tc_port.get_direction() == PinDirection::kInput) {
    lib_port->set_port_type(idb::LibPort::LibertyPortType::kInput);
  } else if (tc_port.get_direction() == PinDirection::kOutput) {
    lib_port->set_port_type(idb::LibPort::LibertyPortType::kOutput);
  } else if (tc_port.get_direction() == PinDirection::kInout) {
    lib_port->set_port_type(idb::LibPort::LibertyPortType::kInOut);
  } else {
    lib_port->set_port_type(idb::LibPort::LibertyPortType::kDefault);
  }
  lib_port->set_is_clock(tc_port.get_is_clock());
  lib_port->set_port_cap(tc_port.get_capacitance());
  lib_cell.addLibertyPort(std::move(lib_port));
}

void STAInterface::buildLibTimingArcList(idb::LibCell& lib_cell, TCLib& tc_lib)
{
  for (TCTimingArc& tc_timing_arc : tc_lib.get_timing_arc_list()) {
    buildLibTimingArc(lib_cell, tc_timing_arc);
  }
}

void STAInterface::buildLibTimingArc(idb::LibCell& lib_cell, TCTimingArc& tc_timing_arc)
{
  std::unique_ptr<idb::LibArc> lib_arc = makeLibArc(tc_timing_arc.get_source_port(), tc_timing_arc.get_sink_port(), tc_timing_arc.get_timing_type());
  if (tc_timing_arc.get_is_check_arc()) {
    buildLibCheckTableModel(*lib_arc, tc_timing_arc);
  } else {
    lib_arc->set_timing_sense(tc_timing_arc.get_timing_sense().c_str());
    buildLibDelayTableModel(*lib_arc, tc_timing_arc);
  }
  lib_arc->set_owner_cell(&lib_cell);
  lib_cell.addLibertyArc(std::move(lib_arc));
}

void STAInterface::buildLibDelayTableModel(idb::LibArc& lib_arc, TCTimingArc& tc_timing_arc)
{
  std::unique_ptr<idb::LibDelayTableModel> delay_table_model = std::make_unique<idb::LibDelayTableModel>();
  for (TCScalarTable& tc_scalar_table : tc_timing_arc.get_scalar_table_list()) {
    if (tc_scalar_table.get_table_name() == "cell_rise") {
      delay_table_model->addTable(makeLibScalarTable(static_cast<int32_t>(idb::LibTable::TableType::kCellRise), tc_scalar_table));
    } else if (tc_scalar_table.get_table_name() == "cell_fall") {
      delay_table_model->addTable(makeLibScalarTable(static_cast<int32_t>(idb::LibTable::TableType::kCellFall), tc_scalar_table));
    } else if (tc_scalar_table.get_table_name() == "rise_transition") {
      delay_table_model->addTable(makeLibScalarTable(static_cast<int32_t>(idb::LibTable::TableType::kRiseTransition), tc_scalar_table));
    } else if (tc_scalar_table.get_table_name() == "fall_transition") {
      delay_table_model->addTable(makeLibScalarTable(static_cast<int32_t>(idb::LibTable::TableType::kFallTransition), tc_scalar_table));
    }
  }
  lib_arc.set_table_model(std::move(delay_table_model));
}

void STAInterface::buildLibCheckTableModel(idb::LibArc& lib_arc, TCTimingArc& tc_timing_arc)
{
  std::unique_ptr<idb::LibCheckTableModel> check_table_model = std::make_unique<idb::LibCheckTableModel>();
  for (TCScalarTable& tc_scalar_table : tc_timing_arc.get_scalar_table_list()) {
    if (tc_scalar_table.get_table_name() == "rise_constraint") {
      check_table_model->addTable(makeLibScalarTable(static_cast<int32_t>(idb::LibTable::TableType::kRiseConstrain), tc_scalar_table));
    } else if (tc_scalar_table.get_table_name() == "fall_constraint") {
      check_table_model->addTable(makeLibScalarTable(static_cast<int32_t>(idb::LibTable::TableType::kFallConstrain), tc_scalar_table));
    }
  }
  lib_arc.set_table_model(std::move(check_table_model));
}

std::unique_ptr<idb::LibArc> STAInterface::makeLibArc(std::string& source_port, std::string& sink_port, std::string& timing_type)
{
  std::unique_ptr<idb::LibArc> lib_arc = std::make_unique<idb::LibArc>();
  lib_arc->set_src_port(source_port.c_str());
  lib_arc->set_snk_port(sink_port.c_str());
  lib_arc->set_timing_type(timing_type.c_str());
  return lib_arc;
}

std::unique_ptr<idb::LibTable> STAInterface::makeLibScalarTable(int32_t table_type, TCScalarTable& tc_scalar_table)
{
  std::unique_ptr<idb::LibTable> lib_table
      = std::make_unique<idb::LibTable>(static_cast<idb::LibTable::TableType>(table_type), nullptr);
  lib_table->addTableValue(std::make_unique<idb::LibFloatValue>(tc_scalar_table.get_value()));
  return lib_table;
}

#endif

#endif

// private

STAInterface* STAInterface::_sta_interface_instance = nullptr;

}  // namespace ista
