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
#include "EAModel.hpp"
#include "EMTech.hpp"
#include "PowerEdge.hpp"
#include "PowerGraph.hpp"

namespace iemir {

#define EMIREA (iemir::EMAnalyzer::getInst())

class EMAnalyzer
{
 public:
  static void initInst();
  static EMAnalyzer& getInst();
  static void destroyInst();
  // function
  void analyze();

 private:
  // self
  static EMAnalyzer* _ea_instance;

  EMAnalyzer() = default;
  EMAnalyzer(const EMAnalyzer& other) = delete;
  EMAnalyzer(EMAnalyzer&& other) = delete;
  ~EMAnalyzer() = default;
  EMAnalyzer& operator=(const EMAnalyzer& other) = delete;
  EMAnalyzer& operator=(EMAnalyzer&& other) = delete;
  template <typename Rule> struct ResolvedRule {
    Rule* rule = nullptr;
    EMRuleMatch match = EMRuleMatch::kNone;
  };
  std::unordered_map<std::string, ResolvedRule<EMMetalRule>> _metal_rules;
  std::unordered_map<std::string, ResolvedRule<EMViaRule>> _via_rules;
  std::unordered_map<std::string, ResolvedRule<EMViaRule>> _via_cut_rules;
  // Rebuilt on each analyze(): no pointers survive a rule reload.
  void prepareRules();
  // function
  void analyzePowerGraphList();
  void analyzePowerGraph(PowerGraph& power_graph);
  EAModel initEAModel();
  void analyzePowerEdgeList(PowerGraph& power_graph, EAModel& ea_model);
  void analyzePowerEdge(PowerGraph& power_graph, PowerEdge& power_edge, EAModel& ea_model);
  double getDBUToMicronRatio();
  double getMicronValue(int32_t dbu_value);
  EMMetalRule* getMetalRule(const std::string& layer_name);
  EMViaRule* getViaRule(const std::string& via_name, bool allow_name_fallback = true);
  std::string getNormalizedRuleName(const std::string& rule_name);
  double calcWireEMLimit(PowerEdge& power_edge, EMMetalRule& metal_rule);
  double calcViaEMLimit(PowerEdge& power_edge, EMViaRule& via_rule);
};

}  // namespace iemir
