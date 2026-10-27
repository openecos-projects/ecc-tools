// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

#include <vector>

#include "check/ClockCheck.hpp"
#include "check/ElectricalCheck.hpp"
#include "check/FanoutCheck.hpp"
#include "graph/Graph.hpp"
#include "liberty/LibertyLibrary.hpp"
#include "network/Network.hpp"
#include "parasitics/Parasitics.hpp"
#include "path/PathGroup.hpp"
#include "path/PathStats.hpp"
#include "sdc/Sdc.hpp"

namespace ista {
// Session-owned domain composition. Domains own their containers and do not
// borrow the containing Database. Graph repairs its vertex references on copy.
class Database
{
 public:
  Network& network() { return _network; }
  const Network& network() const { return _network; }
  Graph& graph() { return _graph; }
  const Graph& graph() const { return _graph; }
  LibertyLibrary& liberty() { return _liberty; }
  const LibertyLibrary& liberty() const { return _liberty; }
  Parasitics& parasitics() { return _parasitics; }
  const Parasitics& parasitics() const { return _parasitics; }
  Sdc& sdc() { return _sdc; }
  const Sdc& sdc() const { return _sdc; }
  std::vector<PathGroup>& pathGroups() { return _path_groups; }
  const std::vector<PathGroup>& pathGroups() const { return _path_groups; }
  PathStats& pathStats() { return _path_stats; }
  const PathStats& pathStats() const { return _path_stats; }
  std::vector<FanoutCheck>& fanoutChecks() { return _fanout_checks; }
  const std::vector<FanoutCheck>& fanoutChecks() const { return _fanout_checks; }
  std::vector<ElectricalCheck>& electricalChecks() { return _electrical_checks; }
  const std::vector<ElectricalCheck>& electricalChecks() const { return _electrical_checks; }

  std::vector<ClockCheck>& clockChecks() { return _clock_checks; }
  const std::vector<ClockCheck>& clockChecks() const { return _clock_checks; }

 private:
  Network _network;
  Graph _graph;
  LibertyLibrary _liberty;
  Parasitics _parasitics;
  Sdc _sdc;
  std::vector<PathGroup> _path_groups;
  PathStats _path_stats;
  std::vector<FanoutCheck> _fanout_checks;
  std::vector<ElectricalCheck> _electrical_checks;
  std::vector<ClockCheck> _clock_checks;
};
}  // namespace ista
