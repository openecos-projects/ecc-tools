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
#pragma once

#include "STAHeader.hpp"

namespace ista {

class ParasiticDmpLoadModel
{
 public:
  ParasiticDmpLoadModel() = default;
  ~ParasiticDmpLoadModel() = default;
  // getter
  bool get_is_valid() const { return _is_valid; }
  std::vector<double>& get_pole_list() { return _pole_list; }
  std::vector<double>& get_residue_list() { return _residue_list; }
  // setter
  void set_is_valid(const bool is_valid) { _is_valid = is_valid; }
  void set_pole_list(const std::vector<double>& pole_list) { _pole_list = pole_list; }
  void set_residue_list(const std::vector<double>& residue_list) { _residue_list = residue_list; }
  // function

 private:
  bool _is_valid = false;
  std::vector<double> _pole_list;
  std::vector<double> _residue_list;
};

class ParasiticDmpModel
{
 public:
  ParasiticDmpModel() = default;
  ~ParasiticDmpModel() = default;
  // getter
  bool get_is_valid() const { return _is_valid; }
  double get_driver_capacitance() const { return _driver_capacitance; }
  double get_pi_resistance() const { return _pi_resistance; }
  double get_load_capacitance() const { return _load_capacitance; }
  std::map<std::string, ParasiticDmpLoadModel>& get_load_model_map() { return _load_model_map; }
  // setter
  void set_is_valid(const bool is_valid) { _is_valid = is_valid; }
  void set_driver_capacitance(const double driver_capacitance) { _driver_capacitance = driver_capacitance; }
  void set_pi_resistance(const double pi_resistance) { _pi_resistance = pi_resistance; }
  void set_load_capacitance(const double load_capacitance) { _load_capacitance = load_capacitance; }
  void set_load_model_map(const std::map<std::string, ParasiticDmpLoadModel>& load_model_map) { _load_model_map = load_model_map; }
  // function

 private:
  bool _is_valid = false;
  double _driver_capacitance = 0.0;
  double _pi_resistance = 0.0;
  double _load_capacitance = 0.0;
  std::map<std::string, ParasiticDmpLoadModel> _load_model_map;
};

class ParasiticDmpTimingResult
{
 public:
  ParasiticDmpTimingResult() = default;
  ~ParasiticDmpTimingResult() = default;
  // getter
  bool get_is_valid() const { return _is_valid; }
  double get_effective_capacitance() const { return _effective_capacitance; }
  double get_gate_delay() const { return _gate_delay; }
  double get_driver_slew() const { return _driver_slew; }
  std::map<std::string, double>& get_wire_delay_map() { return _wire_delay_map; }
  std::map<std::string, double>& get_load_slew_map() { return _load_slew_map; }
  // setter
  void set_is_valid(const bool is_valid) { _is_valid = is_valid; }
  void set_effective_capacitance(const double effective_capacitance) { _effective_capacitance = effective_capacitance; }
  void set_gate_delay(const double gate_delay) { _gate_delay = gate_delay; }
  void set_driver_slew(const double driver_slew) { _driver_slew = driver_slew; }
  void set_wire_delay_map(const std::map<std::string, double>& wire_delay_map) { _wire_delay_map = wire_delay_map; }
  void set_load_slew_map(const std::map<std::string, double>& load_slew_map) { _load_slew_map = load_slew_map; }
  // function

 private:
  bool _is_valid = false;
  double _effective_capacitance = 0.0;
  double _gate_delay = 0.0;
  double _driver_slew = 0.0;
  std::map<std::string, double> _wire_delay_map;
  std::map<std::string, double> _load_slew_map;
};

}  // namespace ista
