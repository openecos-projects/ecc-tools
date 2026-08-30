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
#include "SdcCommand.hpp"
#include "STAInterface.hpp"
#include "Utility.hpp"

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
  _config.path_report_number = std::max(_config.path_report_number, 1);
  _config.endpoint_path_report_number = std::max(_config.endpoint_path_report_number, 1);
  _config.timing_path_limit = std::max(_config.timing_path_limit, 0);
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
  // ********* PowerPropagator    ********* //
  _config.pp_temp_directory_path = _config.temp_directory_path + "power_propagator/";
  // ********** TimingAnalyzer   ********* //
  _config.ta_temp_directory_path = _config.temp_directory_path + "timing_analyzer/";
  // ********** PowerAnalyzer    ********* //
  _config.pa_temp_directory_path = _config.temp_directory_path + "power_analyzer/";
  // ******* TimingCharacterizer ******* //
  _config.tc_temp_directory_path = _config.temp_directory_path + "timing_characterizer/";
  // **********  TimingReporter   ********** //
  _config.tr_temp_directory_path = _config.temp_directory_path + "timing_reporter/";
  // **********  PowerReporter    ********** //
  _config.pr_temp_directory_path = _config.temp_directory_path + "power_reporter/";
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
  // ********* PowerPropagator    ********* //
  STAUTIL.createDir(_config.pp_temp_directory_path);
  // ********** TimingAnalyzer   ********* //
  STAUTIL.createDir(_config.ta_temp_directory_path);
  // ********** PowerAnalyzer    ********* //
  STAUTIL.createDir(_config.pa_temp_directory_path);
  // ******* TimingCharacterizer ******* //
  STAUTIL.createDir(_config.tc_temp_directory_path);
  // **********  TimingReporter   ********** //
  STAUTIL.createDir(_config.tr_temp_directory_path);
  // **********  PowerReporter    ********** //
  STAUTIL.createDir(_config.pr_temp_directory_path);
  // ************  SDFWriter  ************* //
  STAUTIL.createDir(_config.sw_temp_directory_path);
  /////////////////////////////////////////////
  STALOG.openLogFileStream(_config.log_file_path);
}

void DataManager::buildDatabase()
{
  buildInstanceList();
  buildNetList();
  buildInstanceTimingInfo();
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
  instance.get_check_arc_list().clear();
  for (TimingCheckArc& timing_check_arc : timing_cell.get_check_arc_list()) {
    instance.get_check_arc_list().push_back(makeInstanceTimingCheckArc(instance, timing_check_arc));
  }

  TimingCheckArc* representative_check_arc = nullptr;
  for (TimingCheckArc& timing_check_arc : timing_cell.get_check_arc_list()) {
    if (representative_check_arc == nullptr || timing_check_arc.get_check_type() == TimingCheckType::kSetup) {
      representative_check_arc = &timing_check_arc;
    }
    if (timing_check_arc.get_check_type() == TimingCheckType::kSetup) {
      break;
    }
  }
  if (representative_check_arc != nullptr) {
    instance.set_clock_pin_name(getInstancePinName(instance, representative_check_arc->get_clock_port()));
    instance.set_data_pin_name(getInstancePinName(instance, representative_check_arc->get_data_port()));
  }
}

TimingCheckArc DataManager::makeInstanceTimingCheckArc(Instance& instance, TimingCheckArc& timing_check_arc)
{
  TimingCheckArc instance_timing_check_arc;
  instance_timing_check_arc.set_clock_port(getInstancePinName(instance, timing_check_arc.get_clock_port()));
  instance_timing_check_arc.set_data_port(getInstancePinName(instance, timing_check_arc.get_data_port()));
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

void DataManager::readConstraint()
{
  Database& database = _database;
  std::string sdc_file_path = database.get_timing_constraint().get_sdc_file_path();
  database.get_timing_constraint().get_clock_map().clear();
  database.get_timing_constraint().get_port_constraint_map().clear();
  database.get_timing_constraint().get_case_analysis_map().clear();
  if (sdc_file_path.empty()) {
    return;
  }

  auto& sdc_command{Singleton<SdcCommand>::getInst()};
  if (sdc_command.evalScriptFile(sdc_file_path) != TCL_OK) {
    for (const SdcError& error : sdc_command.getErrors()) {
      STALOG.warn(Loc::current(), "SDC command failed in '", sdc_file_path, "' at line ", error.line_number, ": ", error.message);
    }
  }
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
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "endpoint_path_report_number");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), _config.endpoint_path_report_number);
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "has_timing_report_slack_lesser_than");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), _config.has_timing_report_slack_lesser_than);
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "timing_report_slack_lesser_than");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), _config.timing_report_slack_lesser_than);
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "has_timing_report_slack_greater_than");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), _config.has_timing_report_slack_greater_than);
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "timing_report_slack_greater_than");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), _config.timing_report_slack_greater_than);
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "output_timing_reports");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), _config.output_timing_reports);
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "output_timing_features");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), _config.output_timing_features);
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "timing_path_limit");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), _config.timing_path_limit);
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
  // ********* PowerPropagator    ********* //
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "PowerPropagator");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), "pp_temp_directory_path");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(3), _config.pp_temp_directory_path);
  // ********** TimingAnalyzer   ********* //
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "TimingAnalyzer");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), "ta_temp_directory_path");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(3), _config.ta_temp_directory_path);
  // ********** PowerAnalyzer    ********* //
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "PowerAnalyzer");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), "pa_temp_directory_path");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(3), _config.pa_temp_directory_path);
  // ******* TimingCharacterizer ******* //
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "TimingCharacterizer");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), "tc_temp_directory_path");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(3), _config.tc_temp_directory_path);
  // **********  TimingReporter   ********** //
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "TimingReporter");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), "tr_temp_directory_path");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(3), _config.tr_temp_directory_path);
  // **********  PowerReporter    ********** //
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "PowerReporter");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), "pr_temp_directory_path");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(3), _config.pr_temp_directory_path);
  // ************  SDFWriter  ************* //
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "SDFWriter");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), "sw_temp_directory_path");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(3), _config.sw_temp_directory_path);
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
