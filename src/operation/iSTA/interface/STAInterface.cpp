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

#include <any>
#include <cmath>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>

#include "ReadHierarchy.hpp"
#include "ReadLiberty.hpp"
#include "ReadNetwork.hpp"
#include "ReadParasitics.hpp"
#include "SdcWriter.hpp"
#include "TimingDataExporter.hpp"

#ifdef __GLIBC__
#include <malloc.h>
#endif

#include <chrono>

#include "DataManager.hpp"
#include "DelayCalculator.hpp"
#include "LibertyWriter.hpp"
#include "Logger.hpp"
#include "ModelExtractor.hpp"
#include "Monitor.hpp"
#include "SdcCommand.hpp"
#include "SdcReader.hpp"
#include "SdfWriter.hpp"
#include "TimingAnalyzer.hpp"
#include "TimingReporter.hpp"
#include "Utility.hpp"
#include "idm.h"

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

void STAInterface::initSTA(std::map<std::string, std::any> config_map)
{
  for (const std::string key : {"-min_slew_degradation", "-adjust_driver_resistance"}) {
    if (config_map.contains(key)) {
      int32_t value = std::any_cast<int32_t>(config_map.at(key));
      if (value != 0 && value != 1)
        throw std::invalid_argument(key + " must be 0 or 1");
    }
  }
  // From this point initialization may mutate the shared design and constraints.
  _initialized = false;
  _timing_ready = false;
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
  readDesign(config_map);
  DelayCalculator::initInst();
  SdcCommand::initInst();
  sdc::registerSdcCommands(SdcCommand::getInst());
  if (config_map.contains("-hierarchy_verilog")) {
    const auto file = std::any_cast<std::string>(config_map.at("-hierarchy_verilog"));
    if (!file.empty())
      readHierarchy(STADM.getDatabase().network(), *dmInst->get_idb_layout(), file);
  }
  // Simulation values are derived from constraints and are rebuilt by analysis.
  STADM.getDatabase().graph().simValues().clear();
  readSdc(STADM.getDatabase().sdc());

  _initialized = true;
  _timing_ready = false;

  STALOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void STAInterface::runSTA()
{
  if (!_initialized) {
    throw std::runtime_error("STA must be initialized before runSTA");
  }
  _timing_ready = false;
  Monitor monitor;
  STALOG.info(Loc::current(), "Starting...");

  TimingAnalyzer(STADM.getDatabase(), STADC).analyze();

  TimingReporter(STADM.getDatabase(), STADM.getConfig()).report();

  STADC.completeCellArcDelays(STADM.getDatabase().graph());
  SdfWriter(STADM.getDatabase(), STADM.getConfig()).write();

  STADC.clear();

  _analyzed_sdc_revision = STADM.getDatabase().sdc().revision();
  _timing_ready = true;

  STALOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void STAInterface::writeSdc(const std::string& file_path)
{
  if (!_initialized) {
    throw std::runtime_error("STA must be initialized before write_sdc");
  }
  sdc::writeSdcFile(STADM.getDatabase(), file_path);
}

void STAInterface::extractLib()
{
  if (!_initialized) {
    throw std::runtime_error("STA must be initialized before extractLib");
  }
  _timing_ready = false;
  Monitor monitor;
  STALOG.info(Loc::current(), "Starting...");

  TimingAnalyzer(STADM.getDatabase(), STADC).analyze();

  auto timing_model = ModelExtractor(STADM.getDatabase()).extract();
  std::string output_path = STADM.getConfig().tc_temp_directory_path;
  liberty_writer::writeLib(timing_model, STADM.getDatabase().liberty(), output_path);

  STADC.clear();
  _analyzed_sdc_revision = STADM.getDatabase().sdc().revision();
  _timing_ready = true;

  STALOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void STAInterface::destroySTA()
{
  Monitor monitor;
  STALOG.info(Loc::current(), "Starting...");

  STADC.clear();
  DelayCalculator::destroyInst();
  {
    Monitor monitor;
    STALOG.info(Loc::current(), "Starting...");
    STALOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
  }
  SdcCommand::destroyInst();
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

  _initialized = false;
  _timing_ready = false;

#ifdef __GLIBC__
  // Every STA object (liberty trees, wrapped netlist, timing graph, report
  // buffers, ...) has been deleted above, but glibc keeps the freed pages
  // mapped in its arenas, so repeated in-process STA sessions keep inflating
  // the process RSS. Hand every freeable heap page back to the OS here.
  malloc_trim(0);
#endif
}

bool STAInterface::isTimingReady() const
{
  return _initialized && _timing_ready && DataManager::isInitialized() && _analyzed_sdc_revision == STADM.getDatabase().sdc().revision();
}

TimingData STAInterface::exportTimingData()
{
  if (!isTimingReady()) {
    throw std::runtime_error("timing export requires initialized and executed current-main STA");
  }
  return TimingDataExporter::exportData(STADM.getDatabase(), STADM.getConfig());
}

void STAInterface::readDesign(std::map<std::string, std::any>& config_map)
{
  Monitor monitor;
  STALOG.info(Loc::current(), "Starting...");
  STADM.readConfig(config_map);
  STADM.getDatabase().sdc().set_sdc_file_path(dmInst->get_config().get_sdc_path());
  readNetwork(STADM.getDatabase().network());
  ReadLiberty(STADM.getDatabase().liberty(), STADM.getDatabase().network()).read();
  ReadParasitics(STADM.getDatabase().parasitics()).read();
  STADM.buildDesign();
  STALOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

STAInterface* STAInterface::_sta_interface_instance = nullptr;

}  // namespace ista
