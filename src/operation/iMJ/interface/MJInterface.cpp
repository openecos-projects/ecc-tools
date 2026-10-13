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
#include "MJInterface.hpp"

#include "AntennaChecker.hpp"
#include "DataManager.hpp"
#include "DefFlattener.hpp"
#include "FillerInserter.hpp"
#include "Logger.hpp"
#include "MetalInserter.hpp"
#include "Monitor.hpp"
#include "Utility.hpp"

namespace imj {

// public

void MJInterface::destroyInst()
{
  if (_mj_interface_instance != nullptr) {
    delete _mj_interface_instance;
    _mj_interface_instance = nullptr;
  }
}

#if 1  // 外部调用MJ的API

#if 1  // iMJ

void MJInterface::initMJ(std::map<std::string, std::any> config_map)
{
  Logger::initInst();
  // clang-format off
  MJLOG.info(Loc::current(), ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>");
  MJLOG.info(Loc::current(), "___________  ___________   _____________________________________ ");
  MJLOG.info(Loc::current(), "___(_)__   |/  /_____  /   __  ___/__  __/__    |__  __ \\__  __/");
  MJLOG.info(Loc::current(), "__  /__  /|_/ /___ _  /    _____ \\__  /  __  /| |_  /_/ /_  /   ");
  MJLOG.info(Loc::current(), "_  / _  /  / / / /_/ /     ____/ /_  /   _  ___ |  _, _/_  /     ");
  MJLOG.info(Loc::current(), "/_/  /_/  /_/  \\____/      /____/ /_/    /_/  |_/_/ |_| /_/     ");
  MJLOG.info(Loc::current(), ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>");
  // clang-format on
  MJLOG.printLogFilePath();
  //////////////////////////////////////////////////////
  //////////////////////////////////////////////////////
  //////////////////////////////////////////////////////
  Monitor monitor;
  MJLOG.info(Loc::current(), "Starting...");

  DataManager::initInst();
  MJDM.input(config_map);

  MJLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void MJInterface::insertFiller(std::map<std::string, std::any> config_map)
{
  FillerInserter::initInst();
  MJFI.insert(config_map);
  FillerInserter::destroyInst();
}

void MJInterface::insertMetal(std::map<std::string, std::any> config_map)
{
  MetalInserter::initInst();
  MJMI.insert(config_map);
  MetalInserter::destroyInst();
}

void MJInterface::checkAntenna(std::map<std::string, std::any> config_map)
{
  AntennaChecker::initInst();
  MJAC.check(config_map);
  AntennaChecker::destroyInst();
}

void MJInterface::flattenDef(std::map<std::string, std::any> config_map)
{
  DefFlattener::initInst();
  MJDF.flatten(config_map);
  DefFlattener::destroyInst();
}

void MJInterface::destroyMJ()
{
  Monitor monitor;
  MJLOG.info(Loc::current(), "Starting...");

  MJDM.output();
  DataManager::destroyInst();

  MJLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());

  MJLOG.printLogFilePath();
  // clang-format off
  MJLOG.info(Loc::current(), ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>");
  MJLOG.info(Loc::current(), "___________  ___________   _____________________   _____________________  __ ");
  MJLOG.info(Loc::current(), "___(_)__   |/  /_____  /   ___  ____/___  _/__  | / /___  _/_  ___/__  / / / ");
  MJLOG.info(Loc::current(), "__  /__  /|_/ /___ _  /    __  /_    __  / __   |/ / __  / _____ \\__  /_/ / ");
  MJLOG.info(Loc::current(), "_  / _  /  / / / /_/ /     _  __/   __/ /  _  /|  / __/ /  ____/ /_  __  /   ");
  MJLOG.info(Loc::current(), "/_/  /_/  /_/  \\____/      /_/      /___/  /_/ |_/  /___/  /____/ /_/ /_/   ");
  MJLOG.info(Loc::current(), ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>");
  // clang-format on
  Logger::destroyInst();
}

#endif

#endif

#if 1  // MJ调用外部的API

#if 1  // TopData

#if 1  // input

void MJInterface::input(std::map<std::string, std::any>& config_map)
{
  wrapConfig(config_map);
}

void MJInterface::wrapConfig(std::map<std::string, std::any>& config_map)
{
  MJDM.getConfig().temp_directory_path = MJUTIL.getConfigValue<std::string>(config_map, "-temp_directory_path", "./mj_temp_directory");
}

#endif

#if 1  // output

void MJInterface::output()
{
}

#endif

#endif

#endif

// private

MJInterface* MJInterface::_mj_interface_instance = nullptr;

}  // namespace imj
