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
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "OutputLoadEstimator.hpp"

#include "DelayCache.hpp"

#include "DelayCache.hpp"
#include "Net.hpp"
#include "ParasiticGraph.hpp"
#include "RcTreeApprox.hpp"
#include "TimingConstraint.hpp"

#include "DataManager.hpp"
#include "Database.hpp"
#include "Instance.hpp"
#include "ParasiticNet.hpp"
#include "Pin.hpp"
#include "PWHeader.hpp"
#include "TimingCell.hpp"
#include "TimingCellPort.hpp"
#include "TimingConstraint.hpp"
#include "TimingLibrary.hpp"
#include "TimingPortConstraint.hpp"

namespace ipw {

double getPinCapacitance(std::string& pin_name, AnalysisType analysis_type, TransType trans_type)
{
  Database& database = PWDM.getDatabase();
  auto pin_iter = database.get_pin_map().find(pin_name);
  if (pin_iter == database.get_pin_map().end()) {
    return 0.0;
  }
  return getPinCapacitance(static_cast<uint32_t>(pin_iter->second.get_index()), analysis_type, trans_type);
}

double getPinCapacitance(uint32_t pin_index, AnalysisType analysis_type, TransType trans_type)
{
  Database& database = PWDM.getDatabase();
  const std::vector<Database::PinStaticContext>& pin_static_context_list = database.get_pin_static_context_list();
  if (pin_index >= pin_static_context_list.size()) {
    return 0.0;
  }
  const Database::PinStaticContext& pin_static_context = pin_static_context_list[pin_index];
  if (pin_static_context.pin == nullptr) {
    return 0.0;
  }
  if (pin_static_context.pin->get_is_port()) {
    std::map<std::string, TimingPortConstraint>& port_constraint_map
        = database.get_timing_constraint().get_port_constraint_map();
    auto port_constraint_iter = port_constraint_map.find(*pin_static_context.full_name);
    if (port_constraint_iter != port_constraint_map.end() && port_constraint_iter->second.get_has_load()) {
      return port_constraint_iter->second.get_load(analysis_type, trans_type);
    }
    return 0.0;
  }
  if (pin_static_context.cell_port == nullptr) {
    return 0.0;
  }
  TimingCellPort& timing_cell_port = *pin_static_context.cell_port;
  auto capacitance_iter = timing_cell_port.get_trans_capacitance_map().find(analysis_type);
  if (capacitance_iter != timing_cell_port.get_trans_capacitance_map().end()) {
    auto trans_iter = capacitance_iter->second.find(trans_type);
    if (trans_iter != capacitance_iter->second.end()) {
      return trans_iter->second;
    }
  }
  return timing_cell_port.get_capacitance();
}

double getParasiticNodeLoad(
    ParasiticNet& parasitic_net,
    uint32_t node_id,
    AnalysisType analysis_type,
    TransType trans_type
)
{
  const ParasiticGraph& graph = getParasiticGraph(parasitic_net);
  return parasiticNodeLoad(graph, getParasiticPinCapList(parasitic_net, analysis_type, trans_type), node_id);
}

double getArcOutputLoad(Arc& arc, AnalysisType analysis_type, TransType output_trans_type)
{
  Database& database = PWDM.getDatabase();
  std::string& sink_pin_name = arc.get_sink_pin();
  auto sink_pin_iter = database.get_pin_map().find(sink_pin_name);
  if (sink_pin_iter == database.get_pin_map().end()) {
    return 0.0;
  }
  Pin& sink_pin = sink_pin_iter->second;
  if (sink_pin.get_net_name().empty()) {
    return 0.0;
  }
  auto net_iter = database.get_net_map().find(sink_pin.get_net_name());
  if (net_iter == database.get_net_map().end()) {
    return 0.0;
  }
  Net& net = net_iter->second;
  return getNetOutputLoad(sink_pin.get_net_name(), sink_pin_name, net, analysis_type, output_trans_type);
}

double getOutputPinLoad(std::string& output_pin, AnalysisType analysis_type, TransType output_trans_type)
{
  Database& database = PWDM.getDatabase();
  auto pin_iter = database.get_pin_map().find(output_pin);
  if (pin_iter == database.get_pin_map().end()) {
    return 0.0;
  }
  Pin& pin = pin_iter->second;
  if (pin.get_net_name().empty()) {
    return 0.0;
  }
  auto net_iter = database.get_net_map().find(pin.get_net_name());
  if (net_iter == database.get_net_map().end()) {
    return 0.0;
  }
  Net& net = net_iter->second;
  return getNetOutputLoad(pin.get_net_name(), output_pin, net, analysis_type, output_trans_type);
}

double getNetOutputLoad(
    const std::string& net_name,
    const std::string& driver_pin,
    Net& net,
    AnalysisType analysis_type,
    TransType output_trans_type
)
{
  Database& database = PWDM.getDatabase();
  TimingConstraint& timing_constraint = database.get_timing_constraint();
  if (timing_constraint.has_net_load(net_name, analysis_type, output_trans_type)) {
    double output_load = timing_constraint.get_net_load(net_name, analysis_type, output_trans_type);
    for (std::string& load_pin_name : net.get_load_pin_list()) {
      if (load_pin_name != driver_pin) {
        output_load += getPinCapacitance(load_pin_name, analysis_type, output_trans_type);
      }
    }
    return output_load;
  }

  double output_load = 0.0;
  auto parasitic_net_iter = database.get_parasitic_net_map().find(net_name);
  if (parasitic_net_iter != database.get_parasitic_net_map().end()) {
    ParasiticNet& parasitic_net = parasitic_net_iter->second;
    output_load = getParasiticNetOutputLoad(driver_pin, parasitic_net, analysis_type, output_trans_type);
  } else {
    for (std::string& load_pin_name : net.get_load_pin_list()) {
      if (load_pin_name != driver_pin) {
        output_load += getPinCapacitance(load_pin_name, analysis_type, output_trans_type);
      }
    }
  }
  return output_load;
}

double getParasiticNetOutputLoad(
    const std::string& driver_pin,
    ParasiticNet& parasitic_net,
    AnalysisType analysis_type,
    TransType trans_type
)
{
  const ParasiticGraph& graph = getParasiticGraph(parasitic_net);
  const uint32_t source_node_id = getParasiticNodeId(graph, driver_pin);
  if (source_node_id == kInvalidParasiticNode) {
    return getParasiticTotalLoad(graph, parasitic_net, analysis_type, trans_type);
  }

  ParasiticCornerState& corner_state
      = DelayCache::threadCache().parasiticCornerState(parasitic_net.get_net_name(), analysis_type, trans_type);
  buildParasiticDelayMap(graph, corner_state, source_node_id, analysis_type, trans_type);
  return corner_state.node_load[source_node_id];
}

double getParasiticTotalLoad(ParasiticNet& parasitic_net, AnalysisType analysis_type, TransType trans_type)
{
  return getParasiticTotalLoad(getParasiticGraph(parasitic_net), parasitic_net, analysis_type, trans_type);
}

double getParasiticTotalLoad(
    const ParasiticGraph& graph,
    ParasiticNet& parasitic_net,
    AnalysisType analysis_type,
    TransType trans_type
)
{
  ParasiticCornerState& corner_state
      = DelayCache::threadCache().parasiticCornerState(parasitic_net.get_net_name(), analysis_type, trans_type);
  const std::vector<double>& pin_cap_list = getParasiticPinCapList(graph, corner_state, analysis_type, trans_type);
  double total_load = 0.0;
  // id 序 == node_capacitance_map 的迭代序，求和顺序与改造前一致
  for (uint32_t node_id = 0; node_id < graph.capacitance_node_num; node_id++) {
    total_load += parasiticNodeLoad(graph, pin_cap_list, node_id);
  }
  return total_load;
}

}  // namespace ipw
