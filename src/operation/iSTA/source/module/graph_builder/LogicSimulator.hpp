// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// ***************************************************************************************
#pragma once

#include <map>
#include <optional>
#include <string>

#include "common/TimingCaseValue.hpp"

namespace ista {
class Network;
class LibertyLibrary;
class Sdc;
class Graph;
class SimValues;
class Instance;
class Net;
class LogicExpression;

class LogicSimulator
{
 public:
  LogicSimulator(const Network& network, const LibertyLibrary& liberty, const Sdc& sdc, Graph& graph);
  void propagate();

 private:
  bool updateValue(const std::string& pin, TimingCaseValue value);
  bool updateDriverValue(const std::string& pin, TimingCaseValue value);
  std::optional<TimingCaseValue> driverValue(const Net& net, const std::string& load) const;
  std::map<std::string, bool> constantInputs(const Instance& instance) const;
  bool expressionInputsAreStatic(const Instance& instance, const LogicExpression& expression) const;
  void propagateCaseValues();
  void disableInsensitiveArcs();

  const Network& _network;
  const LibertyLibrary& _liberty;
  const Sdc& _sdc;
  Graph& _graph;
  SimValues& _values;
};

}  // namespace ista
