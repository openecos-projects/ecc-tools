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
#include "PWInterface.hpp"

#include <stdexcept>

#ifdef __GLIBC__
#include <malloc.h>
#endif

#include "ClockPropagator.hpp"
#include "DataManager.hpp"
#include "DelayCalculator.hpp"
#include "GraphBuilder.hpp"
#include "Lib.hh"
#include "Logger.hpp"
#include "Monitor.hpp"
#include "PowerAnalyzer.hpp"
#include "PowerPropagator.hpp"
#include "PowerReporter.hpp"
#include "PWHeader.hpp"
#include "SdcCommand.hpp"
#include "TimingPropagator.hpp"
#include "Utility.hpp"
#include "VcdParser.hh"
#include "idm.h"
#include "spef/SpefParser.hh"

namespace ipw {

// public

PWInterface& PWInterface::getInst()
{
  if (_pw_interface_instance == nullptr) {
    _pw_interface_instance = new PWInterface();
  }
  return *_pw_interface_instance;
}

void PWInterface::destroyInst()
{
  if (_pw_interface_instance != nullptr) {
    delete _pw_interface_instance;
    _pw_interface_instance = nullptr;
  }
}

#if 1  // 外部调用PW的API

#if 1  // iPW

void PWInterface::initPW(std::map<std::string, std::any> config_map)
{
  if (config_map.contains("-min_slew_degradation")) {
    int32_t value = std::any_cast<int32_t>(config_map.at("-min_slew_degradation"));
    if (value != 0 && value != 1) {
      throw std::invalid_argument("-min_slew_degradation must be 0 or 1");
    }
  }
  Logger::initInst();
  // clang-format off
  PWLOG.info(Loc::current(), ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>");
  PWLOG.info(Loc::current(), "_____ _______________________       _______________________ ________ ________ ");
  PWLOG.info(Loc::current(), "___(_)__  ___/___  __/___    |      __  ___/___  __/___    |___  __ \\___  __/");
  PWLOG.info(Loc::current(), "__  / _____ \\ __  /   __  /| |      _____ \\ __  /   __  /| |__  /_/ /__  /  ");
  PWLOG.info(Loc::current(), "_  /  ____/ / _  /    _  ___ |      ____/ / _  /    _  ___ |_  _, _/ _  /     ");
  PWLOG.info(Loc::current(), "/_/   /____/  /_/     /_/  |_|      /____/  /_/     /_/  |_|/_/ |_|  /_/      ");
  PWLOG.info(Loc::current(), ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>");
  // clang-format on
  PWLOG.printLogFilePath();
  //////////////////////////////////////////////////////
  //////////////////////////////////////////////////////
  //////////////////////////////////////////////////////
  Monitor monitor;
  PWLOG.info(Loc::current(), "Starting...");

  DataManager::initInst();
  PWDM.input(config_map);
  DelayCalculator::initInst();
  SdcCommand::initInst();
  sdc::registerSdcCommands(SdcCommand::getInst());
  PWDM.readConstraint();

  PWLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void PWInterface::runPW()
{
  Monitor monitor;
  PWLOG.info(Loc::current(), "Starting...");

  PWDC.init();

  GraphBuilder::initInst();
  PWGB.build();
  GraphBuilder::destroyInst();

  ClockPropagator::initInst();
  PWCP.propagate();
  ClockPropagator::destroyInst();

  TimingPropagator::initInst();
  PWTP.propagate();
  TimingPropagator::destroyInst();

  PowerPropagator::initInst();
  PWPP.propagate();
  PowerPropagator::destroyInst();

  PowerAnalyzer::initInst();
  PWPA.analyze();
  PowerAnalyzer::destroyInst();

  PowerReporter::initInst();
  PWPR.report();
  PowerReporter::destroyInst();

  PWDC.destroy();

  PWLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void PWInterface::destroyPW()
{
  Monitor monitor;
  PWLOG.info(Loc::current(), "Starting...");

  PWDC.destroy();
  DelayCalculator::destroyInst();
  PWDM.output();
  SdcCommand::destroyInst();
  DataManager::destroyInst();

  PWLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());

  PWLOG.printLogFilePath();
  // clang-format off
  PWLOG.info(Loc::current(), ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>");
  PWLOG.info(Loc::current(), "_____ _______________________       _______________________   ________________________  __  ");
  PWLOG.info(Loc::current(), "___(_)__  ___/___  __/___    |      ___  ____/____  _/___  | / /____  _/__  ___/___  / / /  ");
  PWLOG.info(Loc::current(), "__  / _____ \\ __  /   __  /| |      __  /_     __  /  __   |/ /  __  /  _____ \\ __  /_/ / ");
  PWLOG.info(Loc::current(), "_  /  ____/ / _  /    _  ___ |      _  __/    __/ /   _  /|  /  __/ /   ____/ / _  __  /    ");
  PWLOG.info(Loc::current(), "/_/   /____/  /_/     /_/  |_|      /_/       /___/   /_/ |_/   /___/   /____/  /_/ /_/     ");
  PWLOG.info(Loc::current(), ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>");
  // clang-format on
  Logger::destroyInst();

#ifdef __GLIBC__
  // Every PW object (liberty trees, wrapped netlist, timing graph, report
  // buffers, ...) has been deleted above, but glibc keeps the freed pages
  // mapped in its arenas, so repeated in-process PW sessions keep inflating
  // the process RSS. Hand every freeable heap page back to the OS here.
  malloc_trim(0);
#endif
}

#endif

#endif

#if 1  // PW调用外部的API

#if 1  // TopData

#if 1  // input

void PWInterface::input(std::map<std::string, std::any>& config_map)
{
  wrapConfig(config_map);
  wrapDatabase();
}

void PWInterface::wrapConfig(std::map<std::string, std::any>& config_map)
{
  /////////////////////////////////////////////
  PWDM.getConfig().temp_directory_path = PWUTIL.getConfigValue<std::string>(config_map, "-temp_directory_path", "./pw_temp_directory");
  PWDM.getConfig().thread_number = PWUTIL.getConfigValue<int32_t>(config_map, "-thread_number", 128);
  PWDM.getConfig().min_slew_degradation = PWUTIL.getConfigValue<int32_t>(config_map, "-min_slew_degradation", 1);
  omp_set_num_threads(std::max(PWDM.getConfig().thread_number, 1));
  /////////////////////////////////////////////
}

void PWInterface::wrapDatabase()
{
  wrapDBInfo();
  wrapInstanceList();
  wrapPortList();
  wrapNetList();
  wrapTimingLibrary();
  wrapParasiticLibrary();
  wrapVcdActivity();
}

void PWInterface::wrapVcdActivity()
{
  Database& database = PWDM.getDatabase();
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

std::string PWInterface::wrapVcdPinName(std::string& vcd_signal_name)
{
  Database& database = PWDM.getDatabase();
  std::string top_scope_path = database.get_design_name() + "/";
  std::size_t top_scope_pos = vcd_signal_name.find(top_scope_path);
  if (top_scope_pos == std::string::npos || (top_scope_pos != 0 && vcd_signal_name[top_scope_pos - 1] != '/')) {
    return "";
  }
  std::string pin_name = vcd_signal_name.substr(top_scope_pos + top_scope_path.size());
  std::replace(pin_name.begin(), pin_name.end(), '/', ':');
  return pin_name;
}

void PWInterface::wrapDBInfo()
{
  PWDM.getDatabase().set_design_name(dmInst->get_idb_design()->get_design_name());
  wrapConstraintFilePath();
}

void PWInterface::wrapConstraintFilePath()
{
  PWDM.getDatabase().get_timing_constraint().set_sdc_file_path(dmInst->get_config().get_sdc_path());
}

void PWInterface::wrapInstanceList()
{
  idb::IdbDesign* idb_design = dmInst->get_idb_design();
  for (idb::IdbInstance* idb_instance : idb_design->get_instance_list()->get_instance_list()) {
    wrapInstance(idb_instance);
    wrapInstancePinList(idb_instance);
  }
}

void PWInterface::wrapInstance(idb::IdbInstance* idb_instance)
{
  Instance instance;
  instance.set_instance_id(idb_instance->get_id());
  instance.set_instance_name(idb_instance->get_name());
  instance.set_cell_name(idb_instance->get_cell_master()->get_name());
  instance.set_is_io_cell(idb_instance->get_cell_master()->is_io_cell());
  PWDM.getDatabase().get_instance_map()[instance.get_instance_name()] = instance;
}

void PWInterface::wrapInstancePinList(idb::IdbInstance* idb_instance)
{
  for (idb::IdbPin* idb_pin : idb_instance->get_pin_list()->get_pin_list()) {
    wrapInstancePin(idb_instance, idb_pin);
  }
}

void PWInterface::wrapInstancePin(idb::IdbInstance* idb_instance, idb::IdbPin* idb_pin)
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
  PWDM.getDatabase().get_pin_map()[full_name] = pin;
}

bool PWInterface::wrapSignalConnectType(idb::IdbConnectType connect_type)
{
  return connect_type == idb::IdbConnectType::kNone || connect_type == idb::IdbConnectType::kSignal || connect_type == idb::IdbConnectType::kClock
         || connect_type == idb::IdbConnectType::kReset || connect_type == idb::IdbConnectType::kScan || connect_type == idb::IdbConnectType::kTieOff;
}

std::string PWInterface::wrapInstancePinName(idb::IdbInstance* idb_instance, idb::IdbPin* idb_pin)
{
  return idb_instance->get_name() + ":" + idb_pin->get_pin_name();
}

PinDirection PWInterface::wrapPinDirection(idb::IdbConnectDirection idb_direction)
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
      PWLOG.error(Loc::current(), "Unrecognized type!");
      break;
  }
  return PinDirection::kNone;
}

void PWInterface::wrapPinCoordinate(Pin& pin, idb::IdbPin* idb_pin)
{
  idb::IdbCoordinate<int32_t>* coordinate = idb_pin->get_average_coordinate();
  pin.set_x(coordinate->get_x());
  pin.set_y(coordinate->get_y());
}

void PWInterface::wrapPortList()
{
  idb::IdbDesign* idb_design = dmInst->get_idb_design();
  for (idb::IdbPin* idb_pin : idb_design->get_io_pin_list()->get_pin_list()) {
    wrapPortPin(idb_pin);
  }
}

void PWInterface::wrapPortPin(idb::IdbPin* idb_pin)
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
  PWDM.getDatabase().get_pin_map()[full_name] = pin;
}

std::string PWInterface::wrapPinName(idb::IdbPin* idb_pin)
{
  return idb_pin->get_pin_name();
}

void PWInterface::wrapNetList()
{
  idb::IdbDesign* idb_design = dmInst->get_idb_design();
  for (idb::IdbNet* idb_net : idb_design->get_net_list()->get_net_list()) {
    wrapNet(idb_net);
  }
}

void PWInterface::wrapNet(idb::IdbNet* idb_net)
{
  if (!wrapSignalConnectType(idb_net->get_connect_type())) {
    return;
  }

  Net net;
  net.set_net_name(idb_net->get_net_name());
  wrapNetPinList(idb_net, net);
  wrapNetToDatabase(net);
}

void PWInterface::wrapNetPinList(idb::IdbNet* idb_net, Net& net)
{
  wrapNetPinList(idb_net->get_io_pins(), idb_net->get_instance_pin_list(), net);
}

void PWInterface::wrapNetPinList(idb::IdbPins* io_pin_list, idb::IdbPins* instance_pin_list, Net& net)
{
  for (idb::IdbPin* idb_pin : io_pin_list->get_pin_list()) {
    wrapNetPin(idb_pin, net);
  }
  for (idb::IdbPin* idb_pin : instance_pin_list->get_pin_list()) {
    wrapNetPin(idb_pin, net);
  }
}

void PWInterface::wrapNetPin(idb::IdbPin* idb_pin, Net& net)
{
  std::string pin_name;
  if (idb_pin->is_io_pin()) {
    pin_name = wrapNetIOPinName(idb_pin);
  } else {
    pin_name = wrapNetInstancePinName(idb_pin);
  }
  wrapNetPinNameList(net, pin_name);
}

std::string PWInterface::wrapNetIOPinName(idb::IdbPin* idb_pin)
{
  return idb_pin->get_pin_name();
}

std::string PWInterface::wrapNetInstancePinName(idb::IdbPin* idb_pin)
{
  return idb_pin->get_instance()->get_name() + ":" + idb_pin->get_pin_name();
}

void PWInterface::wrapNetPinNameList(Net& net, std::string& pin_name)
{
  std::vector<std::string>& pin_name_list = net.get_pin_name_list();
  if (!PWUTIL.exist(pin_name_list, pin_name)) {
    pin_name_list.push_back(pin_name);
  }
}

void PWInterface::wrapNetToDatabase(Net& net)
{
  PWDM.getDatabase().get_net_map()[net.get_net_name()] = net;
}

void PWInterface::wrapTimingLibrary()
{
  Monitor monitor;
  PWLOG.info(Loc::current(), "Starting...");
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
  PWLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void PWInterface::wrapTimingCellMap(std::vector<std::unique_ptr<idb::LibLibrary>>& lib_list)
{
  Database& database = PWDM.getDatabase();
  database.get_timing_library().get_cell_map().clear();
  for (std::unique_ptr<idb::LibLibrary>& lib : lib_list) {
    for (std::unique_ptr<idb::LibCell>& lib_cell : lib->get_cells()) {
      wrapTimingCell(lib_cell.get());
    }
  }
}

void PWInterface::wrapTimingLibraryInfo(std::vector<std::unique_ptr<idb::LibLibrary>>& lib_list)
{
  Database& database = PWDM.getDatabase();
  std::vector<std::string> library_name_list;
  for (std::unique_ptr<idb::LibLibrary>& lib : lib_list) {
    if (!PWUTIL.exist(library_name_list, lib->get_lib_name())) {
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

idb::LibLibrary* PWInterface::wrapReferenceLib(std::vector<std::unique_ptr<idb::LibLibrary>>& lib_list)
{
  Database& database = PWDM.getDatabase();
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

TimingCapacitiveUnit PWInterface::wrapTimingCapacitiveUnit(idb::LibLibrary* lib_library)
{
  if (lib_library->get_cap_unit() == idb::CapacitiveUnit::kFF) {
    return TimingCapacitiveUnit::kFF;
  }
  if (lib_library->get_cap_unit() == idb::CapacitiveUnit::kF) {
    return TimingCapacitiveUnit::kF;
  }
  return TimingCapacitiveUnit::kPF;
}

TimingResistanceUnit PWInterface::wrapTimingResistanceUnit(idb::LibLibrary* lib_library)
{
  if (lib_library->get_resistance_unit() == idb::ResistanceUnit::kOHM) {
    return TimingResistanceUnit::kOHM;
  }
  return TimingResistanceUnit::kkOHM;
}

TimingTimeUnit PWInterface::wrapTimingTimeUnit(idb::LibLibrary* lib_library)
{
  if (lib_library->get_time_unit() == idb::TimeUnit::kPS) {
    return TimingTimeUnit::kPS;
  }
  if (lib_library->get_time_unit() == idb::TimeUnit::kFS) {
    return TimingTimeUnit::kFS;
  }
  return TimingTimeUnit::kNS;
}

void PWInterface::wrapTimingCell(idb::LibCell* lib_cell)
{
  Database& database = PWDM.getDatabase();
  idb::LibLibrary* lib_library = lib_cell->get_owner_lib();
  TimingCell timing_cell;
  timing_cell.set_cell_name(lib_cell->get_cell_name());
  timing_cell.set_library_name(lib_library->get_lib_name());
  timing_cell.set_area(lib_cell->get_cell_area());
  timing_cell.set_nom_voltage(lib_library->get_nom_voltage());
  timing_cell.set_cell_leakage_power(lib_cell->get_cell_leakage_power() * 1E-3);
  // Timing uses check arcs to establish register clock/data pins. Importing
  // state functions for power must not change that timing classification.
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
  for (std::unique_ptr<idb::LibPortBus>& lib_bus : lib_cell->get_cell_buses()) {
    for (std::unique_ptr<idb::LibPort>& lib_port : lib_bus->get_ports()) {
      wrapTimingCellPort(timing_cell, lib_port.get());
    }
  }

  wrapTimingCellSequential(timing_cell, lib_cell);
  wrapTimingCellPower(timing_cell, lib_cell);
  wrapTimingCellLeakagePower(timing_cell, lib_cell);
  wrapTimingCellPowerConditions(timing_cell);

  for (std::unique_ptr<idb::LibArcSet>& lib_arc_set : lib_cell->get_cell_arcs()) {
    wrapTimingCellArc(timing_cell, lib_arc_set.get());
  }

  wrapTimingCellInfo(timing_cell);
  database.get_timing_library().get_cell_map()[timing_cell.get_cell_name()] = timing_cell;
}

void PWInterface::wrapTimingCellPort(TimingCell& timing_cell, idb::LibPort* lib_port)
{
  TimingCellPort timing_cell_port;
  timing_cell_port.set_port_name(lib_port->get_port_name());
  timing_cell_port.set_capacitance(lib_port->get_port_cap());
  timing_cell_port.set_drive_resistance(lib_port->driveResistance());
  idb::LibLibrary* library = lib_port->get_ower_cell()->get_owner_lib();
  timing_cell_port.set_fanout_load(lib_port->get_fanout_load().value_or(library->get_default_fanout_load().value_or(0.0)));
  timing_cell_port.set_max_fanout(lib_port->get_max_fanout() ? lib_port->get_max_fanout() : library->get_default_max_fanout());
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

void PWInterface::wrapTimingCellSequential(TimingCell& timing_cell, const idb::LibCell* lib_cell)
{
  for (const idb::LibSequential& lib_sequential : lib_cell->get_sequentials()) {
    const std::vector<std::string>& state_variables = lib_sequential.state_variables;
    if (state_variables.empty()) {
      continue;
    }
    TimingSequential timing_sequential;
    timing_sequential.state_port = state_variables.front();
    if (state_variables.size() > 1) {
      timing_sequential.inverted_state_port = state_variables[1];
    }
    timing_sequential.is_latch = lib_sequential.is_latch;
    timing_sequential.data = wrapLogicExpression(lib_sequential.get_attribute(timing_sequential.is_latch ? "data_in" : "next_state"));
    timing_sequential.clock = wrapLogicExpression(lib_sequential.get_attribute(timing_sequential.is_latch ? "enable" : "clocked_on"));
    timing_sequential.clear = wrapLogicExpression(lib_sequential.get_attribute("clear"));
    timing_sequential.preset = wrapLogicExpression(lib_sequential.get_attribute("preset"));
    timing_cell.get_sequentials().push_back(std::move(timing_sequential));
  }
}

void PWInterface::wrapTimingCellPower(TimingCell& timing_cell, idb::LibCell* lib_cell)
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
  for (std::unique_ptr<idb::LibPortBus>& lib_bus : lib_cell->get_cell_buses()) {
    for (std::unique_ptr<idb::LibPort>& lib_port : lib_bus->get_ports()) {
      std::string port_name = lib_port->get_port_name();
      for (std::unique_ptr<idb::LibInternalPowerInfo>& internal_power_info : lib_port->get_internal_powers()) {
        timing_cell.get_power_arc_list().push_back(wrapTimingPortPowerArc(internal_power_info.get(), port_name, lib_library));
      }
    }
  }
}

void PWInterface::wrapTimingCellLeakagePower(TimingCell& timing_cell, idb::LibCell* lib_cell)
{
  for (std::unique_ptr<idb::LibLeakagePower>& lib_leakage_power : lib_cell->get_leakage_power_list()) {
    timing_cell.get_leakage_power_list().push_back(wrapTimingLeakagePower(lib_leakage_power.get()));
  }
}

void PWInterface::wrapTimingCellPowerConditions(TimingCell& timing_cell)
{
  timing_cell.resolveDefaultPowerArcConditions();
  if (timing_cell.get_is_sequential_for_power()) {
    return;
  }
  // Liberty may include outputs in a state condition (e.g. A & Y on a
  // buffer). Substitute their functions so the BDD preserves correlation.
  std::map<std::string, LogicExpression> output_functions;
  for (auto& [port_name, timing_cell_port] : timing_cell.get_port_map()) {
    if (timing_cell_port.get_is_output() && !timing_cell_port.get_function_expression().get_is_empty()) {
      output_functions[port_name] = timing_cell_port.get_function_expression();
    }
  }
  for (TimingPowerArc& timing_power_arc : timing_cell.get_power_arc_list()) {
    if (timing_power_arc.get_source_port().empty()) {
      timing_power_arc.get_when_expression().substitute_ports(output_functions);
    }
  }
  for (TimingLeakagePower& timing_leakage_power : timing_cell.get_leakage_power_list()) {
    timing_leakage_power.get_when_expression().substitute_ports(output_functions);
  }
}

TimingPowerArc PWInterface::wrapTimingPowerArc(idb::LibPowerArc* lib_power_arc)
{
  idb::LibInternalPowerInfo* internal_power_info = lib_power_arc->get_internal_power_info().get();
  idb::LibLibrary* lib_library = lib_power_arc->get_owner_cell()->get_owner_lib();
  TimingPowerArc timing_power_arc;
  timing_power_arc.set_source_port(lib_power_arc->get_src_port());
  timing_power_arc.set_sink_port(lib_power_arc->get_snk_port());
  timing_power_arc.set_related_pg_port(internal_power_info->get_related_pg_port());
  std::string when_string = internal_power_info->get_when();
  timing_power_arc.set_when_expression(wrapLogicExpression(when_string));
  // Match the conditional timing arc, since XOR/mux paths can change sense
  // with their side inputs. Clock-to-Q tables always use the active clock edge.
  idb::LibArc* matched_arc = nullptr;
  for (std::unique_ptr<idb::LibArcSet>& lib_arc_set : lib_power_arc->get_owner_cell()->get_cell_arcs()) {
    for (std::unique_ptr<idb::LibArc>& lib_arc : lib_arc_set->get_arcs()) {
      if (timing_power_arc.get_source_port() != lib_arc->get_src_port() || timing_power_arc.get_sink_port() != lib_arc->get_snk_port()) {
        continue;
      }
      if (matched_arc == nullptr || lib_arc->get_when() == when_string) {
        matched_arc = lib_arc.get();
      }
      if (lib_arc->get_when() == when_string) {
        break;
      }
    }
    if (matched_arc != nullptr && matched_arc->get_when() == when_string) {
      break;
    }
  }
  if (matched_arc != nullptr) {
    timing_power_arc.set_source_sense(wrapTimingArcSense(matched_arc));
    timing_power_arc.set_source_transition(wrapTriggerTransType(matched_arc));
    if (matched_arc->get_timing_type() == idb::LibArc::TimingType::kClear) {
      timing_power_arc.set_sink_transition(TransType::kFall);
    } else if (matched_arc->get_timing_type() == idb::LibArc::TimingType::kPreset) {
      timing_power_arc.set_sink_transition(TransType::kRise);
    }
  }
  timing_power_arc.set_time_unit_scale(wrapLibTimeUnitScale(lib_library));
  timing_power_arc.set_cap_unit_scale(wrapLibCapUnitScale(lib_library));
  wrapTimingPowerArcTable(timing_power_arc, internal_power_info->get_power_table_model());
  return timing_power_arc;
}

TimingPowerArc PWInterface::wrapTimingPortPowerArc(idb::LibInternalPowerInfo* internal_power_info, std::string& port_name, idb::LibLibrary* lib_library)
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

void PWInterface::wrapTimingPowerArcTable(TimingPowerArc& timing_power_arc, idb::LibTableModel* power_table_model)
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

TimingLeakagePower PWInterface::wrapTimingLeakagePower(idb::LibLeakagePower* lib_leakage_power)
{
  TimingLeakagePower timing_leakage_power;
  timing_leakage_power.set_related_pg_port(lib_leakage_power->get_related_pg_port());
  std::string when_string = lib_leakage_power->get_when();
  timing_leakage_power.set_when_expression(wrapLogicExpression(when_string));
  timing_leakage_power.set_leakage_power(lib_leakage_power->get_value() * 1E-3);
  return timing_leakage_power;
}

LogicExpression PWInterface::wrapLogicExpression(const std::string& expression_string)
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

void PWInterface::wrapLogicExpressionTermList(LogicExpression& logic_expression, LibertyExpr* liberty_expr)
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

LogicOperationType PWInterface::wrapLogicOperationType(const int32_t liberty_expr_op)
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
      PWLOG.error(Loc::current(), "Unrecognized type!");
      break;
  }
  return LogicOperationType::kNone;
}

void PWInterface::wrapTimingCellArc(TimingCell& timing_cell, idb::LibArcSet* lib_arc_set)
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

bool PWInterface::isSDFDelayArc(idb::LibArc* lib_arc)
{
  if (lib_arc->isDelayArc() || lib_arc->isClearPresetArc()) {
    return true;
  }
  idb::LibArc::TimingType timing_type = lib_arc->get_timing_type();
  return timing_type == idb::LibArc::TimingType::kThreeStateEnable || timing_type == idb::LibArc::TimingType::kThreeStateEnableRise
         || timing_type == idb::LibArc::TimingType::kThreeStateEnableFall || timing_type == idb::LibArc::TimingType::kThreeStateDisable
         || timing_type == idb::LibArc::TimingType::kThreeStateDisableRise || timing_type == idb::LibArc::TimingType::kThreeStateDisableFall;
}

bool PWInterface::isSDFCheckArc(idb::LibArc* lib_arc)
{
  return lib_arc->isCheckTableArc();
}

TimingCellArc PWInterface::wrapDelayArc(idb::LibArcSet* lib_arc_set)
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

void PWInterface::wrapClearPresetArc(TimingCell& timing_cell, idb::LibArc* lib_arc)
{
  if (lib_arc->get_timing_type() == idb::LibArc::TimingType::kClear) {
    timing_cell.set_has_clear_arc(true);
  } else if (lib_arc->get_timing_type() == idb::LibArc::TimingType::kPreset) {
    timing_cell.set_has_preset_arc(true);
  }
}

TimingCheckArc PWInterface::wrapCheckArc(idb::LibArcSet* lib_arc_set)
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

std::vector<TimingArc> PWInterface::wrapTimingArcList(idb::LibArcSet* lib_arc_set)
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

TimingArc PWInterface::wrapTimingArc(idb::LibArc* lib_arc)
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

void PWInterface::wrapTimingArcTable(TimingArc& timing_arc, idb::LibArc* lib_arc)
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

TimingTable PWInterface::wrapTimingTable(idb::LibTable* lib_table)
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

TimingTableVariableType PWInterface::wrapTimingTableVariableType(idb::LibTable* lib_table, bool is_first_variable)
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

double PWInterface::wrapLibTimeUnitScale(idb::LibLibrary* lib_library)
{
  if (lib_library->get_time_unit() == idb::TimeUnit::kPS) {
    return 1e3;
  }
  if (lib_library->get_time_unit() == idb::TimeUnit::kFS) {
    return 1e6;
  }
  return 1.0;
}

double PWInterface::wrapLibCapUnitScale(idb::LibLibrary* lib_library)
{
  if (lib_library->get_cap_unit() == idb::CapacitiveUnit::kFF) {
    return static_cast<double>(idb::g_pf2ff);
  }
  return 1.0;
}

TimingArcSense PWInterface::wrapTimingArcSense(idb::LibArc* lib_arc)
{
  if (lib_arc->isNegativeArc()) {
    return TimingArcSense::kNegative;
  }
  if (lib_arc->isNonUnateArc()) {
    return TimingArcSense::kNonUnate;
  }
  return TimingArcSense::kPositive;
}

TransType PWInterface::wrapTriggerTransType(idb::LibArc* lib_arc)
{
  if (lib_arc->isFallingTriggerArc()) {
    return TransType::kFall;
  }
  if (lib_arc->isRisingTriggerArc()) {
    return TransType::kRise;
  }
  return TransType::kNone;
}

TransType PWInterface::wrapCheckTransType(idb::LibArc* lib_arc)
{
  if (lib_arc->isFallingEdgeCheck()) {
    return TransType::kFall;
  }
  return TransType::kRise;
}

TimingCheckType PWInterface::wrapTimingCheckType(idb::LibArc* lib_arc)
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

void PWInterface::wrapTimingCellInfo(TimingCell& timing_cell)
{
  if (!timing_cell.get_check_arc_list().empty()) {
    timing_cell.set_is_sequential(true);
  }
}

void PWInterface::wrapParasiticLibrary()
{
  Database& database = PWDM.getDatabase();
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

void PWInterface::wrapParasiticNet(spef::Net& spef_net)
{
  Database& database = PWDM.getDatabase();
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

void PWInterface::wrapParasiticConnection(ParasiticNet& parasitic_net, spef::ConnEntry& spef_conn)
{
  ParasiticNode& parasitic_node = wrapParasiticNode(parasitic_net, spef_conn.pin_port_name);
  parasitic_node.set_x(spef_conn.coordinate.x);
  parasitic_node.set_y(spef_conn.coordinate.y);
}

void PWInterface::wrapParasiticCapacitance(ParasiticNet& parasitic_net, spef::ResCap& spef_cap)
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

void PWInterface::wrapParasiticResistance(ParasiticNet& parasitic_net, spef::ResCap& spef_res)
{
  ParasiticResistor parasitic_resistor;
  parasitic_resistor.set_source_node(spef_res.node1);
  parasitic_resistor.set_sink_node(spef_res.node2);
  parasitic_resistor.set_resistance(wrapParasiticResistance(spef_res.res_or_cap));
  parasitic_net.get_resistor_list().push_back(parasitic_resistor);
  wrapParasiticNode(parasitic_net, spef_res.node1);
  wrapParasiticNode(parasitic_net, spef_res.node2);
}

double PWInterface::wrapParasiticCapacitance(double spef_capacitance)
{
  Database& database = PWDM.getDatabase();
  std::string spef_unit = database.get_parasitic_library().get_capacitive_unit();
  std::string target_unit = "PF";
  return spef_capacitance * wrapSpefUnitScale(spef_unit, target_unit);
}

double PWInterface::wrapParasiticResistance(double spef_resistance)
{
  Database& database = PWDM.getDatabase();
  std::string spef_unit = database.get_parasitic_library().get_resistance_unit();
  std::string target_unit = "OHM";
  return spef_resistance * wrapSpefUnitScale(spef_unit, target_unit);
}

double PWInterface::wrapSpefUnitScale(std::string& spef_unit, std::string& target_unit)
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

ParasiticNode& PWInterface::wrapParasiticNode(ParasiticNet& parasitic_net, const std::string& node_name)
{
  ParasiticNode& parasitic_node = parasitic_net.get_node_map()[node_name];
  parasitic_node.set_node_name(node_name);
  return parasitic_node;
}

#endif

#if 1  // output

void PWInterface::output()
{
}

#endif

#endif

#endif

// private

PWInterface* PWInterface::_pw_interface_instance = nullptr;

}  // namespace ipw
