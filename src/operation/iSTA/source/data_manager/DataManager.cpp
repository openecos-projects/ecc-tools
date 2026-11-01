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

#include <omp.h>

#include <algorithm>
#include <any>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <utility>

#include "Logger.hpp"
#include "Monitor.hpp"
#include "Utility.hpp"
#include "network/LinkNetwork.hpp"

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

void DataManager::readConfig(std::map<std::string, std::any>& config_map)
{
  Config& config = _config;
  /////////////////////////////////////////////
  config.temp_directory_path = STAUTIL.getConfigValue<std::string>(config_map, "-temp_directory_path", "./sta_temp_directory");
  config.thread_number = STAUTIL.getConfigValue<int32_t>(config_map, "-thread_number", 128);
  config.output_timing_reports = STAUTIL.getConfigValue<int32_t>(config_map, "-output_timing_reports", 1);
  config.output_timing_features = STAUTIL.getConfigValue<int32_t>(config_map, "-output_timing_features", 1);
  config.timing_path_limit = STAUTIL.getConfigValue<int32_t>(config_map, "-timing_path_limit", 20);
  config.min_slew_degradation = STAUTIL.getConfigValue<int32_t>(config_map, "-min_slew_degradation", 0);
  config.adjust_driver_resistance = STAUTIL.getConfigValue<int32_t>(config_map, "-adjust_driver_resistance", 1);
  config.timing_corner = STAUTIL.getConfigValue<std::string>(config_map, "-timing_corner", "");
  config.is_path_report_number_specified = STAUTIL.exist(config_map, std::string("-max_paths")) || STAUTIL.exist(config_map, std::string("-max_path"))
                                           || STAUTIL.exist(config_map, std::string("-path_report_number"));
  config.path_report_number = STAUTIL.getConfigValue<int32_t>(config_map, "-max_paths", 1);
  if (STAUTIL.exist(config_map, std::string("-max_path"))) {
    config.path_report_number = std::any_cast<int32_t>(config_map["-max_path"]);
  }
  if (STAUTIL.exist(config_map, std::string("-path_report_number"))) {
    config.path_report_number = std::any_cast<int32_t>(config_map["-path_report_number"]);
  }
  config.endpoint_path_report_number = STAUTIL.getConfigValue<int32_t>(config_map, "-nworst", 1);
  if (!config.is_path_report_number_specified && config.endpoint_path_report_number > 1) {
    config.path_report_number = config.endpoint_path_report_number;
  }
  config.has_timing_report_slack_lesser_than = STAUTIL.exist(config_map, std::string("-slack_lesser_than"));
  if (config.has_timing_report_slack_lesser_than) {
    config.timing_report_slack_lesser_than = std::any_cast<double>(config_map["-slack_lesser_than"]);
  }
  config.has_timing_report_slack_greater_than = STAUTIL.exist(config_map, std::string("-slack_greater_than"));
  if (config.has_timing_report_slack_greater_than) {
    config.timing_report_slack_greater_than = std::any_cast<double>(config_map["-slack_greater_than"]);
  }
  omp_set_num_threads(std::max(config.thread_number, 1));
  /////////////////////////////////////////////
}

// private

DataManager* DataManager::_dm_instance = nullptr;

#if 1  // build

void DataManager::buildDesign()
{
  buildConfig();
  LinkNetwork(_database.network(), _database.liberty()).link();
  printConfig();
  printDatabase();
}

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
  // ********* DataPropagator   ********* //
  _config.tp_temp_directory_path = _config.temp_directory_path + "timing_propagator/";
  // ********** TimingAnalyzer   ********* //
  _config.ta_temp_directory_path = _config.temp_directory_path + "timing_analyzer/";
  // ******* TimingCharacterizer ******* //
  _config.tc_temp_directory_path = _config.temp_directory_path + "timing_characterizer/";
  // **********  TimingReporter   ********** //
  _config.tr_temp_directory_path = _config.temp_directory_path + "timing_reporter/";
  // ************  SdfWriter  ************* //
  _config.sw_temp_directory_path = _config.temp_directory_path + "sdf_writer/";
  /////////////////////////////////////////////
  // **********        STA        ********** //
  // The caller owns the parent directory. Refresh only the fixed STA output
  // subdirectories; inputs and other files in the parent must survive init.
  // Do not append '/' here: remove_all must unlink a child symlink rather than
  // traversing its target.
  for (const char* name : {"data_manager", "graph_builder", "delay_calculator", "clock_propagator", "timing_propagator", "timing_analyzer",
                           "timing_characterizer", "timing_reporter", "sdf_writer"}) {
    STAUTIL.removeDir((std::filesystem::path(_config.temp_directory_path) / name).string());
  }
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
  // ********* DataPropagator   ********* //
  STAUTIL.createDir(_config.tp_temp_directory_path);
  // ********** TimingAnalyzer   ********* //
  STAUTIL.createDir(_config.ta_temp_directory_path);
  // ******* TimingCharacterizer ******* //
  STAUTIL.createDir(_config.tc_temp_directory_path);
  // **********  TimingReporter   ********** //
  STAUTIL.createDir(_config.tr_temp_directory_path);
  // ************  SdfWriter  ************* //
  STAUTIL.createDir(_config.sw_temp_directory_path);
  /////////////////////////////////////////////
  STALOG.openLogFileStream(_config.log_file_path);
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
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "min_slew_degradation");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), _config.min_slew_degradation);
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "adjust_driver_resistance");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), _config.adjust_driver_resistance);
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
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "ArcDelayCalculator");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), "dc_temp_directory_path");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(3), _config.dc_temp_directory_path);
  // ******** ClockPropagator    ********* //
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "ClockPropagator");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), "cp_temp_directory_path");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(3), _config.cp_temp_directory_path);
  // ********* DataPropagator   ********* //
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "DataPropagator");
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
  // ************  SdfWriter  ************* //
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "SdfWriter");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), "sw_temp_directory_path");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(3), _config.sw_temp_directory_path);
  /////////////////////////////////////////////
}

void DataManager::printDatabase()
{
  std::size_t port_num = 0;
  for (std::pair<const std::string, Pin>& pin_pair : _database.network().pins()) {
    if (pin_pair.second.get_is_port()) {
      port_num++;
    }
  }
  /////////////////////////////////////////////
  // **********        STA        ********** //
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(0), "STA_DATABASE");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "design_name");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), _database.network().name());
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "instance_num");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), _database.network().instances().size());
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "port_num");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), port_num);
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "pin_num");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), _database.network().pins().size());
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(1), "net_num");
  STALOG.info(Loc::current(), STAUTIL.getSpaceByTabNum(2), _database.network().nets().size());
  /////////////////////////////////////////////
}

#endif

}  // namespace ista
