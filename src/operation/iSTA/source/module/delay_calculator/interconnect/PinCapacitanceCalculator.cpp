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
#include "PinCapacitanceCalculator.hpp"

#include <cstdlib>
#include <algorithm>
#include <limits>
#include <map>
#include <string>

namespace ista {

double PinCapacitanceCalculator::driverCapacitance(const std::string& pin_name, AnalysisType analysis_type, TransType trans_type)
{
  const auto pin = _database.network().pins().find(pin_name);
  if (pin == _database.network().pins().end() || (!pin->second.get_is_port() && pin->second.get_direction() != PinDirection::kInout)) {
    return 0.0;
  }
  return pinCapacitance(pin->second.get_full_name(), analysis_type, trans_type);
}

double PinCapacitanceCalculator::pinCapacitance(std::string& pin_name, AnalysisType analysis_type, TransType trans_type)
{
  Database& database = _database;
  Pin& pin = database.network().pins()[pin_name];
  if (pin.get_is_port()) {
    std::map<std::string, TimingPortConstraint>& port_constraint_map = database.sdc().get_port_constraint_map();
    if (port_constraint_map.count(pin_name) > 0 && port_constraint_map[pin_name].get_has_load()) {
      return port_constraint_map[pin_name].get_load(analysis_type, trans_type);
    }
    return 0.0;
  }
  if (database.network().instances().count(pin.get_instance_name()) == 0) {
    return 0.0;
  }
  Instance& instance = database.network().instances()[pin.get_instance_name()];
  std::map<std::string, LibertyCell>& timing_cell_map = database.liberty().get_cell_map();
  if (timing_cell_map.count(instance.get_cell_name()) == 0) {
    return 0.0;
  }
  LibertyCell& timing_cell = timing_cell_map[instance.get_cell_name()];
  if (timing_cell.get_port_map().count(pin.get_pin_name()) == 0) {
    return 0.0;
  }
  LibertyPort& timing_cell_port = timing_cell.get_port_map()[pin.get_pin_name()];
  // Replacing the per-edge pin capacitance with the plain `capacitance` was tried and
  // refuted at suite level: gcd 88.1% -> 13.9%, ip2 60.0% -> 11.7%. The per-edge
  // `rise_capacitance`/`fall_capacitance` are required; REF's `Total capacitance` report
  // is a separate bookkeeping quantity, not the load used for table lookup.
  {
    static const char* pc = std::getenv("ISTA_DUMP_PIN_CAP2");
    if (pc != nullptr) {
      if (FILE* f = std::fopen(pc, "a"); f != nullptr) {
        const bool has = timing_cell_port.get_trans_capacitance_map().count(analysis_type) > 0
                         && timing_cell_port.get_trans_capacitance_map()[analysis_type].count(trans_type) > 0;
        std::fprintf(f, "%s,%s,%s,%d,has=%d,plain=%.12g,trans=%.12g\n", instance.get_cell_name().c_str(), pin.get_pin_name().c_str(),
                     pin.get_instance_name().c_str(), static_cast<int>(trans_type), has ? 1 : 0, timing_cell_port.get_capacitance(),
                     has ? timing_cell_port.get_trans_capacitance_map()[analysis_type][trans_type] : -1.0);
        std::fclose(f);
      }
    }
  }
  if (timing_cell_port.get_trans_capacitance_map().count(analysis_type) > 0
      && timing_cell_port.get_trans_capacitance_map()[analysis_type].count(trans_type) > 0) {
    return timing_cell_port.get_trans_capacitance_map()[analysis_type][trans_type];
  }
  {
    static const char* pc = std::getenv("ISTA_DUMP_PIN_CAP");
    if (pc != nullptr) {
      if (FILE* f = std::fopen(pc, "a"); f != nullptr) {
        std::fprintf(f, "%s,%s,%s,%d,plain=%.12g,trans=%.12g\n", pin.get_instance_name().c_str(), pin.get_pin_name().c_str(),
                     instance.get_cell_name().c_str(), static_cast<int>(trans_type), timing_cell_port.get_capacitance(),
                     timing_cell_port.get_trans_capacitance_map().count(analysis_type) > 0
                             && timing_cell_port.get_trans_capacitance_map()[analysis_type].count(trans_type) > 0
                         ? timing_cell_port.get_trans_capacitance_map()[analysis_type][trans_type]
                         : -1.0);
        std::fclose(f);
      }
    }
  }
  return timing_cell_port.get_capacitance();
}

}  // namespace ista
