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

#include "EMIRInterface.hpp"
#include "Logger.hpp"
#include "Monitor.hpp"
#include "Utility.hpp"

namespace iemir {

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
    EMIRLOG.error(Loc::current(), "The instance not initialized!");
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
  EMIRLOG.info(Loc::current(), "Starting...");

  EMIRI.input(config_map);
  buildConfig();
  buildDatabase();
  printConfig();
  printDatabase();

  EMIRLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void DataManager::output()
{
  Monitor monitor;
  EMIRLOG.info(Loc::current(), "Starting...");

  EMIRLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

// private

DataManager* DataManager::_dm_instance = nullptr;

#if 1  // build

void DataManager::buildConfig()
{
  /////////////////////////////////////////////
  // **********       EMIR        ********** //
  _config.temp_directory_path = std::filesystem::absolute(_config.temp_directory_path);
  _config.temp_directory_path += "/";
  _config.log_file_path = _config.temp_directory_path + "emir.log";
  // **********    DataManager    ********** //
  _config.dm_temp_directory_path = _config.temp_directory_path + "data_manager/";
  // **********   GraphBuilder    ********** //
  _config.gb_temp_directory_path = _config.temp_directory_path + "graph_builder/";
  // **********    IRAnalyzer     ********** //
  _config.ia_temp_directory_path = _config.temp_directory_path + "ir_analyzer/";
  // **********    EMAnalyzer     ********** //
  _config.ea_temp_directory_path = _config.temp_directory_path + "em_analyzer/";
  // **********   EMIRReporter    ********** //
  _config.er_temp_directory_path = _config.temp_directory_path + "emir_reporter/";
  /////////////////////////////////////////////
  // **********       EMIR        ********** //
  EMIRUTIL.removeDir(_config.temp_directory_path);
  EMIRUTIL.createDir(_config.temp_directory_path);
  EMIRUTIL.createDirByFile(_config.log_file_path);
  // **********    DataManager    ********** //
  EMIRUTIL.createDir(_config.dm_temp_directory_path);
  // **********   GraphBuilder    ********** //
  EMIRUTIL.createDir(_config.gb_temp_directory_path);
  // **********    IRAnalyzer     ********** //
  EMIRUTIL.createDir(_config.ia_temp_directory_path);
  // **********    EMAnalyzer     ********** //
  EMIRUTIL.createDir(_config.ea_temp_directory_path);
  // **********   EMIRReporter    ********** //
  EMIRUTIL.createDir(_config.er_temp_directory_path);
  /////////////////////////////////////////////
  EMIRLOG.openLogFileStream(_config.log_file_path);
}

void DataManager::buildDatabase()
{
  readInstancePower();
}

void DataManager::readInstancePower()
{
  if (_config.instance_power_file_path.empty()) {
    EMIRLOG.error(Loc::current(), "The instance_power_file_path is empty!");
  }

  std::ifstream* instance_power_file = EMIRUTIL.getInputFileStream(_config.instance_power_file_path);
  uint64_t instance_power_num = 0;
  readInstancePowerHeader(instance_power_file, instance_power_num);

  if (instance_power_num > (std::numeric_limits<uint64_t>::max() - 24) / 44) {
    EMIRLOG.error(Loc::current(), "The instance power file record count is invalid!");
  }
  instance_power_file->seekg(0, std::ios::end);
  std::streamoff file_size = instance_power_file->tellg();
  std::streamoff expected_file_size = static_cast<std::streamoff>(24 + instance_power_num * 44);
  if (file_size != expected_file_size) {
    EMIRLOG.error(Loc::current(), "The instance power file length is inconsistent!");
  }
  instance_power_file->seekg(24, std::ios::beg);

  _database.get_instance_power_map().clear();
  for (uint64_t instance_power_idx = 0; instance_power_idx < instance_power_num; instance_power_idx++) {
    readInstancePowerRecord(instance_power_file);
  }
  EMIRUTIL.closeFileStream(instance_power_file);
}

void DataManager::readInstancePowerHeader(std::ifstream* instance_power_file, uint64_t& instance_power_num)
{
  char magic[8];
  uint32_t version = 0;
  uint32_t record_size = 0;
  instance_power_file->read(magic, static_cast<std::streamsize>(sizeof(magic)));
  instance_power_file->read(reinterpret_cast<char*>(&version), static_cast<std::streamsize>(sizeof(version)));
  instance_power_file->read(reinterpret_cast<char*>(&record_size), static_cast<std::streamsize>(sizeof(record_size)));
  instance_power_file->read(reinterpret_cast<char*>(&instance_power_num), static_cast<std::streamsize>(sizeof(instance_power_num)));
  if (!(*instance_power_file)) {
    EMIRLOG.error(Loc::current(), "The instance power file header is incomplete!");
  }
  std::array<char, 8> expected_magic = {'I', 'S', 'T', 'A', 'P', 'W', 'R', '\0'};
  if (!std::equal(std::begin(magic), std::end(magic), expected_magic.begin())) {
    EMIRLOG.error(Loc::current(), "The instance power file magic is invalid!");
  }
  if (version != 1) {
    EMIRLOG.error(Loc::current(), "The instance power file version is invalid!");
  }
  if (record_size != 44) {
    EMIRLOG.error(Loc::current(), "The instance power file record size is invalid!");
  }
}

void DataManager::readInstancePowerRecord(std::ifstream* instance_power_file)
{
  InstancePower instance_power;
  uint64_t instance_id = 0;
  uint32_t power_group_type = 0;
  double voltage = 0.0;
  double internal_power = 0.0;
  double switching_power = 0.0;
  double leakage_power = 0.0;
  instance_power_file->read(reinterpret_cast<char*>(&instance_id), static_cast<std::streamsize>(sizeof(instance_id)));
  instance_power_file->read(reinterpret_cast<char*>(&power_group_type), static_cast<std::streamsize>(sizeof(power_group_type)));
  instance_power_file->read(reinterpret_cast<char*>(&voltage), static_cast<std::streamsize>(sizeof(voltage)));
  instance_power_file->read(reinterpret_cast<char*>(&internal_power), static_cast<std::streamsize>(sizeof(internal_power)));
  instance_power_file->read(reinterpret_cast<char*>(&switching_power), static_cast<std::streamsize>(sizeof(switching_power)));
  instance_power_file->read(reinterpret_cast<char*>(&leakage_power), static_cast<std::streamsize>(sizeof(leakage_power)));
  if (!(*instance_power_file)) {
    EMIRLOG.error(Loc::current(), "The instance power file record is incomplete!");
  }
  if (_database.get_instance_id_set().count(instance_id) == 0) {
    EMIRLOG.error(Loc::current(), "The instance power file references an unknown instance!");
  }
  if (_database.get_instance_power_map().count(instance_id) != 0) {
    EMIRLOG.error(Loc::current(), "The instance power file contains duplicate instance records!");
  }
  instance_power.set_instance_id(instance_id);
  instance_power.set_power_group_type(power_group_type);
  instance_power.set_voltage(voltage);
  instance_power.set_internal_power(internal_power);
  instance_power.set_switching_power(switching_power);
  instance_power.set_leakage_power(leakage_power);
  _database.get_instance_power_map()[instance_id] = instance_power;
}

#endif

#if 1  // exhibit

void DataManager::printConfig()
{
  EMIRLOG.info(Loc::current(), EMIRUTIL.getSpaceByTabNum(0), "EMIR_CONFIG_INPUT");
  EMIRLOG.info(Loc::current(), EMIRUTIL.getSpaceByTabNum(1), "temp_directory_path");
  EMIRLOG.info(Loc::current(), EMIRUTIL.getSpaceByTabNum(2), _config.temp_directory_path);
  EMIRLOG.info(Loc::current(), EMIRUTIL.getSpaceByTabNum(1), "instance_power_file_path");
  EMIRLOG.info(Loc::current(), EMIRUTIL.getSpaceByTabNum(2), _config.instance_power_file_path);
  EMIRLOG.info(Loc::current(), EMIRUTIL.getSpaceByTabNum(1), "thread_number");
  EMIRLOG.info(Loc::current(), EMIRUTIL.getSpaceByTabNum(2), _config.thread_number);
}

void DataManager::printDatabase()
{
  EMIRLOG.info(Loc::current(), EMIRUTIL.getSpaceByTabNum(0), "EMIR_DATABASE_INPUT");
  EMIRLOG.info(Loc::current(), EMIRUTIL.getSpaceByTabNum(1), "design_name");
  EMIRLOG.info(Loc::current(), EMIRUTIL.getSpaceByTabNum(2), _database.get_design_name());
  EMIRLOG.info(Loc::current(), EMIRUTIL.getSpaceByTabNum(1), "power_net_num");
  EMIRLOG.info(Loc::current(), EMIRUTIL.getSpaceByTabNum(2), _database.get_power_net_map().size());
  EMIRLOG.info(Loc::current(), EMIRUTIL.getSpaceByTabNum(1), "instance_power_num");
  EMIRLOG.info(Loc::current(), EMIRUTIL.getSpaceByTabNum(2), _database.get_instance_power_map().size());
}

#endif

}  // namespace iemir
