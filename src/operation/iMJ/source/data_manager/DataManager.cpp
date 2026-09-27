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
#include "DataManager.hpp"

#include "Utility.hpp"
#include "MJInterface.hpp"

namespace imj {

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
    MJLOG.error(Loc::current(), "The instance not initialized!");
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
  MJLOG.info(Loc::current(), "Starting...");

  MJI.input(config_map);
  buildConfig();
  printConfig();

  MJLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void DataManager::output()
{
  Monitor monitor;
  MJLOG.info(Loc::current(), "Starting...");

  MJI.output();

  MJLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

// private

DataManager* DataManager::_dm_instance = nullptr;

#if 1  // build

void DataManager::buildConfig()
{
  /////////////////////////////////////////////
  // **********        MJ         ********** //
  _config.temp_directory_path = std::filesystem::absolute(_config.temp_directory_path);
  _config.temp_directory_path += "/";
  _config.log_file_path = _config.temp_directory_path + "mj.log";
  // **********    DataManager    ********** //
  _config.dm_temp_directory_path = _config.temp_directory_path + "data_manager/";
  // **********  AntennaChecker   ********** //
  _config.ac_temp_directory_path = _config.temp_directory_path + "antenna_checker/";
  // **********   DefFlattener    ********** //
  _config.df_temp_directory_path = _config.temp_directory_path + "def_flattener/";
  // **********  FillerInserter   ********** //
  _config.fi_temp_directory_path = _config.temp_directory_path + "filler_inserter/";
  // **********  MetalInserter    ********** //
  _config.mi_temp_directory_path = _config.temp_directory_path + "metal_inserter/";

  /////////////////////////////////////////////
  // **********        MJ         ********** //
  MJUTIL.removeDir(_config.temp_directory_path);
  MJUTIL.createDir(_config.temp_directory_path);
  MJUTIL.createDirByFile(_config.log_file_path);
  // **********    DataManager    ********** //
  MJUTIL.createDir(_config.dm_temp_directory_path);
  // **********  AntennaChecker   ********** //
  MJUTIL.createDir(_config.ac_temp_directory_path);
  // **********   DefFlattener    ********** //
  MJUTIL.createDir(_config.df_temp_directory_path);
  // **********  FillerInserter   ********** //
  MJUTIL.createDir(_config.fi_temp_directory_path);
  // **********  MetalInserter    ********** //
  MJUTIL.createDir(_config.mi_temp_directory_path);
  /////////////////////////////////////////////
  MJLOG.openLogFileStream(_config.log_file_path);
}

#endif

#if 1  // exhibit

void DataManager::printConfig()
{
  /////////////////////////////////////////////
  // **********        MJ         ********** //
  MJLOG.info(Loc::current(), MJUTIL.getSpaceByTabNum(0), "MJ_CONFIG_INPUT");
  MJLOG.info(Loc::current(), MJUTIL.getSpaceByTabNum(1), "temp_directory_path");
  MJLOG.info(Loc::current(), MJUTIL.getSpaceByTabNum(2), _config.temp_directory_path);

  // **********        MJ         ********** //
  MJLOG.info(Loc::current(), MJUTIL.getSpaceByTabNum(0), "MJ_CONFIG_BUILD");
  MJLOG.info(Loc::current(), MJUTIL.getSpaceByTabNum(1), "log_file_path");
  MJLOG.info(Loc::current(), MJUTIL.getSpaceByTabNum(2), _config.log_file_path);
  // **********    DataManager    ********** //
  MJLOG.info(Loc::current(), MJUTIL.getSpaceByTabNum(1), "DataManager");
  MJLOG.info(Loc::current(), MJUTIL.getSpaceByTabNum(2), _config.dm_temp_directory_path);
  // **********  AntennaChecker   ********** //
  MJLOG.info(Loc::current(), MJUTIL.getSpaceByTabNum(1), "AntennaChecker");
  MJLOG.info(Loc::current(), MJUTIL.getSpaceByTabNum(2), _config.ac_temp_directory_path);
  // **********   DefFlattener    ********** //
  MJLOG.info(Loc::current(), MJUTIL.getSpaceByTabNum(1), "DefFlattener");
  MJLOG.info(Loc::current(), MJUTIL.getSpaceByTabNum(2), _config.df_temp_directory_path);
  // **********  FillerInserter   ********** //
  MJLOG.info(Loc::current(), MJUTIL.getSpaceByTabNum(1), "FillerInserter");
  MJLOG.info(Loc::current(), MJUTIL.getSpaceByTabNum(2), _config.fi_temp_directory_path);
  // **********  MetalInserter    ********** //
  MJLOG.info(Loc::current(), MJUTIL.getSpaceByTabNum(1), "MetalInserter");
  MJLOG.info(Loc::current(), MJUTIL.getSpaceByTabNum(2), _config.mi_temp_directory_path);
  /////////////////////////////////////////////
}

#endif

}  // namespace imj
