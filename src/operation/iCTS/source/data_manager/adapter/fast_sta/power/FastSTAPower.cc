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
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
/**
 * @file FastSTAPower.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-05-18
 * @brief CTS fast STA power calculation implementation.
 */

#include "FastSTAPower.hh"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "FastSTA.hh"
#include "FastSTAClockState.hh"
#include "FastSTALibertyModel.hh"
#include "clock_sizing/FastSTAClockSizingEdit.hh"
#include "io/Wrapper.hh"
#include "timing/FastSTAClockTiming.hh"
#include "timing/FastSTAConstraints.hh"

namespace icts {
namespace {

auto lookupPowerTable(const std::vector<FastStaLibertyTable>& tables, double input_slew_ns, double output_load_pf) -> std::optional<double>
{
  for (const auto& table : tables) {
    if (!table.valid()) {
      continue;
    }
    const auto value = table.lookup(input_slew_ns, output_load_pf);
    if (!value.has_value() || !std::isfinite(*value) || *value < 0.0) {
      return std::nullopt;
    }
    return value;
  }
  return std::nullopt;
}

auto clockActivityDensity(double clock_period_ns) -> std::optional<double>
{
  return std::isfinite(clock_period_ns) && clock_period_ns > 0.0 ? std::optional<double>{2.0 / clock_period_ns} : std::nullopt;
}

auto nodeActivityDensity(const FastStaContext& context, const FastStaNode& node) -> std::optional<double>
{
  if (node.domain != FastStaNodeDomain::kClock || node.clock_inactive) {
    return 0.0;
  }
  return clockActivityDensity(FastStaConstraints::period(context, node.clock_name));
}

auto resolveVoltage(const FastStaContext& context) -> std::optional<double>
{
  if (context.wrapper != nullptr) {
    // A source port can drive only sequential clock pins: no buffer model is
    // required, but its net switching power still uses the actual library supply.
    return context.wrapper->querySupplyVoltage();
  }
  std::optional<double> voltage;
  for (const auto& [_, cell] : context.liberty_cell_by_master) {
    if (std::isfinite(cell.voltage_v) && cell.voltage_v > 0.0) {
      if (voltage.has_value() && *voltage != cell.voltage_v) {
        return std::nullopt;
      }
      voltage = cell.voltage_v;
    }
  }
  return voltage;
}

auto findBufferInputNode(const FastStaContext& context, const FastStaNode& output_node) -> FastStaNodeId
{
  if (const auto indexed = context.buffer_input_node_id_by_inst.find(output_node.inst_name); indexed != context.buffer_input_node_id_by_inst.end()) {
    if (indexed->second < context.nodes.size()) {
      const auto& input_node = context.nodes.at(indexed->second);
      if (input_node.kind == FastStaNodeKind::kBufferInput && input_node.inst_name == output_node.inst_name) {
        return indexed->second;
      }
    }
  }
  return kInvalidFastStaNodeId;
}

auto calcNetSwitchingPowerW(const FastStaNet& net, double voltage, double activity_density) -> double
{
  return 0.5 * std::max(0.0, net.load_cap_pf) * 1e-12 * voltage * voltage * activity_density * 1e9;
}

auto calcBufferPower(FastStaContext& context, FastStaNodeId output_node_id, double activity_density) -> bool
{
  if (output_node_id >= context.nodes.size()) {
    return false;
  }
  auto& node = context.nodes.at(output_node_id);
  node.area_um2 = 0.0;
  node.leakage_power_w = 0.0;
  node.internal_power_w = 0.0;
  if (node.kind != FastStaNodeKind::kBufferOutput || node.domain != FastStaNodeDomain::kClock) {
    return true;
  }
  const auto cell_iter = context.liberty_cell_by_master.find(node.cell_master);
  if (cell_iter == context.liberty_cell_by_master.end()) {
    return false;
  }
  const auto& cell = cell_iter->second;
  if (!std::isfinite(cell.area_um2) || !cell.leakage_power_w.has_value() || !std::isfinite(*cell.leakage_power_w) || *cell.leakage_power_w < 0.0) {
    return false;
  }
  node.area_um2 = std::max(0.0, cell.area_um2);
  node.leakage_power_w = *cell.leakage_power_w;
  if (node.clock_inactive) {
    // A statically closed gate retains its cell leakage/area but no toggling
    // output or characterized clock-arc internal energy.
    return true;
  }
  if (node.output_net_ids.empty() || node.output_net_ids.front() >= context.nets.size()) {
    return false;
  }
  const auto input_node_id = findBufferInputNode(context, node);
  if (input_node_id >= context.nodes.size() || !context.nodes.at(input_node_id).timing.valid) {
    return false;
  }
  const auto& net = context.nets.at(node.output_net_ids.front());
  const auto internal_energy_mw_ns = lookupPowerTable(cell.timing_arc.internal_power_tables, context.nodes.at(input_node_id).timing.slew_ns, net.load_cap_pf);
  if (!internal_energy_mw_ns.has_value()) {
    return false;
  }
  node.internal_power_w = *internal_energy_mw_ns * 1e-12 * activity_density * 1e9;
  return std::isfinite(node.internal_power_w);
}

auto sumPower(const FastStaContext& context) -> FastStaPowerSummary
{
  FastStaPowerSummary power;
  for (const auto& net : context.nets) {
    if (net.domain == FastStaNetDomain::kClock) {
      power.switching_power_w += net.switching_power_w;
    }
  }
  std::unordered_set<std::string> seen_buffer_insts;
  seen_buffer_insts.reserve(context.owner_clock_scope_available ? context.owned_clock_node_ids.size() : context.nodes.size());
  const auto add_buffer_power = [&](const FastStaNode& node) -> void {
    if (node.kind != FastStaNodeKind::kBufferOutput || node.domain != FastStaNodeDomain::kClock || node.inst_name.empty()
        || seen_buffer_insts.contains(node.inst_name)) {
      return;
    }
    seen_buffer_insts.insert(node.inst_name);
    power.area_um2 += node.area_um2;
    power.leakage_power_w += node.leakage_power_w;
    power.internal_power_w += node.internal_power_w;
    const auto model = context.liberty_cell_by_master.find(node.cell_master);
    if (model != context.liberty_cell_by_master.end() && model->second.clock_gate.has_value()
        && FastStaConstraints::gateActive(context, node.inst_name, *model->second.clock_gate) == FastStaLogicValue::kUnknown) {
      ++power.unknown_gate_activity_count;
    }
  };
  if (context.owner_clock_scope_available) {
    for (const auto node_id : context.owned_clock_node_ids) {
      if (node_id < context.nodes.size()) {
        add_buffer_power(context.nodes.at(node_id));
      }
    }
  } else {
    for (const auto& node : context.nodes) {
      add_buffer_power(node);
    }
  }
  power.total_power_w = power.switching_power_w + power.internal_power_w + power.leakage_power_w;
  return power;
}

}  // namespace

auto FastStaPower::update(FastStaContext& context) -> bool
{
  context.power = {};
  context.power_valid = false;
  if (!context.timing_valid && !context.clock_timing_valid) {
    return false;
  }
  const auto voltage = resolveVoltage(context);
  if (!voltage.has_value()) {
    return false;
  }
  for (auto& net : context.nets) {
    net.switching_power_w = 0.0;
    if (net.domain != FastStaNetDomain::kClock) {
      continue;
    }
    const auto activity_density = nodeActivityDensity(context, context.nodes.at(net.driver_node_id));
    if (!activity_density.has_value()) {
      return false;
    }
    net.switching_power_w = calcNetSwitchingPowerW(net, *voltage, *activity_density);
  }
  for (FastStaNodeId node_id = 0U; node_id < context.nodes.size(); ++node_id) {
    const auto activity_density = nodeActivityDensity(context, context.nodes.at(node_id));
    if (!activity_density.has_value() || !calcBufferPower(context, node_id, *activity_density)) {
      context.power = {};
      return false;
    }
  }
  context.power = sumPower(context);
  context.power_valid = true;
  return true;
}

auto FastStaPower::updateRegion(FastStaContext& context, const FastStaDirtyRegion& dirty_region) -> bool
{
  if (!dirty_region.valid) {
    return false;
  }
  if (!context.power_valid) {
    return update(context);
  }
  return updatePreparedRegion(context, dirty_region);
}

auto FastStaPower::updatePreparedRegion(FastStaContext& context, const FastStaDirtyRegion& dirty_region) -> bool
{
  if (!context.timing_valid && !context.clock_timing_valid) {
    context.power_valid = false;
    return false;
  }
  const auto voltage = resolveVoltage(context);
  if (!voltage.has_value()) {
    context.power = {};
    context.power_valid = false;
    return false;
  }
  for (const auto net_id : dirty_region.net_ids) {
    if (net_id < context.nets.size()) {
      auto& net = context.nets.at(net_id);
      const auto activity_density = nodeActivityDensity(context, context.nodes.at(net.driver_node_id));
      if (!activity_density.has_value()) {
        context.power_valid = false;
        return false;
      }
      net.switching_power_w = net.domain == FastStaNetDomain::kClock ? calcNetSwitchingPowerW(net, *voltage, *activity_density) : 0.0;
    }
  }
  for (const auto node_id : dirty_region.node_ids) {
    if (node_id < context.nodes.size() && context.nodes.at(node_id).kind == FastStaNodeKind::kBufferOutput) {
      const auto activity_density = nodeActivityDensity(context, context.nodes.at(node_id));
      if (!activity_density.has_value() || !calcBufferPower(context, node_id, *activity_density)) {
        context.power = {};
        context.power_valid = false;
        return false;
      }
    }
  }
  context.power = sumPower(context);
  context.power_valid = true;
  return true;
}

}  // namespace icts
