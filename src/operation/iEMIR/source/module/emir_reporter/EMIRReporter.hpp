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
#pragma once

#include "Database.hpp"
#include "ERModel.hpp"
#include "PowerGraph.hpp"
#include "PowerVia.hpp"

namespace iemir {

#define EMIRER (iemir::EMIRReporter::getInst())

class EMIRReporter
{
 public:
  static void initInst();
  static EMIRReporter& getInst();
  static void destroyInst();
  // function
  void report();

 private:
  // self
  static EMIRReporter* _er_instance;

  EMIRReporter() = default;
  EMIRReporter(const EMIRReporter& other) = delete;
  EMIRReporter(EMIRReporter&& other) = delete;
  ~EMIRReporter() = default;
  EMIRReporter& operator=(const EMIRReporter& other) = delete;
  EMIRReporter& operator=(EMIRReporter&& other) = delete;
  // function
  ERModel initERModel();
  void buildIRReportFilePath(ERModel& er_model);
  void buildEMReportFilePath(ERModel& er_model);
  void outputIRReport(ERModel& er_model);
  void outputIRReportFile(const std::string& report_file_path);
  void outputIRReportHeader(std::ofstream* ir_report_file);
  void outputIRReportRows(std::ofstream* ir_report_file);
  double getTotalPower(PowerGraph& power_graph);
  double getSupplyVoltage(PowerGraph& power_graph);
  int32_t getInstancePowerGraphNum(uint64_t instance_id, PowerNetType power_net_type);
  double getWorstVoltage(PowerGraph& power_graph);
  double getAverageVoltage(PowerGraph& power_graph);
  double getWorstIRDrop(PowerGraph& power_graph);
  double getAverageIRDrop(PowerGraph& power_graph);
  double getPercentageDrop(PowerGraph& power_graph);
  void outputEMReport(ERModel& er_model);
  void outputEMReportFile(const std::string& report_file_path, bool worst_only);
  void outputEMReportHeader(std::ofstream* em_report_file, bool worst_only);
  void outputEMReportRows(std::ofstream* em_report_file, bool worst_only);
  void outputResNetworkReport();
  void outputResNetworkReportFile(const std::string& report_file_path);
  void outputResNetworkReportHeader(std::ofstream* res_network_report_file);
  void outputResNetworkReportRows(std::ofstream* res_network_report_file);
  double getMaxCurrent(PowerGraph& power_graph);
  double getAverageCurrent(PowerGraph& power_graph);
  std::string buildDesignReportFilePath(const std::string& suffix);
  PowerVia* getPowerVia(PowerGraph& power_graph, PowerEdge& power_edge);
  std::pair<double, double> getViaLocation(PowerGraph& power_graph, PowerEdge& power_edge);
  std::string getLayerName(PowerGraph& power_graph, int32_t layer_idx);
  std::string getViaName(PowerGraph& power_graph, PowerEdge& power_edge);
  std::string getViaCutBox(PowerGraph& power_graph, PowerEdge& power_edge);
  std::string getViaDirection(PowerGraph& power_graph, PowerEdge& power_edge);
  std::string getWireDirection(PowerGraph& power_graph, PowerEdge& power_edge);
  double getDBUToMicronRatio();
  double getMicronValue(int32_t dbu_value);
  double getIRSeverity(PowerGraph& power_graph, PowerNode& power_node);
  double getEMRatioPercent(PowerEdge& power_edge);
};

}  // namespace iemir
