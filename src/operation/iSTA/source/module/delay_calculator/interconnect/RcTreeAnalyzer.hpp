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
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "Database.hpp"
#include "PinCapacitanceCalculator.hpp"

namespace ista {
// Owns rooted RC loads, Elmore delays and impulse moments. Re-rooting invalidates root-dependent values.
class RcTreeAnalyzer
{
 public:
  RcTreeAnalyzer(Database& database, PinCapacitanceCalculator& pin_capacitance) : _database(database), _pin_capacitance(pin_capacitance) {}
  RcTreeAnalyzer(const RcTreeAnalyzer&) = delete;
  RcTreeAnalyzer& operator=(const RcTreeAnalyzer&) = delete;
  void clear()
  {
    _parasitic_resistor_map_cache.clear();
    _trees.clear();
    _timing.clear();
  }
  double load(const std::string& net, AnalysisType analysis, TransType trans, const std::string& node) const;
  std::optional<double> delay(const std::string& net, AnalysisType analysis, TransType trans, const std::string& node) const;
  std::optional<double> impulse(const std::string& net, AnalysisType analysis, TransType trans, const std::string& node) const;
  bool hasDisabledDriver(const std::string& net, const std::string& source) const;
  double balancedDelay(ParasiticNet& net, const std::string& source, std::string& sink, AnalysisType analysis, TransType edge);
  double totalResistance(ParasiticNet& parasitic_net);
  double nodeCapacitance(ParasiticNet& parasitic_net, std::string& pin_name);
  void analyze(ParasiticNet& parasitic_net, std::string& source_node_name, AnalysisType analysis_type, TransType trans_type);
  double totalLoad(ParasiticNet& parasitic_net, AnalysisType analysis_type, TransType trans_type);
  std::string nodeName(ParasiticNet& parasitic_net, const std::string& pin_name);
  std::string pinName(std::string& node_name);
  void buildTree(ParasiticNet& parasitic_net, std::string& source_node_name, AnalysisType analysis_type, TransType trans_type,
                 std::vector<std::string>& node_name_list, std::vector<int32_t>& parent_idx_list, std::vector<double>& resistance_list,
                 std::vector<double>& capacitance_list);

 private:
  double nodeLoad(ParasiticNet& parasitic_net, std::string& node_name, AnalysisType analysis_type, TransType trans_type);
  // Node order and ordered children preserve the resistor-list DFS and the
  // floating-point summation order. Topology is independent of slew and corner.
  struct RcTree
  {
    std::string root;
    std::vector<std::string> nodes;
    std::vector<int32_t> parents;
    std::vector<double> resistances;  // ohms, before numerical unit conversion
    std::vector<std::vector<std::size_t>> children;
  };
  struct RcTiming
  {
    std::string root;
    std::map<std::string, double> loads;
    std::map<std::string, double> delays;
    std::map<std::string, double> impulses;
  };
  RcTree& rootedTree(ParasiticNet& net, const std::string& root);
  const RcTiming* timing(const std::string& net, AnalysisType analysis, TransType trans) const;
  void buildParasiticResistorMap(ParasiticNet& parasitic_net, std::map<std::string, std::vector<std::pair<std::string, double>>>& resistor_map);
  Database& _database;
  PinCapacitanceCalculator& _pin_capacitance;
  std::map<std::string, std::map<std::string, std::vector<std::pair<std::string, double>>>> _parasitic_resistor_map_cache;
  std::map<std::string, RcTree> _trees;
  std::map<std::string, std::map<AnalysisType, std::map<TransType, RcTiming>>> _timing;
};
}  // namespace ista
