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
#include "EMIRReporter.hpp"

#include "DataManager.hpp"
#include "InstancePower.hpp"
#include "Logger.hpp"
#include "Monitor.hpp"
#include "PowerEdge.hpp"
#include "PowerNetType.hpp"
#include "PowerNode.hpp"
#include "Utility.hpp"

namespace iemir {

// public

void EMIRReporter::initInst()
{
  if (_er_instance == nullptr) {
    _er_instance = new EMIRReporter();
  }
}

EMIRReporter& EMIRReporter::getInst()
{
  if (_er_instance == nullptr) {
    EMIRLOG.error(Loc::current(), "The instance not initialized!");
  }
  return *_er_instance;
}

void EMIRReporter::destroyInst()
{
  if (_er_instance != nullptr) {
    delete _er_instance;
    _er_instance = nullptr;
  }
}

// function

void EMIRReporter::report()
{
  Monitor monitor;
  EMIRLOG.info(Loc::current(), "Starting...");

  ERModel er_model = initERModel();
  outputIRReport(er_model);
  outputEMReport(er_model);

  EMIRLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

// private

EMIRReporter* EMIRReporter::_er_instance = nullptr;

ERModel EMIRReporter::initERModel()
{
  ERModel er_model;
  buildIRReportFilePath(er_model);
  buildEMReportFilePath(er_model);
  return er_model;
}

void EMIRReporter::buildIRReportFilePath(ERModel& er_model)
{
  er_model.set_ir_report_file_path(EMIRUTIL.getString(EMIRDM.getConfig().er_temp_directory_path, "ir.rpt"));
}

void EMIRReporter::buildEMReportFilePath(ERModel& er_model)
{
  er_model.set_em_report_file_path(EMIRUTIL.getString(EMIRDM.getConfig().er_temp_directory_path, "em.rpt"));
}

void EMIRReporter::outputIRReport(ERModel& er_model)
{
  std::ofstream* ir_report_file = EMIRUTIL.getOutputFileStream(er_model.get_ir_report_file_path());
  outputIRDesignInfo(ir_report_file);
  outputIRPowerGraphList(ir_report_file);
  EMIRUTIL.closeFileStream(ir_report_file);
}

void EMIRReporter::outputIRDesignInfo(std::ofstream* ir_report_file)
{
  (*ir_report_file) << "Design : " << EMIRDM.getDatabase().get_design_name() << "\n\n";
}

void EMIRReporter::outputIRPowerGraphList(std::ofstream* ir_report_file)
{
  for (std::pair<const std::string, PowerGraph>& power_graph_pair : EMIRDM.getDatabase().get_power_graph_map()) {
    outputIRPowerGraph(ir_report_file, power_graph_pair.second);
  }
}

void EMIRReporter::outputIRPowerGraph(std::ofstream* ir_report_file, PowerGraph& power_graph)
{
  (*ir_report_file) << "########## IR report #################\n";
  (*ir_report_file) << "Net              : " << power_graph.get_net_name() << "\n";
  (*ir_report_file) << "Total power      : " << std::scientific << std::setprecision(2) << getTotalPower(power_graph) << " W\n";
  (*ir_report_file) << "Supply voltage   : " << std::scientific << std::setprecision(2) << getSupplyVoltage(power_graph) << " V\n";
  (*ir_report_file) << "Worstcase voltage: " << std::scientific << std::setprecision(2) << getWorstVoltage(power_graph) << " V\n";
  (*ir_report_file) << "Average voltage  : " << std::scientific << std::setprecision(2) << getAverageVoltage(power_graph) << " V\n";
  (*ir_report_file) << "Average IR drop  : " << std::scientific << std::setprecision(2) << getAverageIRDrop(power_graph) << " V\n";
  (*ir_report_file) << "Worstcase IR drop: " << std::scientific << std::setprecision(2) << getWorstIRDrop(power_graph) << " V\n";
  (*ir_report_file) << "Percentage drop  : " << std::fixed << std::setprecision(2) << getPercentageDrop(power_graph) << " %\n";
  (*ir_report_file) << "######################################\n\n";
}

double EMIRReporter::getTotalPower(PowerGraph& power_graph)
{
  double total_power = 0.0;
  Database& database = EMIRDM.getDatabase();
  for (std::pair<const uint64_t, std::vector<std::size_t>>& instance_node_pair : power_graph.get_instance_node_id_list_map()) {
    if (database.get_instance_power_map().count(instance_node_pair.first) == 0) {
      continue;
    }
    int32_t power_graph_num = getInstancePowerGraphNum(instance_node_pair.first, power_graph.get_net_type());
    if (power_graph_num <= 0) {
      EMIRLOG.error(Loc::current(), "The instance power graph mapping is invalid!");
    }
    total_power += database.get_instance_power_map()[instance_node_pair.first].get_total_power() / power_graph_num;
  }
  return total_power;
}

double EMIRReporter::getSupplyVoltage(PowerGraph& power_graph)
{
  if (power_graph.get_net_type() == PowerNetType::kPower) {
    return power_graph.get_source_voltage();
  }
  Database& database = EMIRDM.getDatabase();
  double supply_voltage = 0.0;
  for (std::pair<const uint64_t, std::vector<std::size_t>>& instance_node_pair : power_graph.get_instance_node_id_list_map()) {
    if (database.get_instance_power_map().count(instance_node_pair.first) == 0) {
      continue;
    }
    double instance_voltage = database.get_instance_power_map()[instance_node_pair.first].get_voltage();
    if (instance_voltage <= EMIR_ERROR) {
      continue;
    }
    if (supply_voltage <= EMIR_ERROR) {
      supply_voltage = instance_voltage;
    } else if (std::abs(supply_voltage - instance_voltage) > EMIR_ERROR) {
      EMIRLOG.error(Loc::current(), "The power graph contains inconsistent supply voltages!");
    }
  }
  return supply_voltage;
}

int32_t EMIRReporter::getInstancePowerGraphNum(uint64_t instance_id, PowerNetType power_net_type)
{
  int32_t power_graph_num = 0;
  for (std::pair<const std::string, PowerGraph>& power_graph_pair : EMIRDM.getDatabase().get_power_graph_map()) {
    PowerGraph& power_graph = power_graph_pair.second;
    if (power_graph.get_net_type() == power_net_type && power_graph.get_instance_node_id_list_map().count(instance_id) != 0) {
      power_graph_num++;
    }
  }
  return power_graph_num;
}

double EMIRReporter::getWorstVoltage(PowerGraph& power_graph)
{
  if (power_graph.get_node_list().empty()) {
    return 0.0;
  }
  double worst_voltage = power_graph.get_source_voltage();
  for (PowerNode& power_node : power_graph.get_node_list()) {
    if (power_graph.get_net_type() == PowerNetType::kGround) {
      worst_voltage = std::max(worst_voltage, power_node.get_voltage());
    } else {
      worst_voltage = std::min(worst_voltage, power_node.get_voltage());
    }
  }
  return worst_voltage;
}

double EMIRReporter::getAverageVoltage(PowerGraph& power_graph)
{
  if (power_graph.get_node_list().empty()) {
    return 0.0;
  }
  double total_voltage = 0.0;
  for (PowerNode& power_node : power_graph.get_node_list()) {
    total_voltage += power_node.get_voltage();
  }
  return total_voltage / power_graph.get_node_list().size();
}

double EMIRReporter::getWorstIRDrop(PowerGraph& power_graph)
{
  if (power_graph.get_net_type() == PowerNetType::kGround) {
    return getWorstVoltage(power_graph);
  }
  return power_graph.get_source_voltage() - getWorstVoltage(power_graph);
}

double EMIRReporter::getAverageIRDrop(PowerGraph& power_graph)
{
  if (power_graph.get_net_type() == PowerNetType::kGround) {
    return getAverageVoltage(power_graph);
  }
  return power_graph.get_source_voltage() - getAverageVoltage(power_graph);
}

double EMIRReporter::getPercentageDrop(PowerGraph& power_graph)
{
  double supply_voltage = getSupplyVoltage(power_graph);
  if (supply_voltage <= EMIR_ERROR) {
    return 0.0;
  }
  return 100.0 * getWorstIRDrop(power_graph) / supply_voltage;
}

void EMIRReporter::outputEMReport(ERModel& er_model)
{
  std::ofstream* em_report_file = EMIRUTIL.getOutputFileStream(er_model.get_em_report_file_path());
  outputEMDesignInfo(em_report_file);
  outputEMPowerGraphList(em_report_file);
  EMIRUTIL.closeFileStream(em_report_file);
}

void EMIRReporter::outputEMDesignInfo(std::ofstream* em_report_file)
{
  (*em_report_file) << "Design : " << EMIRDM.getDatabase().get_design_name() << "\n\n";
}

void EMIRReporter::outputEMPowerGraphList(std::ofstream* em_report_file)
{
  for (std::pair<const std::string, PowerGraph>& power_graph_pair : EMIRDM.getDatabase().get_power_graph_map()) {
    outputEMPowerGraph(em_report_file, power_graph_pair.second);
  }
}

void EMIRReporter::outputEMPowerGraph(std::ofstream* em_report_file, PowerGraph& power_graph)
{
  (*em_report_file) << "########## EM analysis ###############\n";
  (*em_report_file) << "Net                : " << power_graph.get_net_name() << "\n";
  (*em_report_file) << "Maximum current    : " << std::scientific << std::setprecision(2) << getMaxCurrent(power_graph) << " A\n";
  (*em_report_file) << "Average current    : " << std::scientific << std::setprecision(2) << getAverageCurrent(power_graph) << " A\n";
  (*em_report_file) << "Number of resistors: " << power_graph.get_edge_list().size() << "\n";
  (*em_report_file) << "######################################\n\n";
}

double EMIRReporter::getMaxCurrent(PowerGraph& power_graph)
{
  double max_current = 0.0;
  for (PowerEdge& power_edge : power_graph.get_edge_list()) {
    max_current = std::max(max_current, power_edge.get_current());
  }
  return max_current;
}

double EMIRReporter::getAverageCurrent(PowerGraph& power_graph)
{
  if (power_graph.get_edge_list().empty()) {
    return 0.0;
  }
  double total_current = 0.0;
  for (PowerEdge& power_edge : power_graph.get_edge_list()) {
    total_current += power_edge.get_current();
  }
  return total_current / power_graph.get_edge_list().size();
}

}  // namespace iemir
