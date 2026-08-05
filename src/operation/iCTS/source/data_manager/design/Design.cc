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
 * @file Design.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-16
 * @brief Canonical iCTS design ownership and indexing.
 */

#include "data_manager/design/Design.hh"

#include <algorithm>
#include <cstddef>
#include <ostream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Logger.hh"
#include "Point.hh"
#include "data_manager/design/Clock.hh"
#include "data_manager/design/ClockDAG.hh"
#include "data_manager/design/Inst.hh"
#include "data_manager/design/Pin.hh"

namespace icts {
namespace {

template <typename T>
auto collectBorrowedPointers(const std::vector<std::unique_ptr<T>>& objects) -> std::vector<T*>
{
  std::vector<T*> borrowed;
  borrowed.reserve(objects.size());
  for (const auto& object : objects) {
    if (object != nullptr) {
      borrowed.push_back(object.get());
    }
  }
  return borrowed;
}

template <typename ObjectT>
auto eraseNameIndexEntry(std::unordered_map<std::string, ObjectT*>& object_by_name, const std::string& name, const ObjectT* object) -> void
{
  if (name.empty()) {
    return;
  }

  const auto object_iter = object_by_name.find(name);
  if (object_iter != object_by_name.end() && object_iter->second == object) {
    object_by_name.erase(object_iter);
  }
}

template <typename ObjectT>
auto eraseRecordedNameIndexEntry(std::unordered_map<std::string, ObjectT*>& object_by_name, std::unordered_map<const ObjectT*, std::string>& name_by_object,
                                 const ObjectT* object, const std::string& current_name) -> void
{
  const auto name_iter = name_by_object.find(object);
  if (name_iter != name_by_object.end()) {
    eraseNameIndexEntry(object_by_name, name_iter->second, object);
    if (name_iter->second != current_name) {
      eraseNameIndexEntry(object_by_name, current_name, object);
    }
    name_by_object.erase(name_iter);
    return;
  }

  eraseNameIndexEntry(object_by_name, current_name, object);
}

}  // namespace

Design::Design() = default;

Design::~Design() = default;

auto Design::reset() -> void
{
  clearClocks();
  clearTopologyObjects();
}

auto Design::clone() const -> std::unique_ptr<Design>
{
  auto cloned = std::make_unique<Design>();
  std::unordered_map<const Inst*, Inst*> inst_map;
  std::unordered_map<const Pin*, Pin*> pin_map;
  std::unordered_map<const Net*, Net*> net_map;

  for (const auto* inst : get_insts()) {
    if (inst == nullptr) {
      continue;
    }
    auto* cloned_inst = cloned->makeInst(inst->get_name());
    cloned_inst->set_cell_master(inst->get_cell_master());
    cloned_inst->set_type(inst->get_type());
    cloned_inst->set_location(inst->get_location());
    inst_map.emplace(inst, cloned_inst);
  }

  for (const auto* pin : get_pins()) {
    if (pin == nullptr) {
      continue;
    }
    auto* cloned_pin = cloned->makePin(pin->get_name());
    cloned_pin->set_type(pin->get_type());
    cloned_pin->set_location(pin->get_location());
    cloned_pin->set_io(pin->is_io());
    if (const auto inst_iter = inst_map.find(pin->get_inst()); inst_iter != inst_map.end()) {
      cloned_pin->set_inst(inst_iter->second);
    }
    if (!cloned->indexPin(cloned_pin)) {
      CTSLOG.error(Loc::current(), "Design: failed to clone pin \"", getPinFullName(pin), "\".");
    }
    pin_map.emplace(pin, cloned_pin);
  }

  for (const auto* inst : get_insts()) {
    if (inst == nullptr) {
      continue;
    }
    std::vector<Pin*> cloned_pins;
    cloned_pins.reserve(inst->get_pins().size());
    for (const auto* pin : inst->get_pins()) {
      if (const auto pin_iter = pin_map.find(pin); pin_iter != pin_map.end()) {
        cloned_pins.push_back(pin_iter->second);
      }
    }
    inst_map.at(inst)->set_pins(cloned_pins);
  }

  for (const auto* net : get_nets()) {
    if (net == nullptr) {
      continue;
    }
    auto* cloned_net = cloned->makeNet(net->get_name());
    if (const auto driver_iter = pin_map.find(net->get_driver()); driver_iter != pin_map.end()) {
      cloned_net->set_driver(driver_iter->second);
      driver_iter->second->set_net(cloned_net);
    }
    std::vector<Pin*> cloned_loads;
    cloned_loads.reserve(net->get_loads().size());
    for (const auto* load : net->get_loads()) {
      if (const auto load_iter = pin_map.find(load); load_iter != pin_map.end()) {
        cloned_loads.push_back(load_iter->second);
        load_iter->second->set_net(cloned_net);
      }
    }
    cloned_net->set_loads(cloned_loads);
    net_map.emplace(net, cloned_net);
  }

  for (const auto* clock : get_clocks()) {
    if (clock == nullptr) {
      continue;
    }
    auto* cloned_clock = cloned->makeClock(clock->get_clock_name(), clock->get_clock_net_name());
    cloned_clock->set_clock_period_ns(clock->get_clock_period_ns());
    cloned_clock->set_clock_period_source(clock->get_clock_period_source());
    if (const auto source_iter = pin_map.find(clock->get_clock_source()); source_iter != pin_map.end()) {
      cloned_clock->set_clock_source(source_iter->second);
    }
    if (const auto source_net_iter = net_map.find(clock->get_clock_source_net()); source_net_iter != net_map.end()) {
      cloned_clock->set_clock_source_net(source_net_iter->second);
    }
    for (const auto* load : clock->get_loads()) {
      if (const auto load_iter = pin_map.find(load); load_iter != pin_map.end()) {
        cloned_clock->add_load(load_iter->second);
      }
    }
    for (const auto* inst : clock->get_insts()) {
      if (const auto inst_iter = inst_map.find(inst); inst_iter != inst_map.end()) {
        cloned_clock->add_inst(inst_iter->second);
      }
    }
    for (const auto* net : clock->get_nets()) {
      if (const auto net_iter = net_map.find(net); net_iter != net_map.end()) {
        cloned_clock->add_net(net_iter->second);
      }
    }
    cloned_clock->set_preclustered_sink_reuse(clock->is_preclustered_sink_reuse());
    for (const auto& net_name : clock->get_preclustered_anchor_input_net_names()) {
      cloned_clock->add_preclustered_anchor_input_net_name(net_name);
    }
  }

  if (_clock_dag.is_built()) {
    (void) cloned->rebuildClockDAG();
  }
  return cloned;
}

auto Design::get_clocks() const -> std::vector<Clock*>
{
  return collectBorrowedPointers(_clocks);
}

auto Design::get_insts() const -> std::vector<Inst*>
{
  return collectBorrowedPointers(_insts);
}

auto Design::get_pins() const -> std::vector<Pin*>
{
  return collectBorrowedPointers(_pins);
}

auto Design::get_nets() const -> std::vector<Net*>
{
  return collectBorrowedPointers(_nets);
}

auto Design::findClock(const std::string& clock_name, const std::string& clock_net_name) const -> Clock*
{
  const auto clock_name_iter = _clock_by_name_and_net.find(clock_name);
  if (clock_name_iter == _clock_by_name_and_net.end()) {
    return nullptr;
  }
  const auto clock_net_iter = clock_name_iter->second.find(clock_net_name);
  return clock_net_iter == clock_name_iter->second.end() ? nullptr : clock_net_iter->second;
}

auto Design::makeClock(const std::string& clock_name, const std::string& clock_net_name) -> Clock*
{
  auto* clock = findClock(clock_name, clock_net_name);
  if (clock == nullptr) {
    auto clock_owner = std::make_unique<Clock>(clock_name, clock_net_name);
    clock = clock_owner.get();
    _clocks.push_back(std::move(clock_owner));
    _clock_by_name_and_net[clock_name][clock_net_name] = clock;
  }
  _clock_dag.invalidate("clock_topology_changed");

  return clock;
}

auto Design::getPinFullName(const Pin* pin) -> std::string
{
  if (pin == nullptr) {
    return "";
  }
  auto* inst = pin->get_inst();
  if (inst == nullptr) {
    return pin->get_name();
  }
  return inst->get_name() + "/" + pin->get_name();
}

auto Design::findInst(const std::string& name) const -> Inst*
{
  const auto iter = _inst_by_name.find(name);
  return iter == _inst_by_name.end() ? nullptr : iter->second;
}

auto Design::makeInst(const std::string& name) -> Inst*
{
  auto* inst = findInst(name);
  if (inst == nullptr) {
    auto inst_owner = std::make_unique<Inst>(name, "", InstType::kUnknown, Point<int>(-1, -1));
    inst = inst_owner.get();
    _insts.push_back(std::move(inst_owner));
  }

  _inst_by_name[name] = inst;
  _inst_name_by_inst[inst] = name;
  _clock_dag.invalidate("clock_topology_changed");
  return inst;
}

auto Design::commitInst(std::unique_ptr<Inst> inst) -> Inst*
{
  if (inst == nullptr) {
    return nullptr;
  }

  if (findInst(inst->get_name()) != nullptr) {
    CTSLOG.warn(Loc::current(), "Design: reject committing inst \"", inst->get_name(), "\" because a final inst with the same name already exists.");
    return nullptr;
  }
  auto* inst_ptr = inst.get();
  _inst_by_name[inst_ptr->get_name()] = inst_ptr;
  _inst_name_by_inst[inst_ptr] = inst_ptr->get_name();
  _insts.push_back(std::move(inst));
  _clock_dag.invalidate("clock_topology_changed");
  return inst_ptr;
}

auto Design::findPin(const std::string& pin_full_name) const -> Pin*
{
  if (pin_full_name.empty()) {
    return nullptr;
  }

  const auto iter = _pin_by_full_name.find(pin_full_name);
  if (iter != _pin_by_full_name.end()) {
    auto* pin = iter->second;
    if (pin != nullptr && getPinFullName(pin) == pin_full_name) {
      return pin;
    }
  }

  return nullptr;
}

auto Design::makePin(const std::string& name) -> Pin*
{
  auto pin_owner = std::make_unique<Pin>(name);
  auto* pin = pin_owner.get();
  _pins.push_back(std::move(pin_owner));
  _clock_dag.invalidate("clock_topology_changed");
  return pin;
}

auto Design::indexPin(Pin* pin) -> bool
{
  if (pin == nullptr) {
    CTSLOG.warn(Loc::current(), "Design: failed to index null pin.");
    return false;
  }

  const auto full_name = getPinFullName(pin);
  if (full_name.empty()) {
    CTSLOG.warn(Loc::current(), "Design: failed to index pin with empty full name.");
    return false;
  }

  const auto map_iter = _pin_by_full_name.find(full_name);
  if (map_iter != _pin_by_full_name.end() && map_iter->second != pin) {
    CTSLOG.warn(Loc::current(), "Design: reject indexing pin \"", full_name, "\" because a different final pin already uses that full name.");
    return false;
  }

  const auto old_name_iter = _pin_full_name_by_pin.find(pin);
  if (old_name_iter != _pin_full_name_by_pin.end() && old_name_iter->second != full_name) {
    const auto old_pin_iter = _pin_by_full_name.find(old_name_iter->second);
    if (old_pin_iter != _pin_by_full_name.end() && old_pin_iter->second == pin) {
      _pin_by_full_name.erase(old_pin_iter);
    }
  }
  _pin_by_full_name[full_name] = pin;
  _pin_full_name_by_pin[pin] = full_name;
  _clock_dag.invalidate("clock_topology_changed");
  return true;
}

auto Design::commitPin(std::unique_ptr<Pin> pin) -> Pin*
{
  if (pin == nullptr) {
    return nullptr;
  }

  auto* existing_pin = findPin(getPinFullName(pin.get()));
  if (existing_pin != nullptr && existing_pin != pin.get()) {
    CTSLOG.warn(Loc::current(), "Design: reject committing pin \"", getPinFullName(pin.get()),
                "\" because a final pin with the same full name already exists.");
    return nullptr;
  }
  auto* pin_ptr = pin.get();
  auto* inst = pin_ptr->get_inst();
  if (inst != nullptr) {
    inst->add_pin(pin_ptr);
  }
  if (!indexPin(pin_ptr)) {
    if (inst != nullptr) {
      auto& pins = inst->get_pins();
      std::erase(pins, pin_ptr);
    }
    return nullptr;
  }
  _pins.push_back(std::move(pin));
  _clock_dag.invalidate("clock_topology_changed");
  return pin_ptr;
}

auto Design::renamePin(Pin* pin, const std::string& name) -> bool
{
  if (pin == nullptr) {
    CTSLOG.warn(Loc::current(), "Design: failed to rename null pin.");
    return false;
  }

  const auto* inst = pin->get_inst();
  const auto new_full_name = inst == nullptr ? name : inst->get_name() + "/" + name;
  if (new_full_name.empty()) {
    CTSLOG.warn(Loc::current(), "Design: reject renaming pin to an empty full name.");
    return false;
  }

  auto* existing_pin = findPin(new_full_name);
  if (existing_pin != nullptr && existing_pin != pin) {
    CTSLOG.warn(Loc::current(), "Design: reject renaming pin to \"", new_full_name, "\" because that final pin full name already exists.");
    return false;
  }

  const auto old_name = pin->get_name();
  pin->set_name(name);
  if (!indexPin(pin)) {
    pin->set_name(old_name);
    (void) indexPin(pin);
    return false;
  }
  _clock_dag.invalidate("clock_topology_changed");
  return true;
}

auto Design::findNet(const std::string& name) const -> Net*
{
  const auto iter = _net_by_name.find(name);
  return iter == _net_by_name.end() ? nullptr : iter->second;
}

auto Design::makeNet(const std::string& name) -> Net*
{
  auto* net = findNet(name);
  if (net == nullptr) {
    auto net_owner = std::make_unique<Net>(name);
    net = net_owner.get();
    _nets.push_back(std::move(net_owner));
  }

  _net_by_name[name] = net;
  _net_name_by_net[net] = name;
  _clock_dag.invalidate("clock_topology_changed");
  return net;
}

auto Design::commitNet(std::unique_ptr<Net> net) -> Net*
{
  if (net == nullptr) {
    return nullptr;
  }

  if (findNet(net->get_name()) != nullptr) {
    CTSLOG.warn(Loc::current(), "Design: reject committing net \"", net->get_name(), "\" because a final net with the same name already exists.");
    return nullptr;
  }
  auto* net_ptr = net.get();
  if (auto* driver = net_ptr->get_driver(); driver != nullptr) {
    driver->set_net(net_ptr);
  }
  for (auto* load : net_ptr->get_loads()) {
    if (load != nullptr) {
      load->set_net(net_ptr);
    }
  }
  _net_by_name[net_ptr->get_name()] = net_ptr;
  _net_name_by_net[net_ptr] = net_ptr->get_name();
  _nets.push_back(std::move(net));
  _clock_dag.invalidate("clock_topology_changed");
  return net_ptr;
}

auto Design::clearTopologyObjects() -> void
{
  _insts.clear();
  _pins.clear();
  _nets.clear();
  _inst_by_name.clear();
  _inst_name_by_inst.clear();
  _pin_by_full_name.clear();
  _pin_full_name_by_pin.clear();
  _net_by_name.clear();
  _net_name_by_net.clear();
  _clock_dag.invalidate("clock_topology_cleared");
}

auto Design::clearClocks() -> void
{
  _clock_by_name_and_net.clear();
  _clocks.clear();
  _clock_dag.invalidate("clock_topology_cleared");
}

auto Design::removePin(Pin* pin) -> void
{
  if (pin == nullptr) {
    return;
  }

  if (auto* net = pin->get_net(); net != nullptr) {
    if (net->get_driver() == pin) {
      net->set_driver(nullptr);
    }
    auto loads = net->get_loads();
    std::erase(loads, pin);
    net->set_loads(loads);
  }

  if (auto* inst = pin->get_inst(); inst != nullptr) {
    auto& pins = inst->get_pins();
    std::erase(pins, pin);
  }

  const auto full_name_iter = _pin_full_name_by_pin.find(pin);
  if (full_name_iter != _pin_full_name_by_pin.end()) {
    const auto pin_iter = _pin_by_full_name.find(full_name_iter->second);
    if (pin_iter != _pin_by_full_name.end() && pin_iter->second == pin) {
      _pin_by_full_name.erase(pin_iter);
    }
    _pin_full_name_by_pin.erase(full_name_iter);
  }
  std::erase_if(_pins, [pin](const auto& object) -> bool { return object.get() == pin; });
  _clock_dag.invalidate("clock_topology_changed");
}

auto Design::removeInst(Inst* inst) -> void
{
  if (inst == nullptr) {
    return;
  }

  auto pins = inst->get_pins();
  for (auto* pin : pins) {
    removePin(pin);
  }
  eraseRecordedNameIndexEntry(_inst_by_name, _inst_name_by_inst, inst, inst->get_name());
  std::erase_if(_insts, [inst](const auto& object) -> bool { return object.get() == inst; });
  _clock_dag.invalidate("clock_topology_changed");
}

auto Design::removeNet(Net* net) -> void
{
  if (net == nullptr) {
    return;
  }

  if (auto* driver = net->get_driver(); driver != nullptr && driver->get_net() == net) {
    driver->set_net(nullptr);
  }
  for (auto* load : net->get_loads()) {
    if (load != nullptr && load->get_net() == net) {
      load->set_net(nullptr);
    }
  }
  eraseRecordedNameIndexEntry(_net_by_name, _net_name_by_net, net, net->get_name());
  std::erase_if(_nets, [net](const auto& object) -> bool { return object.get() == net; });
  _clock_dag.invalidate("clock_topology_changed");
}

auto Design::removeClockMembershipObjects(Clock& clock) -> void
{
  const auto* clock_source_net = clock.get_clock_source_net();
  for (auto* net : clock.get_nets()) {
    if (net == clock_source_net) {
      continue;
    }
    removeNet(net);
  }

  for (auto* inst : clock.get_insts()) {
    removeInst(inst);
  }
  _clock_dag.invalidate("clock_topology_changed");
}

auto Design::rebuildClockDAG() -> bool
{
  return _clock_dag.rebuild(get_clocks());
}

auto Design::clearClockDAG() -> void
{
  _clock_dag.clear();
}

}  // namespace icts
