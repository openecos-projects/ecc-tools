// SPDX-License-Identifier: MulanPSL-2.0
#pragma once

#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "network/HierarchyPin.hpp"
#include "network/Instance.hpp"
#include "network/Net.hpp"
#include "network/Pin.hpp"

namespace ista {
class LibertyLibrary;
class LibertyPort;

struct NetSegment
{
  std::string name;
  std::string scope;  // Elaborated prefix, including its final separator.
  std::string physical_net;
  std::vector<std::string> pins;
  std::string error;
};

// Owns design connectivity and hierarchy. Queries never read propagated timing
// or Sdc. Mutable access is used by readers/builders before analysis begins.
class Network
{
 public:
  std::string& name() { return _name; }
  const std::string& name() const { return _name; }
  std::map<std::string, Instance>& instances() { return _instances; }
  const std::map<std::string, Instance>& instances() const { return _instances; }
  std::map<std::string, Pin>& pins() { return _pins; }
  const std::map<std::string, Pin>& pins() const { return _pins; }
  std::map<std::string, Net>& nets() { return _nets; }
  const std::map<std::string, Net>& nets() const { return _nets; }
  std::map<std::string, HierarchyPin>& hierarchyPins() { return _hierarchy_pins; }
  const std::map<std::string, HierarchyPin>& hierarchyPins() const { return _hierarchy_pins; }
  std::unordered_map<std::string, size_t>& hierarchyIndices() { return _hierarchy_indices; }
  const std::unordered_map<std::string, size_t>& hierarchyIndices() const { return _hierarchy_indices; }
  const std::map<std::string, NetSegment>& netSegments() const { return _net_segments; }
  void setNetSegments(std::map<std::string, NetSegment> segments);
  const NetSegment& netSegment(const std::string& name) const;
  bool hasNet(const std::string& name) const;
  const std::string& physicalNet(const std::string& name) const;
  std::string netQueryName(const std::string& physical_name) const;
  std::vector<std::string> netSegmentPins(const std::string& name, bool leaf) const;
  const std::vector<std::string>& pinNets(const std::string& pin) const;
  void setName(const std::string& name) { _name = name; }
  const LibertyPort* libertyPort(const LibertyLibrary& liberty, const Pin& pin) const;
  TransType launchClockTransition(std::string_view start) const;
  bool hasHierarchyPins() const;
  bool isHierarchyPin(const std::string& name) const;
  std::set<std::string> hierarchyCells() const;
  const HierarchyPin& checkedHierarchyPin(const std::string& name) const;
  bool crossesHierarchyPin(const HierarchyPin& boundary, std::string_view driver, std::string_view load) const;
  int hierarchyCrossingPosition(const HierarchyPin& boundary, std::string_view driver, std::string_view load) const;
  std::vector<std::string> hierarchyDrivers(const std::string& name) const;

 private:
  std::map<std::string, NetSegment> _net_segments;
  std::unordered_map<std::string, std::string> _net_query_names;
  std::unordered_map<std::string, std::vector<std::string>> _pin_nets;
  std::string _name;
  std::map<std::string, Instance> _instances;
  std::map<std::string, Pin> _pins;
  std::map<std::string, Net> _nets;
  std::map<std::string, HierarchyPin> _hierarchy_pins;
  std::unordered_map<std::string, size_t> _hierarchy_indices;
};
}  // namespace ista
