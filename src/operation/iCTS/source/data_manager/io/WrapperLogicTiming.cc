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
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
/**
 * @file WrapperLogicTiming.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-09-01
 * @brief iDB-backed signal-layer RC authority queries.
 */

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "IdbDesign.h"
#include "IdbInstance.h"
#include "IdbLayer.h"
#include "IdbLayout.h"
#include "IdbNet.h"
#include "IdbPins.h"
#include "IdbTerm.h"
#include "IdbUnits.h"
#include "Wrapper.hh"
#include "idm.h"
#include "liberty/Lib.hh"
#include "salt/base/flute.h"
#include "salt/base/net.h"
#include "salt/base/tree.h"

namespace icts {
namespace {

auto canonicalPinName(idb::IdbPin* pin) -> std::string
{
  if (pin == nullptr) {
    return {};
  }
  if (pin->is_io_pin()) {
    return pin->get_pin_name();
  }
  auto* instance = pin->get_instance();
  if (instance == nullptr || instance->get_name().empty() || pin->get_pin_name().empty()) {
    return {};
  }
  return instance->get_name() + "/" + pin->get_pin_name();
}

auto isCombinationalArc(idb::LibArc* arc) -> bool
{
  if (arc == nullptr || arc->isDisableArc() != 0U || arc->isDelayArc() == 0U) {
    return false;
  }
  const auto timing_type = arc->get_timing_type();
  return timing_type == idb::LibArc::TimingType::kDefault || timing_type == idb::LibArc::TimingType::kComb || timing_type == idb::LibArc::TimingType::kCombRise
         || timing_type == idb::LibArc::TimingType::kCombFall;
}

auto firstEnabledArc(idb::LibArcSet* arc_set) -> idb::LibArc*
{
  if (arc_set == nullptr) {
    return nullptr;
  }
  idb::LibArc* first = nullptr;
  for (const auto& arc_holder : arc_set->get_arcs()) {
    auto* arc = arc_holder.get();
    if (arc == nullptr || arc->isDisableArc() != 0U) {
      continue;
    }
    if (first == nullptr) {
      first = arc;
    }
    if (arc->get_when().empty()) {
      return arc;
    }
  }
  return first;
}

auto toWrapperTransition(idb::LibArc* arc) -> WrapperTimingTransition
{
  return arc != nullptr && (arc->isFallingTriggerArc() != 0U || arc->isFallingEdgeCheck() != 0U) ? WrapperTimingTransition::kFall
                                                                                                 : WrapperTimingTransition::kRise;
}

auto appendTimingNode(idb::IdbPin* pin, idb::LibCell* lib_cell, WrapperTimingGraph& graph, std::unordered_map<std::string, std::size_t>& node_index) -> bool
{
  const auto pin_name = canonicalPinName(pin);
  if (pin_name.empty()) {
    return false;
  }
  if (node_index.contains(pin_name)) {
    return true;
  }
  auto* instance = pin->get_instance();
  auto* coordinate = pin->get_average_coordinate();
  if (coordinate == nullptr) {
    return false;
  }
  WrapperTimingNode node;
  node.pin_name = pin_name;
  node.inst_name = instance == nullptr ? std::string{} : instance->get_name();
  node.cell_master = instance == nullptr || instance->get_cell_master() == nullptr ? std::string{} : instance->get_cell_master()->get_name();
  node.top_level = pin->is_io_pin();
  node.x_dbu = coordinate->get_x();
  node.y_dbu = coordinate->get_y();
  const auto local_pin = pin->get_pin_name();
  node.port_name = local_pin;
  if (node.top_level) {
    // Top-level ports have no Liberty cell. Their direction belongs to the
    // actual iDB terminal, not to the driver's position in a net traversal.
    const auto* term = pin->get_term();
    if (term == nullptr) {
      return false;
    }
    const auto direction = term->get_direction();
    node.input = direction == idb::IdbConnectDirection::kInput || direction == idb::IdbConnectDirection::kInOut;
    node.output = direction == idb::IdbConnectDirection::kOutput || direction == idb::IdbConnectDirection::kOutputTriState
                  || direction == idb::IdbConnectDirection::kInOut;
    if (!node.input && !node.output) {
      return false;
    }
  }
  auto* lib_port = lib_cell == nullptr ? nullptr : lib_cell->get_cell_port_or_port_bus(local_pin.c_str());
  if (!node.top_level && lib_port == nullptr) {
    graph.status = WrapperTimingGraphStatus::kUnsupported;
    graph.diagnostic = "signal_pin_liberty_model_unavailable:" + pin_name + ":" + node.cell_master;
    return false;
  }
  if (lib_port != nullptr) {
    const auto* library = lib_cell->get_owner_lib();
    if (library == nullptr || !library->has_time_unit() || !library->has_cap_unit()) {
      graph.status = WrapperTimingGraphStatus::kUnsupported;
      graph.diagnostic = "signal_pin_liberty_units_unavailable:" + pin_name + ":" + node.cell_master;
      return false;
    }
    node.logic_function = lib_port->get_func_expr_str();
    node.clock_pin = lib_port->isClock() || lib_port->get_clock_gate_clock_pin();
    node.input = lib_port->isInput() != 0U;
    node.output = lib_port->isOutput() != 0U;
    if (node.input) {
      auto* owner_lib = lib_cell->get_owner_lib();
      for (const auto [analysis_index, analysis] : {std::pair{0U, idb::AnalysisMode::kMin}, std::pair{1U, idb::AnalysisMode::kMax}}) {
        for (const auto [transition_index, transition] : {std::pair{0U, idb::TransType::kRise}, std::pair{1U, idb::TransType::kFall}}) {
          auto cap = lib_port->get_port_cap(analysis, transition);
          if (!cap.has_value() && lib_port->has_port_cap()) {
            cap = lib_port->get_port_cap();
          }
          if (!cap.has_value() || !std::isfinite(*cap) || *cap < 0.0 || owner_lib == nullptr) {
            graph.status = WrapperTimingGraphStatus::kUnsupported;
            graph.diagnostic = "signal_pin_capacitance_unavailable:" + pin_name + ":" + std::to_string(analysis_index) + ":" + std::to_string(transition_index);
            return false;
          }
          node.input_cap_pf_by_timing.at(analysis_index).at(transition_index) = *cap;
        }
      }
      node.input_cap_pf = node.input_cap_pf_by_timing.at(1U).at(0U);
      node.input_cap_profile_available = true;
    }
    auto slew = lib_port->get_port_slew_limit(idb::AnalysisMode::kMax);
    if ((!slew.has_value() || !std::isfinite(*slew) || *slew <= 0.0) && lib_cell->get_owner_lib() != nullptr) {
      slew = lib_cell->get_owner_lib()->get_default_max_transition();
    }
    if (slew.has_value() && std::isfinite(*slew) && *slew > 0.0) {
      node.max_slew_ns = *slew;
    }
    node.slew_limit_from_master = true;
  }
  node_index.emplace(pin_name, graph.nodes.size());
  graph.nodes.push_back(std::move(node));
  return true;
}

auto buildNetWireModel(const std::unordered_map<std::string, std::size_t>& node_index, const WrapperTimingGraph& graph,
                       const WrapperSignalRoutingAuthority& routing_authority, int32_t dbu_per_um, idb::IdbPin* driver, const std::vector<idb::IdbPin*>& loads,
                       WrapperTimingNet& result) -> bool
{
  if (!routing_authority.complete() || !routing_authority.horizontal.has_value() || !routing_authority.vertical.has_value() || dbu_per_um <= 0
      || driver == nullptr || loads.empty()) {
    return false;
  }
  const auto& horizontal_layer = routing_authority.horizontal.value();
  const auto& vertical_layer = routing_authority.vertical.value();
  const auto driver_name = canonicalPinName(driver);
  const auto driver_index = node_index.find(driver_name);
  if (driver_index == node_index.end() || driver_index->second >= graph.nodes.size()) {
    return false;
  }
  const auto& driver_node = graph.nodes.at(driver_index->second);
  const auto driver_location = std::pair(driver_node.x_dbu, driver_node.y_dbu);
  std::vector<std::pair<int32_t, int32_t>> locations;
  locations.reserve(loads.size() + 1U);
  locations.push_back(driver_location);
  for (auto* load : loads) {
    const auto load_name = canonicalPinName(load);
    const auto load_index = node_index.find(load_name);
    if (load_index == node_index.end() || load_index->second >= graph.nodes.size()) {
      return false;
    }
    const auto& node = graph.nodes.at(load_index->second);
    locations.emplace_back(node.x_dbu, node.y_dbu);
  }
  std::ranges::sort(locations);
  locations.erase(std::ranges::unique(locations).begin(), locations.end());
  const auto driver_location_iter = std::ranges::find(locations, driver_location);
  if (driver_location_iter == locations.end()) {
    return false;
  }
  std::ranges::rotate(locations, driver_location_iter);

  int64_t horizontal_dbu = 0;
  int64_t vertical_dbu = 0;
  bool valid_wirelength = true;
  auto append_segment = [&](int32_t source_x, int32_t source_y, int32_t target_x, int32_t target_y) -> void {
    const auto horizontal_delta = std::abs(static_cast<int64_t>(source_x) - static_cast<int64_t>(target_x));
    const auto vertical_delta = std::abs(static_cast<int64_t>(source_y) - static_cast<int64_t>(target_y));
    if (horizontal_delta < 0 || vertical_delta < 0 || horizontal_dbu > std::numeric_limits<int64_t>::max() - horizontal_delta
        || vertical_dbu > std::numeric_limits<int64_t>::max() - vertical_delta) {
      valid_wirelength = false;
      return;
    }
    // Emit one RC element per FLUTE tree edge, keeping the tree topology intact.
    // The wire is not split into an L-shaped horizontal and vertical piece:
    // two different tree edges could then decompose onto the same node pair
    // (a shared bend point), which turns the RC graph into one with parallel
    // edges or cycles and violates the tree invariant the parasitics builder
    // validates. The electrical effect of the L-shape is preserved by costing
    // the horizontal and vertical extents on their own layers.
    const auto horizontal_um = static_cast<double>(horizontal_delta) / static_cast<double>(dbu_per_um);
    const auto vertical_um = static_cast<double>(vertical_delta) / static_cast<double>(dbu_per_um);
    const auto edge_resistance_ohm = horizontal_um * horizontal_layer.resistance_ohm_per_um + vertical_um * vertical_layer.resistance_ohm_per_um;
    const auto edge_capacitance_pf = horizontal_um * horizontal_layer.capacitance_pf_per_um + vertical_um * vertical_layer.capacitance_pf_per_um;
    if (edge_resistance_ohm > 0.0 || edge_capacitance_pf > 0.0) {
      result.rc_segments.push_back(WrapperTimingRcSegment{.begin_x_dbu = source_x,
                                                          .begin_y_dbu = source_y,
                                                          .end_x_dbu = target_x,
                                                          .end_y_dbu = target_y,
                                                          .resistance_ohm = edge_resistance_ohm,
                                                          .capacitance_pf = edge_capacitance_pf});
    }
    horizontal_dbu += horizontal_delta;
    vertical_dbu += vertical_delta;
  };
  if (locations.size() == 2U) {
    append_segment(locations.front().first, locations.front().second, locations.back().first, locations.back().second);
  } else if (locations.size() > 2U) {
    std::vector<std::shared_ptr<salt::Pin>> pins;
    pins.reserve(locations.size());
    for (std::size_t index = 0; index < locations.size(); ++index) {
      pins.push_back(std::make_shared<salt::Pin>(locations.at(index).first, locations.at(index).second, static_cast<int>(index), 0.0));
    }
    salt::Net net;
    net.init(0, result.net_name, pins);
    salt::Tree tree;
    salt::FluteBuilder builder;
    builder.Run(net, tree);
    tree.UpdateId();
    if (tree.source == nullptr) {
      return false;
    }
    bool valid = true;
    salt::TreeNode::preOrder(tree.source, [&](const std::shared_ptr<salt::TreeNode>& node) -> void {
      if (node == nullptr || node == tree.source) {
        return;
      }
      if (node->parent == nullptr) {
        valid = false;
        return;
      }
      append_segment(node->loc.x, node->loc.y, node->parent->loc.x, node->parent->loc.y);
    });
    if (!valid || !valid_wirelength) {
      return false;
    }
  }
  if (!valid_wirelength || horizontal_dbu > std::numeric_limits<int64_t>::max() - vertical_dbu) {
    return false;
  }
  const auto horizontal_um = static_cast<double>(horizontal_dbu) / static_cast<double>(dbu_per_um);
  const auto vertical_um = static_cast<double>(vertical_dbu) / static_cast<double>(dbu_per_um);
  result.total_wirelength_dbu = horizontal_dbu + vertical_dbu;
  result.wire_resistance_ohm = horizontal_um * horizontal_layer.resistance_ohm_per_um + vertical_um * vertical_layer.resistance_ohm_per_um;
  result.wire_cap_pf = horizontal_um * horizontal_layer.capacitance_pf_per_um + vertical_um * vertical_layer.capacitance_pf_per_um;
  result.physically_zero_length = result.total_wirelength_dbu == 0;
  return result.total_wirelength_dbu >= 0 && std::isfinite(result.wire_resistance_ohm) && result.wire_resistance_ohm >= 0.0 && std::isfinite(result.wire_cap_pf)
         && result.wire_cap_pf >= 0.0 && std::ranges::all_of(result.rc_segments, [](const auto& segment) -> bool {
              return std::isfinite(segment.resistance_ohm) && segment.resistance_ohm > 0.0 && std::isfinite(segment.capacitance_pf)
                     && segment.capacitance_pf > 0.0;
            });
}

auto isFiniteNonnegative(double value) -> bool
{
  return std::isfinite(value) && value >= 0.0;
}

auto makeSignalLayer(idb::IdbLayerRouting& layer, int32_t dbu_per_um) -> std::optional<WrapperSignalRoutingLayer>
{
  if ((!layer.is_horizontal() && !layer.is_vertical()) || dbu_per_um <= 0 || layer.get_width() <= 0 || !std::isfinite(layer.get_resistance())
      || layer.get_resistance() <= 0.0 || !isFiniteNonnegative(layer.get_capacitance()) || !isFiniteNonnegative(layer.get_edge_capacitance())) {
    return std::nullopt;
  }
  const double width_um = static_cast<double>(layer.get_width()) / static_cast<double>(dbu_per_um);
  const double resistance_per_um = layer.get_resistance() / width_um;
  const double capacitance_per_um = layer.get_capacitance() * width_um + 2.0 * layer.get_edge_capacitance();
  if (!std::isfinite(width_um) || width_um <= 0.0 || !std::isfinite(resistance_per_um) || resistance_per_um <= 0.0 || !std::isfinite(capacitance_per_um)
      || capacitance_per_um <= 0.0) {
    return std::nullopt;
  }
  return WrapperSignalRoutingLayer{
      .name = layer.get_name(),
      .id = static_cast<int32_t>(static_cast<unsigned char>(layer.get_id())),
      .order = static_cast<uint32_t>(layer.get_order()),
      .direction = layer.is_horizontal() ? WrapperRoutingDirection::kHorizontal : WrapperRoutingDirection::kVertical,
      .width_dbu = layer.get_width(),
      .width_um = width_um,
      .sheet_resistance_ohm_per_square = layer.get_resistance(),
      .area_capacitance_pf_per_um2 = layer.get_capacitance(),
      .edge_capacitance_pf_per_um = layer.get_edge_capacitance(),
      .resistance_ohm_per_um = resistance_per_um,
      .capacitance_pf_per_um = capacitance_per_um,
  };
}

}  // namespace

auto Wrapper::deriveSignalRoutingLayer(idb::IdbLayerRouting& layer, int32_t dbu_per_um) -> std::optional<WrapperSignalRoutingLayer>
{
  return makeSignalLayer(layer, dbu_per_um);
}

auto Wrapper::queryConfiguredSignalRoutingAuthority() const -> WrapperSignalRoutingAuthority
{
  return querySignalRoutingAuthority(dmInst->get_config().get_routing_layer_1st());
}

auto Wrapper::querySignalRoutingAuthority(std::string_view anchor_name) const -> WrapperSignalRoutingAuthority
{
  WrapperSignalRoutingAuthority result;
  result.anchor_name = std::string(anchor_name);
  if (_idb_layout == nullptr || _idb_layout->get_layers() == nullptr || _idb_layout->get_units() == nullptr) {
    result.status = WrapperSignalRoutingStatus::kLayoutUnavailable;
    result.diagnostic = "live iDB layout, layers, or units are unavailable";
    return result;
  }
  const int32_t dbu_per_um = _idb_layout->get_units()->get_micron_dbu();
  if (dbu_per_um <= 0) {
    result.status = WrapperSignalRoutingStatus::kInvalidDbu;
    result.diagnostic = "live iDB DBU-per-micron is not positive";
    return result;
  }
  if (anchor_name.empty()) {
    result.status = WrapperSignalRoutingStatus::kAnchorUnavailable;
    result.diagnostic = "platform LayerSettings.routing_layer_1st is empty";
    return result;
  }
  auto* anchor = _idb_layout->get_layers()->find_layer(std::string(anchor_name));
  if (anchor == nullptr) {
    result.status = WrapperSignalRoutingStatus::kAnchorUnavailable;
    result.diagnostic = "platform signal-routing anchor is absent from the live iDB layer table";
    return result;
  }
  if (!anchor->is_routing()) {
    result.status = WrapperSignalRoutingStatus::kAnchorNotRouting;
    result.diagnostic = "platform signal-routing anchor is not a routing layer";
    return result;
  }
  result.anchor_name = anchor->get_name();
  result.anchor_id = static_cast<int32_t>(static_cast<unsigned char>(anchor->get_id()));
  result.anchor_order = static_cast<uint32_t>(anchor->get_order());

  std::vector<idb::IdbLayerRouting*> candidates;
  for (auto* raw_layer : _idb_layout->get_layers()->get_routing_layers()) {
    auto* layer = dynamic_cast<idb::IdbLayerRouting*>(raw_layer);
    if (layer != nullptr && layer->get_order() >= anchor->get_order()) {
      candidates.push_back(layer);
    }
  }
  std::ranges::sort(candidates,
                    [](auto* lhs, auto* rhs) -> bool { return std::pair(lhs->get_order(), lhs->get_name()) < std::pair(rhs->get_order(), rhs->get_name()); });
  for (auto* layer : candidates) {
    const auto candidate = deriveSignalRoutingLayer(*layer, dbu_per_um);
    if (!candidate.has_value()) {
      continue;
    }
    if (candidate->direction == WrapperRoutingDirection::kHorizontal && !result.horizontal.has_value()) {
      result.horizontal = candidate;
    }
    if (candidate->direction == WrapperRoutingDirection::kVertical && !result.vertical.has_value()) {
      result.vertical = candidate;
    }
    if (result.horizontal.has_value() && result.vertical.has_value()) {
      result.status = WrapperSignalRoutingStatus::kComplete;
      return result;
    }
  }
  result.status = WrapperSignalRoutingStatus::kDirectionalLayerUnavailable;
  result.diagnostic = "no electrically valid horizontal/vertical routing-layer pair exists at or above the signal anchor";
  return result;
}

auto Wrapper::collectTimingGraph() const -> WrapperTimingGraph
{
  const auto graph_start = std::chrono::steady_clock::now();
  WrapperTimingGraph result;
  double logic_rc_runtime_s = 0.0;
  if (_idb_design == nullptr || _idb_design->get_net_list() == nullptr || _idb_design->get_instance_list() == nullptr) {
    result.status = WrapperTimingGraphStatus::kDesignUnavailable;
    result.diagnostic = "live iDB design, net list, or instance list is unavailable";
    return result;
  }

  std::unordered_map<std::string, std::size_t> node_index;
  const auto dbu_per_um = queryDbUnit();
  const auto routing_authority = dbu_per_um.has_value() ? queryConfiguredSignalRoutingAuthority() : WrapperSignalRoutingAuthority{};
  result.signal_routing_authority = routing_authority;
  for (auto* net : _idb_design->get_net_list()->get_net_list()) {
    if (net == nullptr || (!net->is_clock() && !net->is_signal() && net->get_connect_type() != idb::IdbConnectType::kNone)) {
      continue;
    }
    if (net->get_pin_number() == 0) {
      continue;
    }
    auto* driver = net->get_driving_pin();
    const auto driver_name = canonicalPinName(driver);
    if (driver == nullptr || driver_name.empty()) {
      result.status = WrapperTimingGraphStatus::kConnectivityInvalid;
      result.diagnostic = "signal net has no canonical driver pin: " + net->get_net_name();
      return result;
    }
    WrapperTimingNet timing_net;
    timing_net.net_name = net->get_net_name();
    timing_net.driver_pin = driver_name;
    std::vector<idb::IdbPin*> load_pins;
    auto* driver_instance = driver->get_instance();
    auto* driver_lib_cell = driver_instance == nullptr || driver_instance->get_cell_master() == nullptr
                                ? nullptr
                                : findLibertyCell(driver_instance->get_cell_master()->get_name());
    if (!appendTimingNode(driver, driver_lib_cell, result, node_index)) {
      if (result.diagnostic.empty()) {
        result.status = WrapperTimingGraphStatus::kConnectivityInvalid;
        result.diagnostic = "signal net driver pin cannot be represented: " + driver_name + ":" + net->get_net_name();
      }
      return result;
    }
    for (auto* load : net->get_load_pins()) {
      const auto load_name = canonicalPinName(load);
      if (load == nullptr || load_name.empty() || load == driver) {
        continue;
      }
      timing_net.load_pins.push_back(load_name);
      load_pins.push_back(load);
      auto* load_instance = load->get_instance();
      auto* load_lib_cell
          = load_instance == nullptr || load_instance->get_cell_master() == nullptr ? nullptr : findLibertyCell(load_instance->get_cell_master()->get_name());
      if (!appendTimingNode(load, load_lib_cell, result, node_index)) {
        if (result.diagnostic.empty()) {
          result.status = WrapperTimingGraphStatus::kConnectivityInvalid;
          result.diagnostic = "signal net load pin cannot be represented: " + load_name + ":" + net->get_net_name();
        }
        return result;
      }
    }
    std::ranges::sort(timing_net.load_pins);
    timing_net.load_pins.erase(std::ranges::unique(timing_net.load_pins).begin(), timing_net.load_pins.end());
    if (timing_net.load_pins.empty()) {
      continue;
    }
    if (!routing_authority.complete()) {
      result.status = WrapperTimingGraphStatus::kUnsupported;
      result.diagnostic = "signal routing authority unavailable:" + routing_authority.diagnostic + ":" + timing_net.net_name;
      return result;
    }
    const auto logic_rc_start = std::chrono::steady_clock::now();
    const bool logic_rc_valid = buildNetWireModel(node_index, result, routing_authority, dbu_per_um.value_or(0), driver, load_pins, timing_net);
    logic_rc_runtime_s += std::chrono::duration<double>(std::chrono::steady_clock::now() - logic_rc_start).count();
    if (!logic_rc_valid) {
      result.status = WrapperTimingGraphStatus::kUnsupported;
      result.diagnostic = "signal net FLUTE RC construction failed: " + timing_net.net_name;
      return result;
    }
    result.nets.push_back(std::move(timing_net));
  }

  for (auto* instance : _idb_design->get_instance_list()->get_instance_list()) {
    if (instance == nullptr || instance->get_cell_master() == nullptr) {
      continue;
    }
    const auto master_name = instance->get_cell_master()->get_name();
    auto* lib_cell = findLibertyCell(master_name);
    if (lib_cell == nullptr) {
      continue;
    }
    for (const auto& arc_set_holder : lib_cell->get_cell_arcs()) {
      auto* arc_set = arc_set_holder.get();
      auto* arc = firstEnabledArc(arc_set);
      if (arc == nullptr) {
        continue;
      }
      const auto source_port = std::string(arc->get_src_port());
      const auto sink_port = std::string(arc->get_snk_port());
      auto* source_pin = instance->get_pin(source_port);
      if (source_pin == nullptr) {
        source_pin = instance->get_pin_by_term(source_port);
      }
      auto* sink_pin = instance->get_pin(sink_port);
      if (sink_pin == nullptr) {
        sink_pin = instance->get_pin_by_term(sink_port);
      }
      const auto source_name = canonicalPinName(source_pin);
      const auto sink_name = canonicalPinName(sink_pin);
      const auto timing_type = arc->get_timing_type();
      bool has_positive_unate = false;
      bool has_negative_unate = false;
      for (const auto& arc_holder : arc_set->get_arcs()) {
        auto* candidate = arc_holder.get();
        if (candidate == nullptr || candidate->isDisableArc() != 0U) {
          continue;
        }
        has_positive_unate = has_positive_unate || candidate->isPositiveArc() != 0U;
        has_negative_unate = has_negative_unate || candidate->isNegativeArc() != 0U;
      }

      if (isCombinationalArc(arc)) {
        if (!node_index.contains(source_name) && !node_index.contains(sink_name)) {
          continue;
        }
        if (sink_pin != nullptr && sink_pin->get_net() == nullptr) {
          continue;
        }
        if (source_pin == nullptr || sink_pin == nullptr || source_name.empty() || sink_name.empty() || !node_index.contains(source_name)
            || !node_index.contains(sink_name)) {
          ++result.unsupported_arc_count;
          continue;
        }
        result.arcs.push_back(WrapperTimingArc{
            .inst_name = instance->get_name(),
            .cell_master = master_name,
            .input_port = source_port,
            .output_port = sink_port,
            .input_pin = source_name,
            .output_pin = sink_name,
            .positive_unate = has_positive_unate,
            .negative_unate = has_negative_unate,
            .clock_gate_boundary = lib_cell->isICG() && source_pin->get_term() != nullptr && lib_cell->get_cell_port_or_port_bus(source_port.c_str()) != nullptr
                                   && (lib_cell->get_cell_port_or_port_bus(source_port.c_str())->isClock()
                                       || lib_cell->get_cell_port_or_port_bus(source_port.c_str())->get_clock_gate_clock_pin())});
        continue;
      }

      if ((timing_type == idb::LibArc::TimingType::kRisingEdge || timing_type == idb::LibArc::TimingType::kFallingEdge) && node_index.contains(sink_name)) {
        if (source_pin == nullptr || sink_pin == nullptr || source_name.empty() || sink_name.empty()) {
          ++result.unsupported_arc_count;
          continue;
        }
        result.launches.push_back(WrapperTimingLaunch{.inst_name = instance->get_name(),
                                                      .cell_master = master_name,
                                                      .clock_port = source_port,
                                                      .output_port = sink_port,
                                                      .clock_pin = source_name,
                                                      .output_pin = sink_name,
                                                      .clock_transition = toWrapperTransition(arc)});
        continue;
      }

      if ((arc->isSetupArc() != 0U || arc->isHoldArc() != 0U) && node_index.contains(sink_name)) {
        if (source_pin == nullptr || sink_pin == nullptr || source_name.empty() || sink_name.empty()) {
          ++result.unsupported_arc_count;
          continue;
        }
        result.checks.push_back(WrapperTimingCheck{.inst_name = instance->get_name(),
                                                   .cell_master = master_name,
                                                   .clock_port = source_port,
                                                   .data_port = sink_port,
                                                   .clock_pin = source_name,
                                                   .data_pin = sink_name,
                                                   .kind = arc->isSetupArc() != 0U ? WrapperTimingCheckKind::kSetup : WrapperTimingCheckKind::kHold,
                                                   .clock_transition = toWrapperTransition(arc),
                                                   .clock_gating = lib_cell->isICG()});
      }
    }
  }

  std::ranges::sort(result.nodes, {}, &WrapperTimingNode::pin_name);
  std::ranges::sort(result.nets, {}, &WrapperTimingNet::net_name);
  std::ranges::sort(result.arcs, [](const auto& lhs, const auto& rhs) -> bool {
    return std::tie(lhs.inst_name, lhs.input_port, lhs.output_port, lhs.input_pin, lhs.output_pin, lhs.cell_master, lhs.positive_unate, lhs.negative_unate,
                    lhs.clock_gate_boundary)
           < std::tie(rhs.inst_name, rhs.input_port, rhs.output_port, rhs.input_pin, rhs.output_pin, rhs.cell_master, rhs.positive_unate, rhs.negative_unate,
                      rhs.clock_gate_boundary);
  });
  std::vector<WrapperTimingArc> merged_arcs;
  merged_arcs.reserve(result.arcs.size());
  for (auto& arc : result.arcs) {
    const bool same_edge = !merged_arcs.empty()
                           && std::tie(merged_arcs.back().inst_name, merged_arcs.back().input_port, merged_arcs.back().output_port,
                                       merged_arcs.back().input_pin, merged_arcs.back().output_pin, merged_arcs.back().cell_master)
                                  == std::tie(arc.inst_name, arc.input_port, arc.output_port, arc.input_pin, arc.output_pin, arc.cell_master);
    if (!same_edge) {
      merged_arcs.push_back(std::move(arc));
      continue;
    }
    merged_arcs.back().positive_unate = merged_arcs.back().positive_unate || arc.positive_unate;
    merged_arcs.back().negative_unate = merged_arcs.back().negative_unate || arc.negative_unate;
    merged_arcs.back().clock_gate_boundary = merged_arcs.back().clock_gate_boundary || arc.clock_gate_boundary;
  }
  result.arcs = std::move(merged_arcs);
  std::ranges::sort(result.launches, [](const auto& lhs, const auto& rhs) -> bool {
    return std::tie(lhs.clock_pin, lhs.output_pin, lhs.cell_master, lhs.clock_port, lhs.output_port, lhs.clock_transition)
           < std::tie(rhs.clock_pin, rhs.output_pin, rhs.cell_master, rhs.clock_port, rhs.output_port, rhs.clock_transition);
  });
  result.launches.erase(std::ranges::unique(result.launches, {},
                                            [](const auto& launch) -> auto { return std::tie(launch.clock_pin, launch.output_pin, launch.clock_transition); })
                            .begin(),
                        result.launches.end());
  std::ranges::sort(result.checks, [](const auto& lhs, const auto& rhs) -> bool {
    return std::tie(lhs.clock_pin, lhs.data_pin, lhs.kind, lhs.clock_transition, lhs.cell_master, lhs.clock_port, lhs.data_port)
           < std::tie(rhs.clock_pin, rhs.data_pin, rhs.kind, rhs.clock_transition, rhs.cell_master, rhs.clock_port, rhs.data_port);
  });
  result.checks.erase(
      std::ranges::unique(result.checks, {},
                          [](const auto& check) -> auto { return std::tie(check.clock_pin, check.data_pin, check.kind, check.clock_transition); })
          .begin(),
      result.checks.end());
  if (result.unsupported_arc_count > 0U) {
    result.status = WrapperTimingGraphStatus::kUnsupported;
    result.diagnostic = "one or more combinational Liberty arcs cannot be joined to live instance pins";
    return result;
  }
  result.status = WrapperTimingGraphStatus::kComplete;
  const auto total_runtime_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - graph_start).count();
  result.logic_rc_construction_runtime_s = logic_rc_runtime_s;
  result.graph_construction_runtime_s = std::max(0.0, total_runtime_s - logic_rc_runtime_s);
  return result;
}

}  // namespace icts
