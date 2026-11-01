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

  const auto start = std::chrono::steady_clock::now();
  prepareRules();
  const auto prepared = std::chrono::steady_clock::now();
  analyzePowerGraphList();
  const auto completed = std::chrono::steady_clock::now();
  EMIRLOG.info(Loc::current(), "EM timing: rule_resolution_seconds=",
      std::chrono::duration<double>(prepared - start).count(), ", edge_evaluation_seconds=",
      std::chrono::duration<double>(completed - prepared).count());
  // Bindings are run-local; do not retain pointers into a replaceable technology database.
  _metal_rules.clear();
  _via_rules.clear();
  _via_cut_rules.clear();

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

void EMAnalyzer::prepareRules()
{
  _metal_rules.clear();
  _via_rules.clear();
  _via_cut_rules.clear();
  auto classify = [](const std::string& name, const std::string& resolved, bool via) {
    if (name == resolved) return EMRuleMatch::kExact;
    if (normalizeRuleName(name) == resolved) return EMRuleMatch::kNormalized;
    const auto aliases = getRuleAliases(name);
    if (std::find(aliases.begin(), aliases.end(), resolved) != aliases.end()) return EMRuleMatch::kAlias;
    return via ? EMRuleMatch::kFallback : EMRuleMatch::kNone;
  };
  for (auto& [net, graph] : EMIRDM.getDatabase().get_power_graph_map()) {
    for (auto& edge : graph.get_edge_list()) {
      if (!edge.get_is_em_checkable()) continue;
      if (edge.get_type() == PowerEdgeType::kWire && !_metal_rules.count(edge.get_layer_name())) {
        auto* rule = getMetalRule(edge.get_layer_name());
        if (rule && !rule->get_has_em_rule()) rule = nullptr;
        _metal_rules.emplace(edge.get_layer_name(), ResolvedRule<EMMetalRule>{rule,
            rule ? classify(edge.get_layer_name(), rule->get_name(), false) : EMRuleMatch::kNone});
      } else if (edge.get_type() == PowerEdgeType::kVia) {
        const bool has_cut_layer = !edge.get_cut_layer_name().empty();
        const auto& name = has_cut_layer ? edge.get_cut_layer_name() : edge.get_via_name();
        auto& cache = has_cut_layer ? _via_cut_rules : _via_rules;
        if (cache.count(name)) continue;
        auto* rule = getViaRule(name, !has_cut_layer);
        if (rule && !rule->get_has_em_rule()) rule = nullptr;
        cache.emplace(name, ResolvedRule<EMViaRule>{rule,
            rule ? classify(name, rule->get_name(), true) : EMRuleMatch::kNone});
      }
    }
  }
  EMIRLOG.info(Loc::current(), "Resolved EM rule keys: wires=", _metal_rules.size(), ", vias=", _via_rules.size() + _via_cut_rules.size());
}

