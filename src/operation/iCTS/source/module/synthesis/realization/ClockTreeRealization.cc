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
 * @file ClockTreeRealization.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-26
 * @brief Clock-tree realization helper implementation.
 */

#include "synthesis/realization/ClockTreeRealization.hh"

#include <algorithm>
#include <cctype>
#include <compare>
#include <cstddef>
#include <limits>
#include <memory>
#include <optional>
#include <ostream>
#include <ranges>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Logger.hh"
#include "design/Clock.hh"
#include "design/ClockLayout.hh"
#include "design/Design.hh"
#include "design/Inst.hh"
#include "design/Net.hh"
#include "design/Pin.hh"
#include "io/Wrapper.hh"
#include "spatial/Point.hh"

namespace icts {
namespace {

auto makeSafeNameToken(const std::string& value, const std::string& default_token) -> std::string
{
  std::string token;
  token.reserve(value.size());
  for (const auto character : value) {
    const auto uch = static_cast<unsigned char>(character);
    if (std::isalnum(uch) != 0) {
      token.push_back(static_cast<char>(character));
    } else {
      token.push_back('_');
    }
  }
  const auto duplicate_underscores = std::ranges::unique(token, [](char lhs, char rhs) -> bool { return lhs == '_' && rhs == '_'; });
  token.erase(duplicate_underscores.begin(), duplicate_underscores.end());
  while (!token.empty() && token.front() == '_') {
    token.erase(token.begin());
  }
  while (!token.empty() && token.back() == '_') {
    token.pop_back();
  }
  return token.empty() ? default_token : token;
}

auto makeClockPrefix(const Clock& clock, std::size_t clock_index) -> std::string
{
  return "cts_flow_clk_" + std::to_string(clock_index) + "_" + makeSafeNameToken(clock.get_clock_name(), "clock");
}

auto makeSinkDomainName(SinkDomainKind sink_domain) -> std::string
{
  switch (sink_domain) {
    case SinkDomainKind::kHardMacro:
      return "hard_macro";
    case SinkDomainKind::kRegular:
      return "regular";
    case SinkDomainKind::kSourceToRoot:
      return "source_to_root";
    case SinkDomainKind::kUnknown:
      return "unknown";
  }
  return "unknown";
}

auto resolveBufferDriveCap(Wrapper& wrapper, const std::string& cell_master) -> std::optional<double>
{
  auto drive_cap_pf = wrapper.queryCellOutPinCapLimit(cell_master);
  if (!drive_cap_pf.has_value()) {
    drive_cap_pf = wrapper.queryCellOutPinCapTableAxisMax(cell_master);
  }
  return drive_cap_pf;
}

auto resolveBufferPortsAndDrive(Wrapper& wrapper, const std::string& cell_master, bool require_output_drive, std::string& input_pin_name,
                                std::string& output_pin_name, double& output_drive_cap_pf) -> bool
{
  input_pin_name.clear();
  output_pin_name.clear();
  output_drive_cap_pf = 0.0;
  if (cell_master.empty()) {
    return false;
  }

  const auto ports = wrapper.queryBufferPorts(cell_master);
  if (!ports.has_value()) {
    CTSLOG.warn(Loc::current(), "ClockTreeRealization: skip buffer master \"", cell_master, "\" because buffer ports are unresolved.");
    return false;
  }

  if (require_output_drive) {
    const auto drive_cap_pf = resolveBufferDriveCap(wrapper, cell_master);
    if (!drive_cap_pf.has_value()) {
      CTSLOG.warn(Loc::current(), "ClockTreeRealization: skip buffer master \"", cell_master, "\" because output drive cap is unresolved.");
      return false;
    }
    output_drive_cap_pf = *drive_cap_pf;
  }

  input_pin_name = ports->input;
  output_pin_name = ports->output;
  return true;
}

auto resolveMinimumDriveRootBuffer(Wrapper& wrapper, const std::vector<std::string>& buffer_types, std::string& cell_master, std::string& input_pin_name,
                                   std::string& output_pin_name) -> bool
{
  cell_master.clear();
  input_pin_name.clear();
  output_pin_name.clear();

  if (buffer_types.empty()) {
    CTSLOG.warn(Loc::current(), "ClockTreeRealization: no configured buffer types are available for root-buffer insertion.");
    return false;
  }

  std::string best_cell_master;
  std::string best_input_pin;
  std::string best_output_pin;
  double best_drive_cap_pf = std::numeric_limits<double>::infinity();
  for (const auto& candidate_cell_master : buffer_types) {
    std::string candidate_input_pin;
    std::string candidate_output_pin;
    double candidate_drive_cap_pf = 0.0;
    if (!resolveBufferPortsAndDrive(wrapper, candidate_cell_master, true, candidate_input_pin, candidate_output_pin, candidate_drive_cap_pf)) {
      continue;
    }

    if (best_cell_master.empty() || candidate_drive_cap_pf < best_drive_cap_pf
        || (candidate_drive_cap_pf == best_drive_cap_pf && candidate_cell_master < best_cell_master)) {
      best_cell_master = candidate_cell_master;
      best_input_pin = std::move(candidate_input_pin);
      best_output_pin = std::move(candidate_output_pin);
      best_drive_cap_pf = candidate_drive_cap_pf;
    }
  }

  if (best_cell_master.empty()) {
    CTSLOG.warn(Loc::current(), "ClockTreeRealization: failed to resolve a minimum-drive root buffer from configured buffer types.");
    return false;
  }

  cell_master = std::move(best_cell_master);
  input_pin_name = std::move(best_input_pin);
  output_pin_name = std::move(best_output_pin);
  return true;
}

auto resolveSinkDomainLocation(Pin* clock_source, const std::vector<Pin*>& sinks) -> Point<int>
{
  long long sum_x = 0;
  long long sum_y = 0;
  std::size_t count = 0U;
  for (const auto* sink : sinks) {
    if (sink == nullptr) {
      continue;
    }
    const auto location = sink->get_location();
    if (location.get_x() < 0 || location.get_y() < 0) {
      continue;
    }
    sum_x += location.get_x();
    sum_y += location.get_y();
    ++count;
  }

  if (count > 0U) {
    return Point<int>(static_cast<int>(sum_x / static_cast<long long>(count)), static_cast<int>(sum_y / static_cast<long long>(count)));
  }
  if (clock_source != nullptr) {
    return clock_source->get_location();
  }
  return Point<int>(0, 0);
}

auto createInsertedBuffer(Design& design, Clock& clock, const std::string& inst_name, const std::string& cell_master, const std::string& input_pin_name,
                          const std::string& output_pin_name, const Point<int>& location, Inst*& buffer, Pin*& input_pin, Pin*& output_pin) -> bool
{
  buffer = nullptr;
  input_pin = nullptr;
  output_pin = nullptr;
  if (inst_name.empty() || cell_master.empty() || input_pin_name.empty() || output_pin_name.empty()) {
    CTSLOG.warn(Loc::current(), "ClockTreeRealization: reject root-buffer insertion because the inst, master, or pin name is empty.");
    return false;
  }
  if (input_pin_name == output_pin_name) {
    CTSLOG.warn(Loc::current(), "ClockTreeRealization: reject root-buffer insertion for \"", inst_name,
                "\" because input and output pin names both resolve to \"", input_pin_name, "\".");
    return false;
  }
  if (design.findInst(inst_name) != nullptr) {
    CTSLOG.warn(Loc::current(), "ClockTreeRealization: reject root-buffer insertion because inst \"", inst_name, "\" already exists.");
    return false;
  }

  buffer = design.makeInst(inst_name);
  if (buffer == nullptr) {
    CTSLOG.warn(Loc::current(), "ClockTreeRealization: failed to create root-buffer inst \"", inst_name, "\".");
    return false;
  }
  buffer->set_name(inst_name);
  buffer->set_cell_master(cell_master);
  buffer->set_type(InstType::kBuffer);
  buffer->set_location(location);
  buffer->set_pins({});

  input_pin = design.makePin(input_pin_name);
  if (input_pin == nullptr) {
    CTSLOG.warn(Loc::current(), "ClockTreeRealization: failed to create root-buffer input pin \"", input_pin_name, "\".");
    return false;
  }
  input_pin->set_name(input_pin_name);
  input_pin->set_type(PinType::kIn);
  input_pin->set_location(location);
  input_pin->set_inst(buffer);
  input_pin->set_net(nullptr);
  input_pin->set_io(false);
  buffer->add_pin(input_pin);
  if (!design.indexPin(input_pin)) {
    return false;
  }

  output_pin = design.makePin(output_pin_name);
  if (output_pin == nullptr) {
    CTSLOG.warn(Loc::current(), "ClockTreeRealization: failed to create root-buffer output pin \"", output_pin_name, "\".");
    return false;
  }
  output_pin->set_name(output_pin_name);
  output_pin->set_type(PinType::kOut);
  output_pin->set_location(location);
  output_pin->set_inst(buffer);
  output_pin->set_net(nullptr);
  output_pin->set_io(false);
  buffer->insertDriverPin(output_pin);
  if (!design.indexPin(output_pin)) {
    return false;
  }

  clock.add_inst(buffer);
  return true;
}

auto createInsertedNet(Design& design, Clock& clock, const std::string& net_name, Pin* driver, const std::vector<Pin*>& loads) -> Net*
{
  if (net_name.empty()) {
    CTSLOG.warn(Loc::current(), "ClockTreeRealization: reject inserted net creation because the net name is empty.");
    return nullptr;
  }
  if (design.findNet(net_name) != nullptr) {
    CTSLOG.warn(Loc::current(), "ClockTreeRealization: reject inserted net creation because net \"", net_name, "\" already exists.");
    return nullptr;
  }

  auto* net_ptr = design.makeNet(net_name);
  if (net_ptr == nullptr) {
    CTSLOG.warn(Loc::current(), "ClockTreeRealization: failed to create inserted net \"", net_name, "\".");
    return nullptr;
  }
  ClockTreeRealization::reconnectNet(NetConnectionInput{
      .net = net_ptr,
      .driver = driver,
      .loads = loads,
  });
  clock.add_net(net_ptr);
  return net_ptr;
}

}  // namespace

auto ClockTreeRealization::partitionClockSinks(const std::vector<Pin*>& sinks) -> ClockSinkPartitionOutput
{
  ClockSinkPartitionOutput output;
  output.macro_sinks.reserve(sinks.size());
  output.regular_sinks.reserve(sinks.size());

  for (auto* sink : sinks) {
    if (sink == nullptr) {
      continue;
    }

    const auto* inst = sink->get_inst();
    if (inst != nullptr && inst->is_macro_block()) {
      output.macro_sinks.push_back(sink);
    } else {
      output.regular_sinks.push_back(sink);
    }
  }
  return output;
}

auto ClockTreeRealization::makeSinkDomainPrefix(const Clock& clock, std::size_t clock_index, SinkDomainKind sink_domain) -> std::string
{
  return makeClockPrefix(clock, clock_index) + "_" + makeSinkDomainName(sink_domain);
}

auto ClockTreeRealization::addRootBufferForSinkDomain(const SinkDomainRootBufferSelectionInput& input) -> SinkDomainRootBufferOutput
{
  if (input.design == nullptr) {
    CTSLOG.error(Loc::current(), "ClockTreeRealization: root-buffer insertion design is null.");
  }
  if (input.clock == nullptr) {
    CTSLOG.error(Loc::current(), "ClockTreeRealization: root-buffer insertion clock is null.");
  }
  if (input.wrapper == nullptr) {
    CTSLOG.error(Loc::current(), "ClockTreeRealization: root-buffer insertion Wrapper is null.");
  }

  std::string cell_master;
  std::string input_pin_name;
  std::string output_pin_name;
  if (!resolveMinimumDriveRootBuffer(*input.wrapper, input.buffer_types, cell_master, input_pin_name, output_pin_name)) {
    return SinkDomainRootBufferOutput{};
  }
  return addRootBufferForSinkDomain(SinkDomainRootBufferInput{
      .design = input.design,
      .clock = input.clock,
      .domain_prefix = input.domain_prefix,
      .sinks = input.sinks,
      .cell_master = cell_master,
      .input_pin_name = input_pin_name,
      .output_pin_name = output_pin_name,
  });
}

auto ClockTreeRealization::addRootBufferForSinkDomain(const SinkDomainRootBufferInput& input) -> SinkDomainRootBufferOutput
{
  if (input.design == nullptr) {
    CTSLOG.error(Loc::current(), "ClockTreeRealization: root-buffer insertion design is null.");
  }
  if (input.clock == nullptr) {
    CTSLOG.error(Loc::current(), "ClockTreeRealization: root-buffer insertion clock is null.");
  }

  SinkDomainRootBufferOutput output;
  if (!createInsertedBuffer(*input.design, *input.clock, input.domain_prefix + "_root_buf", input.cell_master, input.input_pin_name, input.output_pin_name,
                            resolveSinkDomainLocation(input.clock->get_clock_source(), input.sinks), output.root_buffer, output.root_input,
                            output.root_output)) {
    return SinkDomainRootBufferOutput{};
  }
  return output;
}

auto ClockTreeRealization::reconnectNet(const NetConnectionInput& input) -> void
{
  if (input.net == nullptr) {
    CTSLOG.error(Loc::current(), "ClockTreeRealization: net reconnection target is null.");
  }
  auto& net = *input.net;
  auto* old_driver = net.get_driver();
  if (old_driver != nullptr && old_driver != input.driver && old_driver->get_net() == &net) {
    old_driver->set_net(nullptr);
  }

  const auto old_loads = net.get_loads();
  for (auto* old_load : old_loads) {
    if (old_load == nullptr || old_load->get_net() != &net) {
      continue;
    }
    if (std::ranges::find(input.loads, old_load) == input.loads.end()) {
      old_load->set_net(nullptr);
    }
  }

  net.set_driver(input.driver);
  if (input.driver != nullptr) {
    input.driver->set_net(&net);
  }

  net.set_loads({});
  for (auto* load : input.loads) {
    if (load == nullptr) {
      continue;
    }
    net.add_load(load);
    load->set_net(&net);
  }
}

auto ClockTreeRealization::connectSinkDomainDownstreamNet(const SinkDomainDownstreamNetInput& input) -> Net*
{
  if (input.design == nullptr) {
    CTSLOG.error(Loc::current(), "ClockTreeRealization: downstream-net connection design is null.");
  }
  if (input.clock == nullptr) {
    CTSLOG.error(Loc::current(), "ClockTreeRealization: downstream-net connection clock is null.");
  }
  return createInsertedNet(*input.design, *input.clock, input.domain_prefix + "_downstream_net", input.root_output, input.sinks);
}

auto ClockTreeRealization::restoreClockSourceNetToClockLoads(Clock& clock) -> void
{
  auto* clock_source = clock.get_clock_source();
  auto* clock_source_net = clock.get_clock_source_net();
  if (clock_source_net == nullptr && clock_source != nullptr) {
    clock_source_net = clock_source->get_net();
    clock.set_clock_source_net(clock_source_net);
  }
  if (clock_source_net != nullptr) {
    reconnectNet(NetConnectionInput{
        .net = clock_source_net,
        .driver = clock_source,
        .loads = clock.get_loads(),
    });
  }
}

auto ClockTreeRealization::reuseClockSourceNetAsSourceToRootBuffers(const SourceToRootNetReuseInput& input) -> Net*
{
  if (input.clock == nullptr) {
    CTSLOG.error(Loc::current(), "ClockTreeRealization: source-to-root net reuse clock is null.");
  }
  auto& clock = *input.clock;
  auto* clock_source = input.clock_source;
  auto* clock_source_net = clock.get_clock_source_net();
  if (clock_source_net == nullptr && clock_source != nullptr) {
    clock_source_net = clock_source->get_net();
    clock.set_clock_source_net(clock_source_net);
  }
  if (clock_source_net != nullptr) {
    reconnectNet(NetConnectionInput{
        .net = clock_source_net,
        .driver = clock_source,
        .loads = input.root_buffer_inputs,
    });
  }
  return clock_source_net;
}

auto ClockTreeRealization::commitInsertedObjects(const InsertedObjectCommitInput& input) -> bool
{
  if (input.design == nullptr) {
    CTSLOG.error(Loc::current(), "ClockTreeRealization: inserted-object commit design is null.");
  }
  if (input.clock == nullptr) {
    CTSLOG.error(Loc::current(), "ClockTreeRealization: inserted-object commit clock is null.");
  }
  if (input.inserted_insts == nullptr) {
    CTSLOG.error(Loc::current(), "ClockTreeRealization: inserted-object commit inst payload is null.");
  }
  if (input.inserted_pins == nullptr) {
    CTSLOG.error(Loc::current(), "ClockTreeRealization: inserted-object commit pin payload is null.");
  }
  if (input.inserted_nets == nullptr) {
    CTSLOG.error(Loc::current(), "ClockTreeRealization: inserted-object commit net payload is null.");
  }
  auto& design = *input.design;
  auto& clock = *input.clock;
  auto& inserted_insts = *input.inserted_insts;
  auto& inserted_pins = *input.inserted_pins;
  auto& inserted_nets = *input.inserted_nets;

  std::unordered_set<std::string> inst_names;
  for (const auto& inst : inserted_insts) {
    if (inst == nullptr) {
      continue;
    }
    if (!inst_names.insert(inst->get_name()).second) {
      CTSLOG.warn(Loc::current(), "ClockTreeRealization: reject committing duplicate algorithm inst \"", inst->get_name(), "\".");
      return false;
    }
    if (design.findInst(inst->get_name()) != nullptr) {
      CTSLOG.warn(Loc::current(), "ClockTreeRealization: reject committing algorithm inst \"", inst->get_name(),
                  "\" because a final inst with the same name already exists.");
      return false;
    }
  }

  std::unordered_set<std::string> pin_full_names;
  for (const auto& pin : inserted_pins) {
    if (pin == nullptr) {
      continue;
    }
    const auto pin_full_name = Design::getPinFullName(pin.get());
    if (pin_full_name.empty()) {
      CTSLOG.warn(Loc::current(), "ClockTreeRealization: reject committing algorithm pin because its full name is empty.");
      return false;
    }
    if (!pin_full_names.insert(pin_full_name).second) {
      CTSLOG.warn(Loc::current(), "ClockTreeRealization: reject committing duplicate algorithm pin \"", pin_full_name, "\".");
      return false;
    }
    auto* existing_pin = design.findPin(pin_full_name);
    if (existing_pin != nullptr && existing_pin != pin.get()) {
      CTSLOG.warn(Loc::current(), "ClockTreeRealization: reject committing algorithm pin \"", pin_full_name,
                  "\" because a final pin with the same full name already exists.");
      return false;
    }
  }

  std::unordered_set<std::string> net_names;
  for (const auto& net : inserted_nets) {
    if (net == nullptr) {
      continue;
    }
    if (!net_names.insert(net->get_name()).second) {
      CTSLOG.warn(Loc::current(), "ClockTreeRealization: reject committing duplicate algorithm net \"", net->get_name(), "\".");
      return false;
    }
    if (design.findNet(net->get_name()) != nullptr) {
      CTSLOG.warn(Loc::current(), "ClockTreeRealization: reject committing algorithm net \"", net->get_name(),
                  "\" because a final net with the same name already exists.");
      return false;
    }
  }

  for (auto& inst : inserted_insts) {
    if (inst == nullptr) {
      continue;
    }
    auto* committed_inst = design.commitInst(std::move(inst));
    if (committed_inst == nullptr) {
      CTSLOG.warn(Loc::current(), "ClockTreeRealization: failed to commit algorithm inst.");
      return false;
    }
    clock.add_inst(committed_inst);
  }
  inserted_insts.clear();

  for (auto& pin : inserted_pins) {
    if (pin == nullptr) {
      continue;
    }
    if (design.commitPin(std::move(pin)) == nullptr) {
      CTSLOG.warn(Loc::current(), "ClockTreeRealization: failed to commit algorithm pin.");
      return false;
    }
  }
  inserted_pins.clear();

  for (auto& net : inserted_nets) {
    if (net == nullptr) {
      continue;
    }
    auto* committed_net = design.commitNet(std::move(net));
    if (committed_net == nullptr) {
      CTSLOG.warn(Loc::current(), "ClockTreeRealization: failed to commit algorithm net.");
      return false;
    }
    clock.add_net(committed_net);
  }
  inserted_nets.clear();
  return true;
}

}  // namespace icts
