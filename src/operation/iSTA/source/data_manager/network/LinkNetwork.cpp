// SPDX-License-Identifier: MulanPSL-2.0
#include "network/LinkNetwork.hpp"

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace ista {
void LinkNetwork::link()
{
  linkInstancePins();
  linkNets();
  linkInstances();
}

void LinkNetwork::linkInstancePins()
{
  for (const auto& id : _network.instances().sortedIds()) {
    _network.instances().get(id).get_pin_name_list().clear();
  }

  for (const auto& id : _network.pins().sortedIds()) {
    Pin& pin = _network.pins().get(id);
    if (!isInstancePin(pin)) {
      continue;
    }

    appendUnique(_network.instances().at(pin.get_instance_name()).get_pin_name_list(), std::string(_network.pins().nameOf(id)));
  }
}

void LinkNetwork::linkInstances()
{
  for (const auto& id : _network.instances().sortedIds()) {
    linkInstance(_network.instances().get(id));
  }
}

void LinkNetwork::linkInstance(Instance& instance)
{
  auto& timing_cell_map = _liberty.get_cell_map();
  if (timing_cell_map.count(instance.get_cell_name()) == 0) {
    return;
  }

  LibertyCell& timing_cell = timing_cell_map[instance.get_cell_name()];
  instance.set_is_sequential(timing_cell.get_is_sequential());
  instance.set_is_clock_gating(timing_cell.get_is_clock_gating());
  instance.set_has_clear_arc(timing_cell.get_has_clear_arc());
  instance.set_has_preset_arc(timing_cell.get_has_preset_arc());
  TimingCellArc* clock_to_q_arc = findClockToQArc(instance, timing_cell);
  if (clock_to_q_arc != nullptr) {
    instance.set_output_pin_name(instancePinName(instance, clock_to_q_arc->get_sink_port()));
    instance.set_clock_to_q_delay(clock_to_q_arc->get_delay());
    instance.set_clock_to_q_arc(*clock_to_q_arc);
  } else {
    instance.set_output_pin_name(findOutputPinName(instance, timing_cell));
  }
  instance.get_check_arc_list().clear();
  for (TimingCheckArc& timing_check_arc : timing_cell.get_check_arc_list()) {
    instance.get_check_arc_list().push_back(instanceCheck(instance, timing_check_arc));
  }

  TimingCheckArc* representative_check_arc = nullptr;
  for (TimingCheckArc& timing_check_arc : timing_cell.get_check_arc_list()) {
    if (representative_check_arc == nullptr || timing_check_arc.get_check_type() == TimingCheckType::kSetup) {
      representative_check_arc = &timing_check_arc;
    }
    if (timing_check_arc.get_check_type() == TimingCheckType::kSetup) {
      break;
    }
  }
  if (representative_check_arc != nullptr) {
    instance.set_clock_pin_name(instancePinName(instance, representative_check_arc->get_clock_port()));
    instance.set_data_pin_name(instancePinName(instance, representative_check_arc->get_data_port()));
  }
}

TimingCheckArc LinkNetwork::instanceCheck(Instance& instance, TimingCheckArc& timing_check_arc)
{
  TimingCheckArc instance_timing_check_arc;
  instance_timing_check_arc.set_clock_port(instancePinName(instance, timing_check_arc.get_clock_port()));
  instance_timing_check_arc.set_data_port(instancePinName(instance, timing_check_arc.get_data_port()));
  instance_timing_check_arc.set_check_type(timing_check_arc.get_check_type());
  instance_timing_check_arc.set_check_time(timing_check_arc.get_check_time());
  instance_timing_check_arc.set_timing_arc_list(timing_check_arc.get_timing_arc_list());
  instance_timing_check_arc.set_clock_trans_type(timing_check_arc.get_clock_trans_type());
  return instance_timing_check_arc;
}

TimingCellArc* LinkNetwork::findClockToQArc(Instance& instance, LibertyCell& timing_cell)
{
  TimingCellArc* first_clock_arc = nullptr;
  for (TimingCellArc& timing_cell_arc : timing_cell.get_cell_arc_list()) {
    if (!timing_cell_arc.get_is_clock_arc())
      continue;
    if (first_clock_arc == nullptr)
      first_clock_arc = &timing_cell_arc;
    const std::string sink_pin = instancePinName(instance, timing_cell_arc.get_sink_port());
    const auto pin = _network.pins().find(sink_pin);
    // A sequential master can expose both Q and QN.  The DEF may connect only
    // one of them; bind the clock-to-output metadata to that physical output
    // instead of whichever Liberty arc happens to be listed first.
    if (pin != _network.pins().end() && !pin->get_net_name().empty())
      return &timing_cell_arc;
  }
  return first_clock_arc;
}

std::string LinkNetwork::instancePinName(Instance& instance, std::string& port_name)
{
  return instance.get_instance_name() + ":" + port_name;
}

std::string LinkNetwork::findOutputPinName(Instance& instance, LibertyCell& timing_cell)
{
  for (auto& [port_name, timing_cell_port] : timing_cell.get_port_map()) {
    if (timing_cell_port.get_is_output() && !timing_cell_port.get_is_clock()) {
      return instancePinName(instance, timing_cell_port.get_port_name());
    }
  }
  return "";
}

bool LinkNetwork::isInstancePin(Pin& pin)
{
  return !pin.get_is_port();
}

void LinkNetwork::appendUnique(std::vector<std::string>& list, const std::string& value)
{
  if (std::find(list.begin(), list.end(), value) == list.end()) {
    list.push_back(value);
  }
}

void LinkNetwork::linkNets()
{
  for (const auto& id : _network.pins().sortedIds()) {
    _network.pins().get(id).get_net_name().clear();
  }

  for (const auto& id : _network.nets().sortedIds()) {
    const std::string net_name(_network.nets().nameOf(id));
    linkNet(net_name, _network.nets().get(id));
  }
}

void LinkNetwork::linkNet(const std::string& net_name, Net& net)
{
  net.get_driver_pin().clear();
  net.get_driver_pin_list().clear();
  net.get_load_pin_list().clear();

  for (std::string& pin_name : net.get_pin_name_list()) {
    Pin& pin = _network.pins().at(pin_name);
    pin.set_net_name(net_name);
    appendUnique(net.get_load_pin_list(), pin_name);
  }
}
}  // namespace ista
