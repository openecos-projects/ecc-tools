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
#include <cmath>
#include <filesystem>
#include <fstream>

#include "DataManager.hpp"
#include "EMAnalyzer.hpp"
#include "EMIRReporter.hpp"
#include "IRAnalyzer.hpp"
#include "InstancePower.hpp"
#include "Logger.hpp"
#include "PowerEdge.hpp"
#include "PowerEdgeType.hpp"
#include "PowerGraph.hpp"
#include "PowerNetType.hpp"
#include "PowerNode.hpp"
#include "PowerNodeType.hpp"

namespace {

iemir::PowerGraph buildPowerGraph(const std::string& net_name, iemir::PowerNetType power_net_type)
{
  iemir::PowerGraph power_graph;
  power_graph.set_net_name(net_name);
  power_graph.set_net_type(power_net_type);

  iemir::PowerNode source_node;
  source_node.set_node_id(0);
  source_node.set_type(iemir::PowerNodeType::kSource);
  source_node.set_is_source(true);
  power_graph.get_node_list().push_back(source_node);
  power_graph.get_source_node_id_list().push_back(source_node.get_node_id());

  iemir::PowerNode instance_node;
  instance_node.set_node_id(1);
  instance_node.set_type(iemir::PowerNodeType::kInstancePin);
  instance_node.get_instance_id_set().insert(1);
  power_graph.get_node_list().push_back(instance_node);
  power_graph.get_instance_node_id_list_map()[1].push_back(instance_node.get_node_id());

  iemir::PowerEdge power_edge;
  power_edge.set_edge_id(0);
  power_edge.set_type(iemir::PowerEdgeType::kWire);
  power_edge.set_first_node_id(source_node.get_node_id());
  power_edge.set_second_node_id(instance_node.get_node_id());
  power_edge.set_resistance(1.0);
  power_graph.get_edge_list().push_back(power_edge);
  return power_graph;
}

bool checkPowerGraph(iemir::PowerGraph& power_graph, double expected_voltage)
{
  double voltage = power_graph.get_node_list()[1].get_voltage();
  double current = power_graph.get_edge_list()[0].get_current();
  return std::abs(voltage - expected_voltage) <= EMIR_ERROR && std::abs(current - 1.0) <= EMIR_ERROR;
}

bool checkReport(const std::string& report_file_path, const std::string& content)
{
  std::ifstream report_file(report_file_path);
  std::string report_content((std::istreambuf_iterator<char>(report_file)), std::istreambuf_iterator<char>());
  return report_content.find(content) != std::string::npos;
}

}  // namespace

int main()
{
  iemir::Logger::initInst();
  iemir::DataManager::initInst();

  iemir::InstancePower instance_power;
  instance_power.set_instance_id(1);
  instance_power.set_voltage(1.0);
  instance_power.set_internal_power(1.0);
  EMIRDM.getDatabase().get_instance_power_map()[instance_power.get_instance_id()] = instance_power;
  EMIRDM.getDatabase().get_power_graph_map()["VDD"] = buildPowerGraph("VDD", iemir::PowerNetType::kPower);
  EMIRDM.getDatabase().get_power_graph_map()["VSS"] = buildPowerGraph("VSS", iemir::PowerNetType::kGround);

  iemir::IRAnalyzer::initInst();
  EMIRIA.analyze();
  iemir::IRAnalyzer::destroyInst();

  iemir::EMAnalyzer::initInst();
  EMIREA.analyze();
  iemir::EMAnalyzer::destroyInst();

  bool is_pass
      = checkPowerGraph(EMIRDM.getDatabase().get_power_graph_map()["VDD"], 0.0) && checkPowerGraph(EMIRDM.getDatabase().get_power_graph_map()["VSS"], 1.0);
  std::filesystem::path report_directory_path = std::filesystem::temp_directory_path() / "iemir_ir_em_test";
  std::filesystem::remove_all(report_directory_path);
  std::filesystem::create_directories(report_directory_path);
  EMIRDM.getConfig().er_temp_directory_path = report_directory_path.string() + "/";
  iemir::EMIRReporter::initInst();
  EMIRER.report();
  iemir::EMIRReporter::destroyInst();
  is_pass = is_pass && checkReport((report_directory_path / "ir.rpt").string(), "########## IR report #################")
            && checkReport((report_directory_path / "em.rpt").string(), "########## EM analysis ###############");
  std::filesystem::remove_all(report_directory_path);

  iemir::DataManager::destroyInst();
  iemir::Logger::destroyInst();
  return is_pass ? 0 : 1;
}
