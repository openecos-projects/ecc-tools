// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of the Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "EMAnalyzer.hpp"

#include "DataManager.hpp"
#include "Logger.hpp"
#include "Monitor.hpp"
#include "PowerNode.hpp"

namespace iemir {

// public

void EMAnalyzer::initInst()
{
  if (_ea_instance == nullptr) {
    _ea_instance = new EMAnalyzer();
  }
}

EMAnalyzer& EMAnalyzer::getInst()
{
  if (_ea_instance == nullptr) {
    EMIRLOG.error(Loc::current(), "The instance not initialized!");
  }
  return *_ea_instance;
}

void EMAnalyzer::destroyInst()
{
  if (_ea_instance != nullptr) {
    delete _ea_instance;
    _ea_instance = nullptr;
  }
}

// function

void EMAnalyzer::analyze()
{
  Monitor monitor;
  EMIRLOG.info(Loc::current(), "Starting...");

  analyzePowerGraphList();

  EMIRLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

// private

EMAnalyzer* EMAnalyzer::_ea_instance = nullptr;

void EMAnalyzer::analyzePowerGraphList()
{
  for (std::pair<const std::string, PowerGraph>& power_graph_pair : EMIRDM.getDatabase().get_power_graph_map()) {
    analyzePowerGraph(power_graph_pair.second);
  }
}

void EMAnalyzer::analyzePowerGraph(PowerGraph& power_graph)
{
  EAModel ea_model = initEAModel();
  analyzePowerEdgeList(power_graph, ea_model);
}

EAModel EMAnalyzer::initEAModel()
{
  EAModel ea_model;
  return ea_model;
}

void EMAnalyzer::analyzePowerEdgeList(PowerGraph& power_graph, EAModel& ea_model)
{
  for (PowerEdge& power_edge : power_graph.get_edge_list()) {
    analyzePowerEdge(power_graph, power_edge, ea_model);
  }
}

void EMAnalyzer::analyzePowerEdge(PowerGraph& power_graph, PowerEdge& power_edge, EAModel& ea_model)
{
  if (power_edge.get_first_node_id() >= power_graph.get_node_list().size() || power_edge.get_second_node_id() >= power_graph.get_node_list().size()) {
    EMIRLOG.error(Loc::current(), "The power edge node is invalid!");
  }
  if (power_edge.get_resistance() <= EMIR_ERROR) {
    EMIRLOG.error(Loc::current(), "The power edge resistance is invalid!");
  }
  PowerNode& first_power_node = power_graph.get_node_list()[power_edge.get_first_node_id()];
  PowerNode& second_power_node = power_graph.get_node_list()[power_edge.get_second_node_id()];
  double current = std::abs(first_power_node.get_voltage() - second_power_node.get_voltage()) / power_edge.get_resistance();
  power_edge.set_current(current);
  power_edge.set_current_density(0.0);
  power_edge.set_is_violation(false);
  ea_model.set_power_edge_num(ea_model.get_power_edge_num() + 1);
  ea_model.set_total_current(ea_model.get_total_current() + current);
  ea_model.set_max_current(std::max(ea_model.get_max_current(), current));
}

}  // namespace iemir
