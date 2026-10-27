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
// WHETHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include <map>
#include <set>
#include <string>

#include "common/TransType.hpp"

namespace ista {

class DelayResult
{
 public:
  DelayResult() = default;
  ~DelayResult() = default;
  // getter
  TransType get_output_trans_type() const { return _output_trans_type; }
  double get_delay() const { return _delay; }
  double get_slew() const { return _slew; }
  // setter
  void set_output_trans_type(const TransType& output_trans_type) { _output_trans_type = output_trans_type; }
  void set_delay(const double delay) { _delay = delay; }
  void set_slew(const double slew) { _slew = slew; }
  // function

 private:
  TransType _output_trans_type = TransType::kNone;
  double _delay = 0.0;
  double _slew = 0.0;
};

// Gate/driver and per-receiver values, independent of the solving algorithm.
class DriverResponse
{
 public:
  DriverResponse() = default;
  ~DriverResponse() = default;
  // getter
  bool get_is_valid() const { return _is_valid; }
  double get_effective_capacitance() const { return _effective_capacitance; }
  double get_gate_delay() const { return _gate_delay; }
  double get_driver_slew() const { return _driver_slew; }
  std::map<std::string, double>& get_wire_delay_map() { return _wire_delay_map; }
  const std::map<std::string, double>& get_wire_delay_map() const { return _wire_delay_map; }
  std::map<std::string, double>& get_load_slew_map() { return _load_slew_map; }
  const std::map<std::string, double>& get_load_slew_map() const { return _load_slew_map; }
  bool loadFailed(const std::string& pin) const { return _failed_loads.contains(pin); }
  void setLoadFailed(const std::string& pin) { _failed_loads.insert(pin); }
  // setter
  void set_is_valid(const bool is_valid) { _is_valid = is_valid; }
  void set_effective_capacitance(const double effective_capacitance) { _effective_capacitance = effective_capacitance; }
  void set_gate_delay(const double gate_delay) { _gate_delay = gate_delay; }
  void set_driver_slew(const double driver_slew) { _driver_slew = driver_slew; }
  void set_wire_delay_map(const std::map<std::string, double>& wire_delay_map) { _wire_delay_map = wire_delay_map; }
  void set_load_slew_map(const std::map<std::string, double>& load_slew_map) { _load_slew_map = load_slew_map; }
  // function

 private:
  std::set<std::string> _failed_loads;
  bool _is_valid = false;
  double _effective_capacitance = 0.0;
  double _gate_delay = 0.0;
  double _driver_slew = 0.0;
  std::map<std::string, double> _wire_delay_map;
  std::map<std::string, double> _load_slew_map;
};

}  // namespace ista
