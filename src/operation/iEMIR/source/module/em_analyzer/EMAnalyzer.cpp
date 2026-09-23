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
#include "PowerEdgeType.hpp"
#include "PowerNode.hpp"

namespace {

std::string normalizeRuleName(const std::string& name)
{
  std::string result;
  for (char ch : name) {
    if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_') {
      result.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
    }
  }
  return result;
}

std::vector<std::string> getRuleAliases(const std::string& name)
{
  std::vector<std::string> aliases;
  std::string normalized_name = normalizeRuleName(name);
  if (normalized_name.empty()) {
    return aliases;
  }
  aliases.push_back(normalized_name);
  std::smatch match;
  if (std::regex_match(normalized_name, match, std::regex("^M([0-9]+)$"))) {
    aliases.push_back("MET" + match.str(1));
  } else if (std::regex_match(normalized_name, match, std::regex("^MET([0-9]+)$"))) {
    aliases.push_back("M" + match.str(1));
  } else if (std::regex_match(normalized_name, match, std::regex("^V([0-9]+)$"))) {
    aliases.push_back("VIA" + match.str(1));
  } else if (std::regex_match(normalized_name, match, std::regex("^VIA([0-9]+)$"))) {
    aliases.push_back("V" + match.str(1));
  } else if (normalized_name == "TM") {
    aliases.push_back("T4M2");
  } else if (normalized_name == "T4M2") {
    aliases.push_back("TM");
  } else if (normalized_name == "TV") {
    aliases.push_back("T4V2");
  } else if (normalized_name == "T4V2") {
    aliases.push_back("TV");
  }
  return aliases;
}

}  // namespace

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

  double em_limit = 0.0;
  std::string em_rule_name;
  if (!power_edge.get_is_em_checkable()) {
    em_rule_name = "NON_PHYSICAL_CONNECTOR";
  } else if (power_edge.get_type() == PowerEdgeType::kWire) {
    EMMetalRule* metal_rule = getMetalRule(power_edge.get_layer_name());
    if (metal_rule != nullptr) {
      em_limit = calcWireEMLimit(power_edge, *metal_rule);
      em_rule_name = metal_rule->get_name();
    }
  } else if (power_edge.get_type() == PowerEdgeType::kVia) {
    EMViaRule* via_rule = getViaRule(power_edge.get_via_name());
    if (via_rule != nullptr) {
      em_limit = calcViaEMLimit(power_edge, *via_rule);
      em_rule_name = via_rule->get_name();
    }
  }

  double em_ratio_percent = em_limit > EMIR_ERROR ? 100.0 * current / em_limit : 0.0;
  power_edge.set_em_limit(em_limit);
  power_edge.set_em_ratio_percent(em_ratio_percent);
  power_edge.set_em_rule_name(em_rule_name);
  power_edge.set_has_em_limit(em_limit > EMIR_ERROR);
  power_edge.set_current_density(em_ratio_percent);
  power_edge.set_is_violation(em_ratio_percent >= EMIRDM.getConfig().em_violation_threshold_percent);

  ea_model.set_power_edge_num(ea_model.get_power_edge_num() + 1);
  ea_model.set_total_current(ea_model.get_total_current() + current);
  ea_model.set_max_current(std::max(ea_model.get_max_current(), current));
}

double EMAnalyzer::getDBUToMicronRatio()
{
  int32_t micron_dbu = EMIRDM.getDatabase().get_micron_dbu();
  if (micron_dbu <= 0) {
    return 1.0;
  }
  return 1.0 / static_cast<double>(micron_dbu);
}

double EMAnalyzer::getMicronValue(int32_t dbu_value)
{
  return static_cast<double>(dbu_value) * getDBUToMicronRatio();
}

EMMetalRule* EMAnalyzer::getMetalRule(const std::string& layer_name)
{
  EMTech& em_tech = EMIRDM.getDatabase().get_em_tech();
  for (const std::string& alias : getRuleAliases(layer_name)) {
    auto iter = em_tech.get_metal_rule_map().find(alias);
    if (iter != em_tech.get_metal_rule_map().end()) {
      return &iter->second;
    }
  }
  return nullptr;
}

EMViaRule* EMAnalyzer::getViaRule(const std::string& via_name)
{
  EMTech& em_tech = EMIRDM.getDatabase().get_em_tech();
  std::string normalized_via_name = normalizeRuleName(via_name);
  for (const std::string& alias : getRuleAliases(via_name)) {
    auto iter = em_tech.get_via_rule_map().find(alias);
    if (iter != em_tech.get_via_rule_map().end()) {
      return &iter->second;
    }
  }
  std::size_t separator_pos = normalized_via_name.find('_');
  if (separator_pos != std::string::npos) {
    std::string base_via_name = normalized_via_name.substr(0, separator_pos);
    for (const std::string& alias : getRuleAliases(base_via_name)) {
      auto iter = em_tech.get_via_rule_map().find(alias);
      if (iter != em_tech.get_via_rule_map().end()) {
        return &iter->second;
      }
    }
  }
  EMViaRule* best_rule = nullptr;
  std::size_t best_length = 0;
  for (std::pair<const std::string, EMViaRule>& rule_pair : em_tech.get_via_rule_map()) {
    const std::string& rule_name = rule_pair.first;
    if (normalized_via_name.rfind(rule_name + "_", 0) == 0 && rule_name.size() > best_length) {
      best_rule = &rule_pair.second;
      best_length = rule_name.size();
    }
  }
  return best_rule;
}

std::string EMAnalyzer::getNormalizedRuleName(const std::string& rule_name)
{
  return normalizeRuleName(rule_name);
}

double EMAnalyzer::calcWireEMLimit(PowerEdge& power_edge, EMMetalRule& metal_rule)
{
  double width_um = getMicronValue(power_edge.get_width());
  double effective_width_um = width_um * EMIRDM.getDatabase().get_em_tech().get_half_node_scale_factor() - metal_rule.get_em_adjust_um();
  if (effective_width_um <= EMIR_ERROR || metal_rule.get_em_limit_ma_per_um() <= 0.0) {
    return 0.0;
  }
  return metal_rule.get_em_limit_ma_per_um() * effective_width_um / 1000.0;
}

double EMAnalyzer::calcViaEMLimit(PowerEdge& power_edge, EMViaRule& via_rule)
{
  if (via_rule.get_em_limit_ma() <= 0.0) {
    return 0.0;
  }
  // A scalar RedHawk VIA EM value is a per-cut limit. Geometry area is not an
  // interchangeable multiplier unless an area-dependent rule table is present.
  double scale = power_edge.get_cut_num() > 0 ? static_cast<double>(power_edge.get_cut_num()) : 1.0;
  return via_rule.get_em_limit_ma() * scale / 1000.0;
}

}  // namespace iemir
