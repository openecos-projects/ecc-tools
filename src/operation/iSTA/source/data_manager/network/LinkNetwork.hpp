// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

#include <string>
#include <vector>

#include "liberty/LibertyLibrary.hpp"
#include "network/Network.hpp"

namespace ista {
// Rebuild imported connectivity and bind instance timing metadata to its library.
// Pin-direction resolution and graph construction run after this operation.
class LinkNetwork
{
 public:
  LinkNetwork(Network& network, LibertyLibrary& liberty) : _network(network), _liberty(liberty) {}
  void link();

 private:
  void linkInstancePins();
  void linkInstances();
  void linkInstance(Instance& instance);
  TimingCheckArc instanceCheck(Instance& instance, TimingCheckArc& timing_check_arc);
  TimingCellArc* findClockToQArc(LibertyCell& timing_cell);
  std::string instancePinName(Instance& instance, std::string& port_name);
  std::string findOutputPinName(Instance& instance, LibertyCell& timing_cell);
  bool isInstancePin(Pin& pin);
  void appendUnique(std::vector<std::string>& list, const std::string& value);
  void linkNets();
  void linkNet(const std::string& net_name, Net& net);
  Network& _network;
  LibertyLibrary& _liberty;
};
}  // namespace ista
