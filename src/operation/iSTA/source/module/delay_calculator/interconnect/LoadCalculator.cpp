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
#include "LoadCalculator.hpp"

#include <algorithm>
#include <string>

namespace ista {

double LoadCalculator::arcLoad(Arc& arc, AnalysisType analysis_type, TransType output_trans_type)
{
  Database& database = _database;
  std::string& sink_pin_name = arc.get_sink_pin();
  if (database.network().pins().count(sink_pin_name) == 0) {
    return 0.0;
  }
  Pin& sink_pin = database.network().pins()[sink_pin_name];
  if (sink_pin.get_net_name().empty() || database.network().nets().count(sink_pin.get_net_name()) == 0) {
    return 0.0;
  }
  Net& net = database.network().nets()[sink_pin.get_net_name()];
  return netLoad(net, sink_pin_name, analysis_type, output_trans_type);
}

double LoadCalculator::pinLoad(std::string& output_pin, AnalysisType analysis_type, TransType output_trans_type)
{
  Database& database = _database;
  if (database.network().pins().count(output_pin) == 0) {
    return 0.0;
  }
  Pin& pin = database.network().pins()[output_pin];
  if (pin.get_net_name().empty() || database.network().nets().count(pin.get_net_name()) == 0) {
    return 0.0;
  }
  Net& net = database.network().nets()[pin.get_net_name()];
  return netLoad(net, output_pin, analysis_type, output_trans_type);
}

double LoadCalculator::netLoad(Net& net, const std::string& driver_pin, AnalysisType analysis_type, TransType output_trans_type)
{
  Database& database = _database;
  auto& net_load_map = database.sdc().get_net_load_map();
  const auto load_constraint = net_load_map.find(net.get_net_name());
  if (load_constraint != net_load_map.end() && load_constraint->second.has(analysis_type, output_trans_type)) {
    return load_constraint->second.get(analysis_type, output_trans_type) + netPinLoad(net, driver_pin, analysis_type, output_trans_type);
  }

  if (database.parasitics().nets().count(net.get_net_name()) > 0) {
    ParasiticNet& parasitic_net = database.parasitics().nets()[net.get_net_name()];
    return parasiticLoad(driver_pin, parasitic_net, analysis_type, output_trans_type);
  }
  return netPinLoad(net, driver_pin, analysis_type, output_trans_type);
}

double LoadCalculator::netPinLoad(Net& net, const std::string& driver_pin, AnalysisType analysis_type, TransType trans_type)
{
  double output_load = _pin_capacitance.driverCapacitance(driver_pin, analysis_type, trans_type);
  const auto& loads = net.get_load_pin_list();
  for (auto& load_pin : net.get_load_pin_list()) {
    if (load_pin != driver_pin)
      output_load += _pin_capacitance.pinCapacitance(load_pin, analysis_type, trans_type);
  }
  // Input ports are driver-only after graph construction, but every explicit
  // port load remains attached to the net. Inout pins already occur in loads.
  for (const auto& other_driver : net.get_driver_pin_list()) {
    if (other_driver != driver_pin && std::find(loads.begin(), loads.end(), other_driver) == loads.end())
      output_load += _pin_capacitance.driverCapacitance(other_driver, analysis_type, trans_type);
  }
  return output_load;
}

double LoadCalculator::parasiticLoad(const std::string& driver_pin, ParasiticNet& parasitic_net, AnalysisType analysis_type, TransType trans_type)
{
  std::string source_node_name = _rc.nodeName(parasitic_net, driver_pin);
  if (source_node_name.empty()) {
    return _rc.totalLoad(parasitic_net, analysis_type, trans_type) + _pin_capacitance.driverCapacitance(driver_pin, analysis_type, trans_type);
  }

  _rc.analyze(parasitic_net, source_node_name, analysis_type, trans_type);
  return _rc.load(parasitic_net.get_net_name(), analysis_type, trans_type, source_node_name);
}

}  // namespace ista
