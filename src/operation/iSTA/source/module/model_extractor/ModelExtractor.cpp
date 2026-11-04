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
#include "ModelExtractor.hpp"

#include <string>
#include <utility>

#include "Logger.hpp"
#include "Monitor.hpp"

namespace ista {
TimingModel ModelExtractor::extract()
{
  Monitor monitor;
  STALOG.info(Loc::current(), "Starting...");

  TimingModel tc_model;
  extractLibraries(tc_model);

  STALOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
  return tc_model;
}

void ModelExtractor::extractLibraries(TimingModel& tc_model)
{
  extractLibrary(tc_model, AnalysisType::kMax);
  extractLibrary(tc_model, AnalysisType::kMin);
}

void ModelExtractor::extractLibrary(TimingModel& tc_model, AnalysisType analysis_type)
{
  Database& database = _database;
  CharacterizedLibrary tc_lib;
  tc_lib.set_design_name(database.network().name());
  tc_lib.set_analysis_type(analysis_type);
  tc_lib.set_area(getDesignArea());
  buildPorts(tc_lib);
  extractClockArcs(tc_lib);
  _check_arc_extractor.extract(tc_lib, analysis_type);
  _delay_arc_extractor.extract(tc_lib, analysis_type);
  tc_model.get_lib_list().push_back(tc_lib);
}

double ModelExtractor::getDesignArea()
{
  Database& database = _database;
  double design_area = 0.0;
  for (std::pair<const std::string, Instance>& instance_pair : database.network().instances()) {
    Instance& instance = instance_pair.second;
    if (database.liberty().get_cell_map().count(instance.get_cell_name()) == 0) {
      continue;
    }
    design_area += database.liberty().get_cell_map()[instance.get_cell_name()].get_area();
  }
  return design_area;
}
void ModelExtractor::buildPorts(CharacterizedLibrary& tc_lib)
{
  Database& database = _database;
  for (std::pair<const std::string, Pin>& pin_pair : database.network().pins()) {
    Pin& pin = pin_pair.second;
    if (pin.get_is_port()) {
      buildCharacterizedPort(tc_lib, pin);
    }
  }
}

void ModelExtractor::buildCharacterizedPort(CharacterizedLibrary& tc_lib, Pin& pin)
{
  CharacterizedPort tc_port;
  tc_port.set_port_name(pin.get_pin_name());
  tc_port.set_direction(pin.get_direction());
  tc_port.set_is_clock(_model_path_query.isClockPort(pin.get_full_name()));
  tc_port.set_capacitance(getPortCapacitance(pin));
  tc_lib.get_port_map()[tc_port.get_port_name()] = tc_port;
}

double ModelExtractor::getPortCapacitance(Pin& pin)
{
  Database& database = _database;
  if (pin.get_net_name().empty() || database.network().nets().count(pin.get_net_name()) == 0) {
    return 0.0;
  }
  Net& net = database.network().nets()[pin.get_net_name()];
  double port_capacitance = 0.0;
  for (std::string& load_pin_name : net.get_load_pin_list()) {
    if (database.network().pins().count(load_pin_name) == 0) {
      continue;
    }
    Pin& load_pin = database.network().pins()[load_pin_name];
    if (load_pin.get_is_port() || database.network().instances().count(load_pin.get_instance_name()) == 0) {
      continue;
    }
    Instance& load_instance = database.network().instances()[load_pin.get_instance_name()];
    if (database.liberty().get_cell_map().count(load_instance.get_cell_name()) == 0) {
      continue;
    }
    LibertyCell& timing_cell = database.liberty().get_cell_map()[load_instance.get_cell_name()];
    if (timing_cell.get_port_map().count(load_pin.get_pin_name()) == 0) {
      continue;
    }
    port_capacitance += timing_cell.get_port_map()[load_pin.get_pin_name()].get_capacitance();
  }
  return port_capacitance;
}

void ModelExtractor::extractClockArcs(CharacterizedLibrary& tc_lib)
{
  Database& database = _database;
  for (std::pair<const std::string, TimingClock>& clock_pair : database.sdc().get_clock_map()) {
    for (std::string& clock_port_name : clock_pair.second.get_source_list()) {
      if (_model_path_query.isClockPort(clock_port_name)) {
        buildClockPathArc(tc_lib, clock_port_name, "min_clock_tree_path");
        buildClockPathArc(tc_lib, clock_port_name, "max_clock_tree_path");
      }
    }
  }
}

void ModelExtractor::buildClockPathArc(CharacterizedLibrary& tc_lib, std::string& clock_port_name, std::string timing_type)
{
  CharacterizedTimingArc timing_arc;
  timing_arc.set_sink_port(clock_port_name);
  timing_arc.set_timing_type(timing_type);
  timing_arc.set_timing_sense("positive_unate");
  timing_arc.addScalarTable("cell_rise", 0.0);
  timing_arc.addScalarTable("cell_fall", 0.0);
  tc_lib.get_timing_arc_list().push_back(timing_arc);
}
}  // namespace ista
