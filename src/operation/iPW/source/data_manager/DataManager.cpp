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
#include "PWInterface.hpp"
#include "SdcCommand.hpp"
#include "Utility.hpp"

namespace ipw {

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
    PWLOG.error(Loc::current(), "The instance not initialized!");
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
  PWLOG.info(Loc::current(), "Starting...");
  PWI.input(config_map);
  buildConfig();
  buildDatabase();
  printConfig();
  printDatabase();
  PWLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void DataManager::output()
{
  Monitor monitor;
  PWLOG.info(Loc::current(), "Starting...");
  PWI.output();
  PWLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

// private

DataManager* DataManager::_dm_instance = nullptr;

#if 1  // build

void DataManager::buildConfig()
{
  /////////////////////////////////////////////
  // **********        PW        ********** //
  _config.temp_directory_path = std::filesystem::absolute(_config.temp_directory_path);
  _config.temp_directory_path += "/";
  _config.log_file_path = _config.temp_directory_path + "pw.log";
  // **********    DataManager    ********** //
  _config.dm_temp_directory_path = _config.temp_directory_path + "data_manager/";
  // **********   GraphBuilder    ********** //
  _config.gb_temp_directory_path = _config.temp_directory_path + "graph_builder/";
  // ********* DelayCalculator   ********* //
  _config.dc_temp_directory_path = _config.temp_directory_path + "delay_calculator/";
  // ******** ClockPropagator    ********* //
  _config.cp_temp_directory_path = _config.temp_directory_path + "clock_propagator/";
  // ********* SignalPropagator   ********* //
  _config.sp_temp_directory_path = _config.temp_directory_path + "signal_propagator/";
  // ********* PowerPropagator    ********* //
  _config.pp_temp_directory_path = _config.temp_directory_path + "power_propagator/";
  // ********** PowerAnalyzer    ********* //
  _config.pa_temp_directory_path = _config.temp_directory_path + "power_analyzer/";
  // **********  PowerReporter    ********** //
  _config.pr_temp_directory_path = _config.temp_directory_path + "power_reporter/";
  /////////////////////////////////////////////
  // **********        PW        ********** //
  PWUTIL.removeDir(_config.temp_directory_path);
  PWUTIL.createDir(_config.temp_directory_path);
  PWUTIL.createDirByFile(_config.log_file_path);
  // **********    DataManager    ********** //
  PWUTIL.createDir(_config.dm_temp_directory_path);
  // **********   GraphBuilder    ********** //
  PWUTIL.createDir(_config.gb_temp_directory_path);
  // ********* DelayCalculator   ********* //
  PWUTIL.createDir(_config.dc_temp_directory_path);
  // ******** ClockPropagator    ********* //
  PWUTIL.createDir(_config.cp_temp_directory_path);
  // ********* SignalPropagator   ********* //
  PWUTIL.createDir(_config.sp_temp_directory_path);
  // ********* PowerPropagator    ********* //
  PWUTIL.createDir(_config.pp_temp_directory_path);
  // ********** PowerAnalyzer    ********* //
  PWUTIL.createDir(_config.pa_temp_directory_path);
  // **********  PowerReporter    ********** //
  PWUTIL.createDir(_config.pr_temp_directory_path);
  /////////////////////////////////////////////
  PWLOG.openLogFileStream(_config.log_file_path);
}

void DataManager::buildDatabase()
{
  buildInstanceList();
  buildNetList();
  buildInstanceTimingInfo();
  readConstraint();
}

void DataManager::readConstraint()
{
  Database& database = _database;
  database.get_timing_constraint().get_clock_map().clear();
  database.get_timing_constraint().get_port_constraint_map().clear();
  database.get_timing_constraint().get_case_analysis_map().clear();
  database.get_timing_constraint().get_effective_case_analysis_map().clear();
  database.get_timing_constraint().clear_net_load();
  if (_config.sdc_file_path.empty()) {
    return;
  }

  SdcCommand::initInst();
  SdcCommand& sdc_command = SdcCommand::getInst();
  if (sdc_command.evalScriptFile(_config.sdc_file_path) != TCL_OK) {
    PWLOG.warn(Loc::current(), "SDC command failed in '", _config.sdc_file_path, "' at line ", sdc_command.get_error_line_number(), ": ",
               sdc_command.get_error_message());
    PWLOG.error(Loc::current(), "SDC contains invalid or unsupported constraints; power analysis stopped");
  }
  SdcCommand::destroyInst();
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
  instance.set_is_clock_gating(timing_cell.get_is_clock_gating());
  TimingCellArc* clock_to_q_arc = findClockToQArc(timing_cell);
  if (clock_to_q_arc != nullptr) {
    instance.set_output_pin_name(getInstancePinName(instance, clock_to_q_arc->get_sink_port()));
    instance.set_clock_to_q_arc(clock_to_q_arc);
  } else {
    instance.set_output_pin_name(findOutputPinName(instance, timing_cell));
  }
  if (!timing_cell.get_clock_port_name().empty()) {
    instance.set_clock_pin_name(getInstancePinName(instance, timing_cell.get_clock_port_name()));
  }
  if (!timing_cell.get_data_port_name().empty()) {
    instance.set_data_pin_name(getInstancePinName(instance, timing_cell.get_data_port_name()));
  }
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
      std::string output_port_name = port_name;
      return getInstancePinName(instance, output_port_name);
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
  if (!PWUTIL.exist(list, value)) {
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
  net.get_driver_pin_list().clear();
  net.get_load_pin_list().clear();

  for (std::string& pin_name : net.get_pin_name_list()) {
    Pin& pin = database.get_pin_map()[pin_name];
    pin.set_net_name(net_name);
    makeUniqueName(net.get_load_pin_list(), pin_name);
  }
}

void DataManager::printConfig()
{
  /////////////////////////////////////////////
  // **********        PW        ********** //
  PWLOG.info(Loc::current(), PWUTIL.getSpaceByTabNum(0), "PW_CONFIG_INPUT");
  PWLOG.info(Loc::current(), PWUTIL.getSpaceByTabNum(1), "temp_directory_path");
  PWLOG.info(Loc::current(), PWUTIL.getSpaceByTabNum(2), _config.temp_directory_path);
  PWLOG.info(Loc::current(), PWUTIL.getSpaceByTabNum(1), "thread_number");
  PWLOG.info(Loc::current(), PWUTIL.getSpaceByTabNum(2), _config.thread_number);
  PWLOG.info(Loc::current(), PWUTIL.getSpaceByTabNum(1), "min_slew_degradation");
  PWLOG.info(Loc::current(), PWUTIL.getSpaceByTabNum(2), _config.min_slew_degradation);
  PWLOG.info(Loc::current(), PWUTIL.getSpaceByTabNum(1), "sdc_file_path");
  PWLOG.info(Loc::current(), PWUTIL.getSpaceByTabNum(2), _config.sdc_file_path);
  // **********        PW        ********** //
  PWLOG.info(Loc::current(), PWUTIL.getSpaceByTabNum(0), "PW_CONFIG_BUILD");
  PWLOG.info(Loc::current(), PWUTIL.getSpaceByTabNum(1), "log_file_path");
  PWLOG.info(Loc::current(), PWUTIL.getSpaceByTabNum(2), _config.log_file_path);
  // **********    DataManager    ********** //
  PWLOG.info(Loc::current(), PWUTIL.getSpaceByTabNum(1), "DataManager");
  PWLOG.info(Loc::current(), PWUTIL.getSpaceByTabNum(2), "dm_temp_directory_path");
  PWLOG.info(Loc::current(), PWUTIL.getSpaceByTabNum(3), _config.dm_temp_directory_path);
  // **********   GraphBuilder    ********** //
  PWLOG.info(Loc::current(), PWUTIL.getSpaceByTabNum(1), "GraphBuilder");
  PWLOG.info(Loc::current(), PWUTIL.getSpaceByTabNum(2), "gb_temp_directory_path");
  PWLOG.info(Loc::current(), PWUTIL.getSpaceByTabNum(3), _config.gb_temp_directory_path);
  // ********* DelayCalculator   ********* //
  PWLOG.info(Loc::current(), PWUTIL.getSpaceByTabNum(1), "DelayCalculator");
  PWLOG.info(Loc::current(), PWUTIL.getSpaceByTabNum(2), "dc_temp_directory_path");
  PWLOG.info(Loc::current(), PWUTIL.getSpaceByTabNum(3), _config.dc_temp_directory_path);
  // ******** ClockPropagator    ********* //
  PWLOG.info(Loc::current(), PWUTIL.getSpaceByTabNum(1), "ClockPropagator");
  PWLOG.info(Loc::current(), PWUTIL.getSpaceByTabNum(2), "cp_temp_directory_path");
  PWLOG.info(Loc::current(), PWUTIL.getSpaceByTabNum(3), _config.cp_temp_directory_path);
  // ********* SignalPropagator   ********* //
  PWLOG.info(Loc::current(), PWUTIL.getSpaceByTabNum(1), "SignalPropagator");
  PWLOG.info(Loc::current(), PWUTIL.getSpaceByTabNum(2), "sp_temp_directory_path");
  PWLOG.info(Loc::current(), PWUTIL.getSpaceByTabNum(3), _config.sp_temp_directory_path);
  // ********* PowerPropagator    ********* //
  PWLOG.info(Loc::current(), PWUTIL.getSpaceByTabNum(1), "PowerPropagator");
  PWLOG.info(Loc::current(), PWUTIL.getSpaceByTabNum(2), "pp_temp_directory_path");
  PWLOG.info(Loc::current(), PWUTIL.getSpaceByTabNum(3), _config.pp_temp_directory_path);
  // ********** PowerAnalyzer    ********* //
  PWLOG.info(Loc::current(), PWUTIL.getSpaceByTabNum(1), "PowerAnalyzer");
  PWLOG.info(Loc::current(), PWUTIL.getSpaceByTabNum(2), "pa_temp_directory_path");
  PWLOG.info(Loc::current(), PWUTIL.getSpaceByTabNum(3), _config.pa_temp_directory_path);
  // **********  PowerReporter    ********** //
  PWLOG.info(Loc::current(), PWUTIL.getSpaceByTabNum(1), "PowerReporter");
  PWLOG.info(Loc::current(), PWUTIL.getSpaceByTabNum(2), "pr_temp_directory_path");
  PWLOG.info(Loc::current(), PWUTIL.getSpaceByTabNum(3), _config.pr_temp_directory_path);
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
  // **********        PW        ********** //
  PWLOG.info(Loc::current(), PWUTIL.getSpaceByTabNum(0), "PW_DATABASE");
  PWLOG.info(Loc::current(), PWUTIL.getSpaceByTabNum(1), "design_name");
  PWLOG.info(Loc::current(), PWUTIL.getSpaceByTabNum(2), _database.get_design_name());
  PWLOG.info(Loc::current(), PWUTIL.getSpaceByTabNum(1), "instance_num");
  PWLOG.info(Loc::current(), PWUTIL.getSpaceByTabNum(2), _database.get_instance_map().size());
  PWLOG.info(Loc::current(), PWUTIL.getSpaceByTabNum(1), "port_num");
  PWLOG.info(Loc::current(), PWUTIL.getSpaceByTabNum(2), port_num);
  PWLOG.info(Loc::current(), PWUTIL.getSpaceByTabNum(1), "pin_num");
  PWLOG.info(Loc::current(), PWUTIL.getSpaceByTabNum(2), _database.get_pin_map().size());
  PWLOG.info(Loc::current(), PWUTIL.getSpaceByTabNum(1), "net_num");
  PWLOG.info(Loc::current(), PWUTIL.getSpaceByTabNum(2), _database.get_net_map().size());
  /////////////////////////////////////////////
}

#endif

}  // namespace ipw
