// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

#include <ostream>
#include <string>

#include "SdfDelay.hpp"
#include "liberty/TimingArc.hpp"
#include "liberty/TimingCheckType.hpp"
#include "network/Network.hpp"
namespace ista {
class SdfFormatter
{
 public:
  explicit SdfFormatter(Network& network) : _network(network) {}
  std::string checkName(TimingCheckType timing_check_type);
  std::string pathName(Pin& pin);
  std::string portName(Pin& pin);
  std::string instanceName(Instance& instance);
  std::string escapeName(std::string& name);
  std::string condition(TimingArc& timing_arc);
  std::string edgeName(TransType trans_type);
  void delay(std::ostream& sdf_file, SdfDelay& sdf_delay, bool explicit_edges = false);
  void triple(std::ostream& sdf_file, double min_delay, double max_delay);
  std::string number(double value);

 private:
  Network& _network;
};
}  // namespace ista
