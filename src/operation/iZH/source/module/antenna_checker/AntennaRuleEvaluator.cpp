// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
// ***************************************************************************************

#include "AntennaRuleEvaluator.hpp"

#include "Utility.hpp"

#include "IdbLayer.h"
#include "IdbLayout.h"

namespace izh {

void AntennaRuleEvaluator::initLayers(ACModel& ac_model, idb::IdbDesign* design)
{
  std::vector<ACAntennaRule>& rules = ac_model.get_rule_list();
  std::vector<std::vector<const ACAntennaRule*>>& rule_map = ac_model.get_rule_map();
  std::vector<double>& thickness_by_order = ac_model.get_thickness_by_order();
  std::vector<bool>& conductive_order = ac_model.get_conductive_order();
  int& max_layer_order = ac_model.get_max_layer_order();
  if (!design || !design->get_layout()) {
    return;
  }

  idb::IdbLayers* layers = design->get_layout()->get_layers();
  if (!layers) {
    return;
  }

  rules.clear();
  rule_map.clear();
  thickness_by_order.clear();

  max_layer_order = 0;

  for (idb::IdbLayer* layer : layers->get_layers()) {
    if (layer) {
      max_layer_order = std::max(max_layer_order, static_cast<int>(layer->get_order()));
    }
  }

  for (idb::IdbLayer* layer : layers->get_routing_layers()) {
    idb::IdbLayerRouting* routing = dynamic_cast<idb::IdbLayerRouting*>(layer);
    if (routing == nullptr) {
      continue;
    }

    bool has_any_rule = false;

    has_any_rule |= routing->has_antenna_area_ratio();
    has_any_rule |= routing->has_antenna_cum_area_ratio();
    has_any_rule |= routing->has_antenna_area_factor();
    has_any_rule |= routing->has_antenna_side_area_ratio();
    has_any_rule |= routing->has_antenna_cum_side_area_ratio();
    has_any_rule |= routing->has_antenna_side_area_factor();
    has_any_rule |= routing->has_antenna_gate_plus_diff();
    has_any_rule |= routing->has_antenna_area_minus_diff();
    has_any_rule |= routing->has_antenna_diff_area_ratio();
    has_any_rule |= routing->has_antenna_cum_diff_area_ratio();
    has_any_rule |= routing->has_antenna_diff_side_area_ratio();
    has_any_rule |= routing->has_antenna_cum_diff_side_area_ratio();
    has_any_rule |= routing->has_antenna_diff_area_ratio_pwl();
    has_any_rule |= routing->has_antenna_cum_diff_area_ratio_pwl();
    has_any_rule |= routing->has_antenna_diff_side_area_ratio_pwl();
    has_any_rule |= routing->has_antenna_cum_diff_side_area_ratio_pwl();
    has_any_rule |= routing->has_antenna_area_diff_reduce_pwl();
    has_any_rule |= routing->get_antenna_cum_routing_plus_cut();

    if (!has_any_rule) {
      continue;
    }

    ACAntennaRule rule;
    rule.layer_name = routing->get_name();
    rule.layer_order = static_cast<int>(routing->get_order());
    rule.is_routing = true;
    rule.thickness_um = Utility::getRatio(static_cast<int64_t>(routing->get_thickness()), ac_model.get_micron_dbu());

    if (routing->has_antenna_area_ratio()) {
      rule.area_ratio = routing->get_antenna_area_ratio();
    }
    if (routing->has_antenna_cum_area_ratio()) {
      rule.cum_area_ratio = routing->get_antenna_cum_area_ratio();
    }
    if (routing->has_antenna_area_factor()) {
      rule.area_factor = routing->get_antenna_area_factor();
    }

    rule.area_factor_diffuse_only = routing->get_antenna_area_factor_diffuse_only();

    if (routing->has_antenna_side_area_ratio()) {
      rule.side_area_ratio = routing->get_antenna_side_area_ratio();
    }
    if (routing->has_antenna_cum_side_area_ratio()) {
      rule.cum_side_area_ratio = routing->get_antenna_cum_side_area_ratio();
    }
    if (routing->has_antenna_side_area_factor()) {
      rule.side_area_factor = routing->get_antenna_side_area_factor();
    }

    rule.side_area_factor_diffuse_only = routing->get_antenna_side_area_factor_diffuse_only();

    if (routing->has_antenna_gate_plus_diff()) {
      rule.gate_plus_diff = routing->get_antenna_gate_plus_diff();
    }
    if (routing->has_antenna_area_minus_diff()) {
      rule.area_minus_diff = routing->get_antenna_area_minus_diff();
    }
    if (routing->has_antenna_diff_area_ratio()) {
      rule.diff_area_ratio = routing->get_antenna_diff_area_ratio();
    }
    if (routing->has_antenna_cum_diff_area_ratio()) {
      rule.cum_diff_area_ratio = routing->get_antenna_cum_diff_area_ratio();
    }
    if (routing->has_antenna_diff_side_area_ratio()) {
      rule.diff_side_area_ratio = routing->get_antenna_diff_side_area_ratio();
    }
    if (routing->has_antenna_cum_diff_side_area_ratio()) {
      rule.cum_diff_side_area_ratio = routing->get_antenna_cum_diff_side_area_ratio();
    }

    rule.cum_routing_plus_cut = routing->get_antenna_cum_routing_plus_cut();

    if (routing->has_antenna_diff_area_ratio_pwl()) {
      rule.diff_area_ratio_pwl = routing->get_antenna_diff_area_ratio_pwl();
    }
    if (routing->has_antenna_cum_diff_area_ratio_pwl()) {
      rule.cum_diff_area_ratio_pwl = routing->get_antenna_cum_diff_area_ratio_pwl();
    }
    if (routing->has_antenna_diff_side_area_ratio_pwl()) {
      rule.diff_side_area_ratio_pwl = routing->get_antenna_diff_side_area_ratio_pwl();
    }
    if (routing->has_antenna_cum_diff_side_area_ratio_pwl()) {
      rule.cum_diff_side_area_ratio_pwl = routing->get_antenna_cum_diff_side_area_ratio_pwl();
    }
    if (routing->has_antenna_area_diff_reduce_pwl()) {
      rule.area_diff_reduce_pwl = routing->get_antenna_area_diff_reduce_pwl();
    }

    rules.push_back(rule);
  }

  for (idb::IdbLayer* layer : layers->get_cut_layers()) {
    idb::IdbLayerCut* cut = dynamic_cast<idb::IdbLayerCut*>(layer);
    if (cut == nullptr) {
      continue;
    }

    bool has_any_rule = false;

    has_any_rule |= cut->has_antenna_area_ratio();
    has_any_rule |= cut->has_antenna_cum_area_ratio();
    has_any_rule |= cut->has_antenna_area_factor();
    has_any_rule |= cut->has_antenna_diff_area_ratio();
    has_any_rule |= cut->has_antenna_cum_diff_area_ratio();
    has_any_rule |= cut->has_antenna_gate_plus_diff();
    has_any_rule |= cut->has_antenna_area_minus_diff();
    has_any_rule |= cut->has_antenna_diff_area_ratio_pwl();
    has_any_rule |= cut->has_antenna_cum_diff_area_ratio_pwl();
    has_any_rule |= cut->has_antenna_area_diff_reduce_pwl();
    has_any_rule |= cut->get_antenna_cum_routing_plus_cut();

    if (!has_any_rule) {
      continue;
    }

    ACAntennaRule rule;
    rule.layer_name = cut->get_name();
    rule.layer_order = static_cast<int>(cut->get_order());
    rule.is_routing = false;

    if (cut->has_antenna_area_factor()) {
      rule.area_factor = cut->get_antenna_area_factor();
    }
    rule.area_factor_diffuse_only = cut->get_antenna_area_factor_diffuse_only();

    if (cut->has_antenna_area_ratio()) {
      rule.area_ratio = cut->get_antenna_area_ratio();
    }
    if (cut->has_antenna_cum_area_ratio()) {
      rule.cum_area_ratio = cut->get_antenna_cum_area_ratio();
    }
    if (cut->has_antenna_diff_area_ratio()) {
      rule.diff_area_ratio = cut->get_antenna_diff_area_ratio();
    }
    if (cut->has_antenna_cum_diff_area_ratio()) {
      rule.cum_diff_area_ratio = cut->get_antenna_cum_diff_area_ratio();
    }
    if (cut->has_antenna_gate_plus_diff()) {
      rule.gate_plus_diff = cut->get_antenna_gate_plus_diff();
    }
    if (cut->has_antenna_area_minus_diff()) {
      rule.area_minus_diff = cut->get_antenna_area_minus_diff();
    }

    rule.cum_routing_plus_cut = cut->get_antenna_cum_routing_plus_cut();

    if (cut->has_antenna_diff_area_ratio_pwl()) {
      rule.diff_area_ratio_pwl = cut->get_antenna_diff_area_ratio_pwl();
    }
    if (cut->has_antenna_cum_diff_area_ratio_pwl()) {
      rule.cum_diff_area_ratio_pwl = cut->get_antenna_cum_diff_area_ratio_pwl();
    }
    if (cut->has_antenna_area_diff_reduce_pwl()) {
      rule.area_diff_reduce_pwl = cut->get_antenna_area_diff_reduce_pwl();
    }

    rules.push_back(rule);
  }

  for (idb::IdbLayer* layer : layers->get_layers()) {
    idb::IdbLayerMasterslice* ms = dynamic_cast<idb::IdbLayerMasterslice*>(layer);
    if (ms == nullptr) {
      continue;
    }

    bool has_any_rule = false;

    has_any_rule |= ms->has_antenna_area_ratio();
    has_any_rule |= ms->has_antenna_cum_area_ratio();
    has_any_rule |= ms->has_antenna_area_factor();
    has_any_rule |= ms->has_antenna_side_area_ratio();
    has_any_rule |= ms->has_antenna_cum_side_area_ratio();
    has_any_rule |= ms->has_antenna_side_area_factor();
    has_any_rule |= ms->has_antenna_gate_plus_diff();
    has_any_rule |= ms->has_antenna_area_minus_diff();
    has_any_rule |= ms->has_antenna_diff_area_ratio();
    has_any_rule |= ms->has_antenna_cum_diff_area_ratio();
    has_any_rule |= ms->has_antenna_diff_side_area_ratio();
    has_any_rule |= ms->has_antenna_cum_diff_side_area_ratio();
    has_any_rule |= ms->has_antenna_diff_area_ratio_pwl();
    has_any_rule |= ms->has_antenna_cum_diff_area_ratio_pwl();
    has_any_rule |= ms->has_antenna_diff_side_area_ratio_pwl();
    has_any_rule |= ms->has_antenna_cum_diff_side_area_ratio_pwl();
    has_any_rule |= ms->has_antenna_area_diff_reduce_pwl();
    has_any_rule |= ms->get_antenna_cum_routing_plus_cut();

    if (!has_any_rule) {
      continue;
    }

    ACAntennaRule rule;
    rule.layer_name = ms->get_name();
    rule.layer_order = static_cast<int>(ms->get_order());
    rule.is_routing = true;
    rule.thickness_um = Utility::getRatio(static_cast<int64_t>(ms->get_thickness()), ac_model.get_micron_dbu());

    if (ms->has_antenna_area_ratio()) {
      rule.area_ratio = ms->get_antenna_area_ratio();
    }
    if (ms->has_antenna_cum_area_ratio()) {
      rule.cum_area_ratio = ms->get_antenna_cum_area_ratio();
    }
    if (ms->has_antenna_area_factor()) {
      rule.area_factor = ms->get_antenna_area_factor();
    }

    rule.area_factor_diffuse_only = ms->get_antenna_area_factor_diffuse_only();

    if (ms->has_antenna_side_area_ratio()) {
      rule.side_area_ratio = ms->get_antenna_side_area_ratio();
    }
    if (ms->has_antenna_cum_side_area_ratio()) {
      rule.cum_side_area_ratio = ms->get_antenna_cum_side_area_ratio();
    }
    if (ms->has_antenna_side_area_factor()) {
      rule.side_area_factor = ms->get_antenna_side_area_factor();
    }

    rule.side_area_factor_diffuse_only = ms->get_antenna_side_area_factor_diffuse_only();

    if (ms->has_antenna_gate_plus_diff()) {
      rule.gate_plus_diff = ms->get_antenna_gate_plus_diff();
    }
    if (ms->has_antenna_area_minus_diff()) {
      rule.area_minus_diff = ms->get_antenna_area_minus_diff();
    }
    if (ms->has_antenna_diff_area_ratio()) {
      rule.diff_area_ratio = ms->get_antenna_diff_area_ratio();
    }
    if (ms->has_antenna_cum_diff_area_ratio()) {
      rule.cum_diff_area_ratio = ms->get_antenna_cum_diff_area_ratio();
    }
    if (ms->has_antenna_diff_side_area_ratio()) {
      rule.diff_side_area_ratio = ms->get_antenna_diff_side_area_ratio();
    }
    if (ms->has_antenna_cum_diff_side_area_ratio()) {
      rule.cum_diff_side_area_ratio = ms->get_antenna_cum_diff_side_area_ratio();
    }

    rule.cum_routing_plus_cut = ms->get_antenna_cum_routing_plus_cut();

    if (ms->has_antenna_diff_area_ratio_pwl()) {
      rule.diff_area_ratio_pwl = ms->get_antenna_diff_area_ratio_pwl();
    }
    if (ms->has_antenna_cum_diff_area_ratio_pwl()) {
      rule.cum_diff_area_ratio_pwl = ms->get_antenna_cum_diff_area_ratio_pwl();
    }
    if (ms->has_antenna_diff_side_area_ratio_pwl()) {
      rule.diff_side_area_ratio_pwl = ms->get_antenna_diff_side_area_ratio_pwl();
    }
    if (ms->has_antenna_cum_diff_side_area_ratio_pwl()) {
      rule.cum_diff_side_area_ratio_pwl = ms->get_antenna_cum_diff_side_area_ratio_pwl();
    }
    if (ms->has_antenna_area_diff_reduce_pwl()) {
      rule.area_diff_reduce_pwl = ms->get_antenna_area_diff_reduce_pwl();
    }

    rules.push_back(rule);
  }

  if (max_layer_order < 0) {
    max_layer_order = 0;
  }

  rule_map.resize(static_cast<size_t>(max_layer_order) + 1);
  thickness_by_order.assign(static_cast<size_t>(max_layer_order) + 1, 0.0);
  conductive_order.assign(static_cast<size_t>(max_layer_order) + 1, false);

  for (const auto& rule : rules) {
    if (rule.layer_order >= 0 && static_cast<size_t>(rule.layer_order) < rule_map.size()) {
      rule_map[rule.layer_order].push_back(&rule);
      conductive_order[rule.layer_order] = true;

      if (rule.is_routing && rule.thickness_um > 0.0 && thickness_by_order[rule.layer_order] <= 0.0) {
        thickness_by_order[rule.layer_order] = rule.thickness_um;
      }
    }
  }
}

const ACAntennaRule* AntennaRuleEvaluator::pickRule(const ACModel& ac_model, const int layer_order, const bool routing)
{
  const std::vector<std::vector<const ACAntennaRule*>>& rule_map = ac_model.get_rule_map();
  if (layer_order < 0 || static_cast<size_t>(layer_order) >= rule_map.size()) {
    return nullptr;
  }

  for (const ACAntennaRule* rule : rule_map[layer_order]) {
    if (rule != nullptr && rule->is_routing == routing) {
      return rule;
    }
  }

  return nullptr;
}

ACThresholdPick AntennaRuleEvaluator::pickThreshold(const double plain_ratio, const double diff_ratio,
                                                    const std::vector<std::pair<double, double>>& diff_pwl, const double diff_area,
                                                    const bool diff_connected)
{
  const bool has_diff = (diff_ratio >= 0.0) || (!diff_pwl.empty());
  const bool has_plain = (plain_ratio >= 0.0);
  const bool diff_active = diff_connected || !Utility::equalDoubleByError(diff_area, 0.0, ZH_ERROR);
  bool use_diff = has_diff && (diff_active || !has_plain);

  if (use_diff && !diff_pwl.empty() && Utility::equalDoubleByError(diff_area, 0.0, ZH_ERROR) && has_plain) {
    use_diff = false;
  }

  if (use_diff) {
    const double threshold = !diff_pwl.empty() ? Utility::getPWLValue(diff_pwl, diff_area, diff_ratio) : diff_ratio;
    return {true, threshold, true};
  }

  if (has_plain) {
    return {true, plain_ratio, false};
  }

  return {};
}

}  // namespace izh
