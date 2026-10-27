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
#include <unordered_map>

#include "ReadHierarchy.hpp"
#include "ReadLiberty.hpp"
#include "ReadNetwork.hpp"
#include "ReadParasitics.hpp"
#include "SdcWriter.hpp"
#include "TimingDataExporter.hpp"
#include "MinTimingDataExporter.hpp"
#include "SparseTimingSnapshot.hpp"

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
#include "sdc/TimingClock.hpp"
#include "sdc/TimingPortConstraint.hpp"

namespace ista {

namespace {

double modelTransitionPs(TimingClock& clock, TransType transition, double time_unit_ps)
{
  const auto max = clock.get_transition_map().find(AnalysisType::kMax);
  if (max != clock.get_transition_map().end()) {
    const auto value = max->second.find(transition);
    if (value != max->second.end())
      return value->second * time_unit_ps;
  }
  const auto min = clock.get_transition_map().find(AnalysisType::kMin);
  if (min != clock.get_transition_map().end()) {
    const auto value = min->second.find(transition);
    if (value != min->second.end())
      return value->second * time_unit_ps;
  }
  return 0.0;
}

void seedStructuralElectricalInputs(TimingData& snapshot, Database& database)
{
  std::unordered_map<std::string, TimingPinData*> pins;
  pins.reserve(snapshot.pins.size());
  for (auto& pin : snapshot.pins)
    pins.emplace(pin.name, &pin);

  // Input transition constraints are the electrical boundary for PI-launched
  // model states. Keep the seed in the same ps unit as the exported LUTs.
  for (const auto& [name, constraint] : database.sdc().get_port_constraint_map()) {
    const auto pin_it = database.network().pins().find(name);
    const auto exported = pins.find(name);
    if (pin_it == database.network().pins().end() || exported == pins.end() || !pin_it->second.get_is_port()
        || (pin_it->second.get_direction() != PinDirection::kInput && pin_it->second.get_direction() != PinDirection::kInout))
      continue;
    exported->second->max_rise_slew_ps = constraint.get_input_transition(AnalysisType::kMax, TransType::kRise) * snapshot.time_unit_ps;
    exported->second->max_fall_slew_ps = constraint.get_input_transition(AnalysisType::kMax, TransType::kFall) * snapshot.time_unit_ps;
  }

  // Clock pins have no numeric waveform in structural preparation. Use the
  // SDC ideal transition as the CK boundary so the normal LUT CK->Q arc still
  // evaluates with the same electrical seed as complete STA.
  for (const auto& pin_name : snapshot.clock_pins) {
    const auto exported = pins.find(pin_name);
    const auto point = database.graph().points().find(pin_name);
    if (exported == pins.end() || point == database.graph().points().end())
      continue;
    double rise = 0.0;
    double fall = 0.0;
    for (const auto& [clock_name, state] : point->second.get_clock_state_map()) {
      (void) state;
      const auto clock = database.sdc().get_clock_map().find(clock_name);
      if (clock == database.sdc().get_clock_map().end())
        continue;
      rise = std::max(rise, modelTransitionPs(clock->second, TransType::kRise, snapshot.time_unit_ps));
      fall = std::max(fall, modelTransitionPs(clock->second, TransType::kFall, snapshot.time_unit_ps));
    }
    exported->second->max_rise_slew_ps = rise;
    exported->second->max_fall_slew_ps = fall;
  }
}

}  // namespace

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
  if (config_map.contains("-min_slew_degradation")) {
    int32_t value = std::any_cast<int32_t>(config_map.at("-min_slew_degradation"));
    if (value != 0 && value != 1) {
      throw std::invalid_argument("-min_slew_degradation must be 0 or 1");
    }
  }
  // From this point initialization may mutate the shared design and constraints.
  _initialized = false;
  _model_ready = false;
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
  _model_ready = false;
  _timing_ready = false;

  STALOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void STAInterface::prepareTimingModel()
{
  if (!_initialized) {
    throw std::runtime_error("STA must be initialized before prepareTimingModel");
  }
  TimingAnalyzer analyzer(STADM.getDatabase(), STADC);
  if (_model_ready && analyzer.isStructureCurrent()) {
    return;
  }

  _model_ready = false;
  _timing_ready = false;
  Monitor monitor;
  STALOG.info(Loc::current(), "Starting structural timing model preparation...");
  analyzer.prepareStructure();
  _model_ready = true;
  _timing_ready = false;
  STALOG.info(Loc::current(), "Completed structural timing model preparation", monitor.getStatsInfo());
}

void STAInterface::runSTA()
{
  if (!_initialized) {
    throw std::runtime_error("STA must be initialized before runSTA");
  }
  _timing_ready = false;
  Monitor monitor;
  STALOG.info(Loc::current(), "Starting...");

  try {
    TimingAnalyzer(STADM.getDatabase(), STADC).analyze(_model_ready);

    TimingReporter(STADM.getDatabase(), STADM.getConfig()).report();

    SdfWriter(STADM.getDatabase(), STADM.getConfig()).write();

    STADC.clear();

    _timing_ready = true;
    _model_ready = true;
  } catch (...) {
    _timing_ready = false;
    _model_ready = false;
    throw;
  }

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

  try {
    TimingAnalyzer(STADM.getDatabase(), STADC).analyze();

    auto timing_model = ModelExtractor(STADM.getDatabase()).extract();
    std::string output_path = STADM.getConfig().tc_temp_directory_path;
    liberty_writer::writeLib(timing_model, STADM.getDatabase().liberty(), output_path);

    STADC.clear();
    _timing_ready = true;
    _model_ready = true;
  } catch (...) {
    _timing_ready = false;
    _model_ready = false;
    throw;
  }

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
  _model_ready = false;
  _timing_ready = false;

#ifdef __GLIBC__
  // Every STA object (liberty trees, wrapped netlist, timing graph, report
  // buffers, ...) has been deleted above, but glibc keeps the freed pages
  // mapped in its arenas, so repeated in-process STA sessions keep inflating
  // the process RSS. Hand every freeable heap page back to the OS here.
  malloc_trim(0);
#endif
}

TimingData STAInterface::exportTimingData()
{
  if (!isTimingReady()) {
    throw std::runtime_error("timing export requires initialized and executed current-main STA");
  }
  TimingData snapshot = TimingDataExporter::exportData(STADM.getDatabase(), STADM.getConfig());
  exportMinTimingData(snapshot, STADM.getDatabase(), STADM.getConfig());
  SparseTimingExportAdapter::compile(snapshot);
  return snapshot;
}

TimingData STAInterface::exportTimingModel()
{
  if (!isModelReady()) {
    throw std::runtime_error("timing model export requires initialized and prepared structural timing model");
  }
  Monitor monitor;
  STALOG.info(Loc::current(), "Starting structural timing snapshot export...");
  TimingData snapshot = TimingDataExporter::exportData(STADM.getDatabase(), STADM.getConfig());
  STALOG.info(Loc::current(), "Structural base snapshot ready: pins=", snapshot.pins.size(), ", arcs=", snapshot.graph_arcs.size(),
              ", diagnostics=", snapshot.diagnostics.size(), "; phase ", monitor.getStatsInfo());
  seedStructuralElectricalInputs(snapshot, STADM.getDatabase());
  STALOG.info(Loc::current(), "Structural electrical seeds ready; phase ", monitor.getStatsInfo());
  exportMinTimingModel(snapshot, STADM.getDatabase(), STADM.getConfig());
  STALOG.info(Loc::current(), "Structural min contexts ready: contexts=", snapshot.min_contexts.size(), "; phase ", monitor.getStatsInfo());
  SparseTimingExportAdapter::compileModel(snapshot);
  STALOG.info(Loc::current(), "Structural max/min sparse snapshots ready: states=", snapshot.sparse.states.size(), "/", snapshot.sparse_min.states.size(),
              "; phase ", monitor.getStatsInfo());
  snapshot.diagnostics.push_back("preparation_mode=model_only; numeric timing values are not reference data");
  return snapshot;
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