void EMAnalyzer::analyzePowerEdge(PowerGraph& power_graph, PowerEdge& power_edge, EAModel& ea_model)
{
  auto& evaluation = power_edge.get_em_evaluation();
  evaluation = EMEvaluation{};
  const double unavailable = std::numeric_limits<double>::quiet_NaN();
  power_edge.set_current(unavailable);
  power_edge.set_em_limit(unavailable);
  power_edge.set_em_ratio_percent(unavailable);
  power_edge.set_current_density(unavailable);  // Legacy percentage field, not a physical current density.
  power_edge.set_has_em_limit(false);
  power_edge.set_is_violation(false);
  power_edge.set_em_rule_name("");
  if (!power_edge.get_is_em_checkable()) {
    evaluation.status = EMEvalStatus::kExcluded;
    if (power_edge.get_first_node_id() < power_graph.get_node_list().size()
        && power_edge.get_second_node_id() < power_graph.get_node_list().size()
        && std::isfinite(power_edge.get_resistance()) && power_edge.get_resistance() > 0.0) {
      evaluation.voltage_delta_v = power_graph.get_node_list()[power_edge.get_first_node_id()].get_voltage()
          - power_graph.get_node_list()[power_edge.get_second_node_id()].get_voltage();
      evaluation.signed_current_a = evaluation.voltage_delta_v / power_edge.get_resistance();
      double current = std::abs(evaluation.signed_current_a);
      if (std::isfinite(current)) power_edge.set_current(current);
    }
    return;
  }
  if (power_edge.get_first_node_id() >= power_graph.get_node_list().size() || power_edge.get_second_node_id() >= power_graph.get_node_list().size()) {
    evaluation.status = EMEvalStatus::kInvalidNode;
    return;
  }
  const double resistance = power_edge.get_resistance();
  if (!std::isfinite(resistance) || resistance <= 0.0) {
    evaluation.status = EMEvalStatus::kInvalidResistance;
    return;
  }
  double delta = power_graph.get_node_list()[power_edge.get_first_node_id()].get_voltage()
                 - power_graph.get_node_list()[power_edge.get_second_node_id()].get_voltage();
  evaluation.voltage_delta_v = delta;
  evaluation.signed_current_a = delta / resistance;
  double current = std::abs(evaluation.signed_current_a);
  if (!std::isfinite(delta) || !std::isfinite(evaluation.signed_current_a) || !std::isfinite(current)) {
    evaluation.status = EMEvalStatus::kNonFiniteInput;
    return;
  }
  power_edge.set_current(current);
  ea_model.set_power_edge_num(ea_model.get_power_edge_num() + 1);
  ea_model.set_total_current(ea_model.get_total_current() + current);
  ea_model.set_max_current(std::max(ea_model.get_max_current(), current));
  double limit = unavailable;
  if (power_edge.get_type() == PowerEdgeType::kWire) {
    auto binding = _metal_rules.at(power_edge.get_layer_name());
    evaluation.match = binding.match;
    if (!binding.rule) { evaluation.status = EMEvalStatus::kNoRule; return; }
    power_edge.set_em_rule_name(binding.rule->get_name());
    evaluation.effective_width_um = getMicronValue(power_edge.get_width())
        * EMIRDM.getDatabase().get_em_tech().get_half_node_scale_factor() - binding.rule->get_em_adjust_um();
    if (EMIRDM.getDatabase().get_micron_dbu() <= 0 || power_edge.get_width() <= 0
        || !std::isfinite(evaluation.effective_width_um) || evaluation.effective_width_um <= 0.0) {
      evaluation.status = EMEvalStatus::kInvalidGeometry; return;
    }
    limit = calcWireEMLimit(power_edge, *binding.rule);
  } else if (power_edge.get_type() == PowerEdgeType::kVia) {
    const bool has_cut_layer = !power_edge.get_cut_layer_name().empty();
    auto binding = has_cut_layer ? _via_cut_rules.at(power_edge.get_cut_layer_name()) : _via_rules.at(power_edge.get_via_name());
    evaluation.match = binding.match;
    if (!binding.rule) { evaluation.status = EMEvalStatus::kNoRule; return; }
    power_edge.set_em_rule_name(binding.rule->get_name());
    if (power_edge.get_cut_num() <= 0) { evaluation.status = EMEvalStatus::kInvalidGeometry; return; }
    limit = calcViaEMLimit(power_edge, *binding.rule);
  } else {
    evaluation.status = EMEvalStatus::kInvalidGeometry; return;
  }
  power_edge.set_em_limit(limit);
  if (!std::isfinite(limit) || limit <= 0.0) { evaluation.status = EMEvalStatus::kInvalidLimit; return; }
  double ratio = 100.0 * (current / limit);
  if (!std::isfinite(ratio)) { evaluation.status = EMEvalStatus::kInvalidLimit; return; }
  evaluation.status = EMEvalStatus::kEvaluated;
  power_edge.set_has_em_limit(true);
  power_edge.set_em_ratio_percent(ratio);
  power_edge.set_current_density(ratio);
  power_edge.set_is_violation(ratio > EMIRDM.getConfig().em_violation_threshold_percent);
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

EMViaRule* EMAnalyzer::getViaRule(const std::string& via_name, bool allow_name_fallback)
{
  EMTech& em_tech = EMIRDM.getDatabase().get_em_tech();
  std::string normalized_via_name = normalizeRuleName(via_name);
  for (const std::string& alias : getRuleAliases(via_name)) {
    auto iter = em_tech.get_via_rule_map().find(alias);
    if (iter != em_tech.get_via_rule_map().end()) {
      return &iter->second;
    }
  }
  // A known physical cut layer must not fall back to another layer by prefix.
  if (!allow_name_fallback) return nullptr;
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
  return metal_rule.get_em_limit_ma_per_um() * effective_width_um / 1000.0;
}

double EMAnalyzer::calcViaEMLimit(PowerEdge& power_edge, EMViaRule& via_rule)
{
  // A scalar technology VIA EM value is a per-cut limit. Geometry area is not an
  // interchangeable multiplier unless an area-dependent rule table is present.
  double scale = static_cast<double>(power_edge.get_cut_num());
  return via_rule.get_em_limit_ma() * scale / 1000.0;
}

}  // namespace iemir
