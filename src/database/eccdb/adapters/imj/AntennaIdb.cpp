// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2026 Beijing Institute of Open Source Chip

#include "AntennaIdb.h"

#include <cstdint>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

#include "DataManager.hpp"
#include "IdbDesign.h"
#include "IdbLayer.h"
#include "IdbNet.h"
#include "IdbPins.h"
#include "IdbRegularWire.h"
#include "IdbVias.h"
#include "idm.h"
#include "json.hpp"

namespace imj {
namespace {

using Json = nlohmann::json;

[[nodiscard]] bool hasRoutingRule(idb::IdbLayerRouting& layer)
{
  return layer.has_antenna_area_ratio() || layer.has_antenna_cum_area_ratio() || layer.has_antenna_area_factor()
         || layer.has_antenna_side_area_ratio() || layer.has_antenna_cum_side_area_ratio() || layer.has_antenna_side_area_factor()
         || layer.has_antenna_gate_plus_diff() || layer.has_antenna_area_minus_diff() || layer.has_antenna_diff_area_ratio()
         || layer.has_antenna_cum_diff_area_ratio() || layer.has_antenna_diff_side_area_ratio()
         || layer.has_antenna_cum_diff_side_area_ratio() || layer.has_antenna_diff_area_ratio_pwl()
         || layer.has_antenna_cum_diff_area_ratio_pwl() || layer.has_antenna_diff_side_area_ratio_pwl()
         || layer.has_antenna_cum_diff_side_area_ratio_pwl() || layer.has_antenna_area_diff_reduce_pwl()
         || layer.get_antenna_cum_routing_plus_cut();
}

[[nodiscard]] bool hasCutRule(idb::IdbLayerCut& layer)
{
  return layer.has_antenna_area_ratio() || layer.has_antenna_cum_area_ratio() || layer.has_antenna_area_factor()
         || layer.has_antenna_diff_area_ratio() || layer.has_antenna_cum_diff_area_ratio() || layer.has_antenna_gate_plus_diff()
         || layer.has_antenna_area_minus_diff() || layer.has_antenna_diff_area_ratio_pwl()
         || layer.has_antenna_cum_diff_area_ratio_pwl() || layer.has_antenna_area_diff_reduce_pwl()
         || layer.get_antenna_cum_routing_plus_cut();
}

[[nodiscard]] bool hasMastersliceRule(idb::IdbLayerMasterslice& layer)
{
  return layer.has_antenna_area_ratio() || layer.has_antenna_cum_area_ratio() || layer.has_antenna_area_factor()
         || layer.has_antenna_side_area_ratio() || layer.has_antenna_cum_side_area_ratio() || layer.has_antenna_side_area_factor()
         || layer.has_antenna_gate_plus_diff() || layer.has_antenna_area_minus_diff() || layer.has_antenna_diff_area_ratio()
         || layer.has_antenna_cum_diff_area_ratio() || layer.has_antenna_diff_side_area_ratio()
         || layer.has_antenna_cum_diff_side_area_ratio() || layer.has_antenna_diff_area_ratio_pwl()
         || layer.has_antenna_cum_diff_area_ratio_pwl() || layer.has_antenna_diff_side_area_ratio_pwl()
         || layer.has_antenna_cum_diff_side_area_ratio_pwl() || layer.has_antenna_area_diff_reduce_pwl()
         || layer.get_antenna_cum_routing_plus_cut();
}

[[nodiscard]] std::set<int> antennaRuleLayers(idb::IdbLayers& layers, uint64_t& rule_count)
{
  std::set<int> rule_layers;
  rule_count = 0;
  for (idb::IdbLayer* candidate : layers.get_routing_layers()) {
    auto* routing = dynamic_cast<idb::IdbLayerRouting*>(candidate);
    if (routing != nullptr && hasRoutingRule(*routing)) {
      ++rule_count;
      rule_layers.insert(static_cast<int>(routing->get_order()));
    }
  }
  for (idb::IdbLayer* candidate : layers.get_cut_layers()) {
    auto* cut = dynamic_cast<idb::IdbLayerCut*>(candidate);
    if (cut != nullptr && hasCutRule(*cut)) {
      ++rule_count;
      rule_layers.insert(static_cast<int>(cut->get_order()));
    }
  }
  for (idb::IdbLayer* candidate : layers.get_layers()) {
    auto* masterslice = dynamic_cast<idb::IdbLayerMasterslice*>(candidate);
    if (masterslice != nullptr && hasMastersliceRule(*masterslice)) {
      ++rule_count;
      rule_layers.insert(static_cast<int>(masterslice->get_order()));
    }
  }
  return rule_layers;
}

[[nodiscard]] NativeAntennaSummary readCheckerResult(const std::filesystem::path& step_json)
{
  std::ifstream input(step_json);
  if (!input.is_open()) {
    throw std::runtime_error("legacy iDB antenna checker did not produce step JSON: " + step_json.string());
  }

  Json payload;
  try {
    input >> payload;
  } catch (const std::exception& error) {
    throw std::runtime_error("cannot parse legacy iDB antenna step JSON '" + step_json.string() + "': " + error.what());
  }

  if (payload.at("schema_version").get<int64_t>() != 1) {
    throw std::runtime_error("unsupported legacy iDB antenna step JSON schema");
  }
  if (payload.at("step").get<std::string>() != "antennaCheck") {
    throw std::runtime_error("legacy iDB antenna step JSON has an unexpected step name");
  }

  const Json& check = payload.at("antenna_check");
  NativeAntennaSummary summary;
  summary.violation_count = check.at("violation_count").get<uint64_t>();
  summary.net_count = check.at("signal_net_count").get<uint64_t>();
  summary.pins_missing_antenna_info = check.at("pins_missing_antenna_info").get<uint64_t>();
  summary.pins_with_gate_area = check.at("pins_with_gate_area").get<uint64_t>();
  summary.components_without_gate = check.at("components_without_gate").get<uint64_t>();
  summary.skipped_segments = check.at("skipped_segments").get<uint64_t>();
  summary.conductors_out_of_range = check.at("conductors_out_of_range").get<uint64_t>();
  summary.partial_areas_dropped = check.at("partial_areas_dropped").get<uint64_t>();

  const Json& violations = payload.at("violations");
  summary.violations.reserve(violations.size());
  for (const Json& violation : violations) {
    const std::string net = violation.at("net").get<std::string>();
    const std::string layer = violation.at("layer").get<std::string>();
    const std::string type = violation.at("type").get<std::string>();
    const double ratio = violation.at("ratio").get<double>();
    const double threshold = violation.at("threshold").get<double>();
    const Json& location = violation.at("location");
    const double lx = location.at("lx").get<double>();
    const double ly = location.at("ly").get<double>();
    const double hx = location.at("hx").get<double>();
    const double hy = location.at("hy").get<double>();

    std::ostringstream line;
    line << net << ':' << layer << ' ' << type << " ratio=" << ratio << " threshold=" << threshold << " bbox=(" << lx << ',' << ly
         << ',' << hx << ',' << hy << ')';
    summary.violations.push_back(line.str());
  }
  return summary;
}

void appendLegacyCoverage(NativeAntennaSummary& summary)
{
  idb::IdbDesign* design = nullptr;
  if (dmInst != nullptr) {
    if (auto* service = dmInst->get_idb_def_service(); service != nullptr) {
      design = service->get_design();
    }
  }
  if (design == nullptr || design->get_layout() == nullptr || design->get_net_list() == nullptr) {
    summary.coverage_diagnostics.push_back("legacy iDB antenna checker has no loaded layout/netlist");
    return;
  }

  auto* layers = design->get_layout()->get_layers();
  if (layers == nullptr) {
    summary.coverage_diagnostics.push_back("legacy iDB antenna checker has no technology layer list");
    return;
  }

  const std::set<int> rule_layers = antennaRuleLayers(*layers, summary.rule_layer_count);
  const bool has_rules = !rule_layers.empty();
  std::set<int> observed_layers;
  std::set<int> missing_layers;
  uint64_t clock_net_count = 0;
  uint64_t rect_segment_count = 0;

  for (idb::IdbNet* net : design->get_net_list()->get_net_list()) {
    if (net == nullptr) {
      continue;
    }
    if (net->is_clock()) {
      ++clock_net_count;
      continue;
    }
    if (!net->is_signal()) {
      continue;
    }

    const bool has_instance_pin = net->get_instance_pin_list() != nullptr
                                  && !net->get_instance_pin_list()->get_pin_list().empty();
    const bool has_io_pin = net->get_io_pins() != nullptr && !net->get_io_pins()->get_pin_list().empty();
    if (has_instance_pin || has_io_pin) {
      ++summary.connected_net_count;
    }

    auto* wires = net->get_wire_list();
    if (wires == nullptr) {
      continue;
    }
    for (idb::IdbRegularWire* wire : wires->get_wire_list()) {
      if (wire == nullptr) {
        continue;
      }
      ++summary.wire_count;
      for (idb::IdbRegularWireSegment* segment : wire->get_segment_list()) {
        if (segment == nullptr) {
          continue;
        }
        if (segment->is_wire()) {
          if (idb::IdbLayer* layer = segment->get_layer(); layer != nullptr) {
            const int order = static_cast<int>(layer->get_order());
            const bool routing_like = layer->is_routing()
                                      || (layer->get_type() == idb::IdbLayerType::kLayerMasterslice
                                          && rule_layers.contains(order));
            if (routing_like) {
              observed_layers.insert(order);
            }
          }
        }
        if (segment->is_rect()) {
          ++rect_segment_count;
        }
        if (!segment->is_via()) {
          continue;
        }
        for (idb::IdbVia* via : segment->get_via_list()) {
          if (via == nullptr) {
            continue;
          }
          ++summary.via_geometry_count;
          const auto observe = [&observed_layers](idb::IdbLayerShape shape) {
            if (idb::IdbLayer* layer = shape.get_layer(); layer != nullptr) {
              observed_layers.insert(static_cast<int>(layer->get_order()));
            }
          };
          observe(via->get_bottom_layer_shape());
          observe(via->get_cut_layer_shape());
          observe(via->get_top_layer_shape());
        }
      }
    }
  }

  for (const int layer : observed_layers) {
    if (rule_layers.contains(layer)) {
      ++summary.covered_rule_layer_count;
    } else {
      missing_layers.insert(layer);
    }
  }
  summary.skipped_rule_layer_count = missing_layers.size();

  summary.checked = has_rules;
  if (!has_rules) {
    summary.coverage_diagnostics.push_back("legacy iDB technology contains no antenna layer rules; no antenna ratios were evaluated");
  }
  if (summary.net_count == 0u) {
    summary.coverage_diagnostics.push_back("legacy iDB design contains no SIGNAL net evaluated by the antenna checker");
  }
  for (const int layer : missing_layers) {
    summary.coverage_diagnostics.push_back("legacy iDB routed geometry on layer order " + std::to_string(layer)
                                           + " has no antenna rule");
  }
  if (clock_net_count != 0u) {
    summary.coverage_diagnostics.push_back("legacy iDB antenna checker excludes " + std::to_string(clock_net_count)
                                           + " CLOCK nets from its SIGNAL-only scope");
  }
  if (rect_segment_count != 0u) {
    summary.unsupported_route_shape_count = rect_segment_count;
    summary.coverage_diagnostics.push_back("legacy iDB antenna checker does not evaluate " + std::to_string(rect_segment_count)
                                           + " DEF RECT route segments");
  }
  if (summary.pins_missing_antenna_info != 0u) {
    summary.coverage_diagnostics.push_back(std::to_string(summary.pins_missing_antenna_info)
                                           + " instance pins have no antenna gate/diffusion annotation");
  }
  if (summary.components_without_gate != 0u) {
    summary.coverage_diagnostics.push_back(std::to_string(summary.components_without_gate)
                                           + " conductor components had no connected gate at their layer-event evaluation time"
                                             " (diagnostic only; not final net coverage)");
  }
  if (summary.skipped_segments != 0u || summary.conductors_out_of_range != 0u || summary.partial_areas_dropped != 0u) {
    summary.coverage_diagnostics.push_back("legacy iDB antenna graph dropped unsupported route geometry or layer-qualified metadata");
  }
  summary.coverage_complete = summary.checked && summary.net_count != 0u && summary.skipped_rule_layer_count == 0u
                              && summary.pins_missing_antenna_info == 0u && summary.skipped_segments == 0u
                              && summary.conductors_out_of_range == 0u && summary.partial_areas_dropped == 0u
                              && summary.unsupported_route_shape_count == 0u && clock_net_count == 0u;
}

}  // namespace

NativeAntennaSummary checkAntennaIdb(MJConfigMap config, const std::filesystem::path& step_json)
{
  if (step_json.empty()) {
    throw std::invalid_argument("legacy iDB antenna step JSON path must not be empty");
  }
  if (MJI.hasDesignOperations()) {
    throw std::logic_error("checkAntennaIdb requires the legacy iDB design source");
  }

  std::error_code remove_error;
  std::filesystem::remove(step_json, remove_error);
  if (remove_error) {
    throw std::runtime_error("cannot remove stale legacy iDB antenna step JSON '" + step_json.string() + "': "
                             + remove_error.message());
  }
  config["-step_json"] = step_json.string();
  MJI.checkAntenna(std::move(config));

  auto summary = readCheckerResult(step_json);
  appendLegacyCoverage(summary);
  return summary;
}

}  // namespace imj
