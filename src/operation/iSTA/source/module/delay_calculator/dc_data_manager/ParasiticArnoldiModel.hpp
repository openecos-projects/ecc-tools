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

#include "AnalysisType.hpp"
#include "STAHeader.hpp"
#include "TransType.hpp"

namespace ista {

using ParasiticArnoldiTimingResultKey = std::tuple<std::string, std::uintptr_t, AnalysisType, TransType, double>;
using ParasiticArnoldiDriverResultKey = std::tuple<std::string, AnalysisType, TransType, double>;
using ParasiticArnoldiModelKey = std::tuple<std::string, std::string, AnalysisType, TransType>;

class ParasiticArnoldiModel
{
 public:
  ParasiticArnoldiModel() = default;
  ~ParasiticArnoldiModel() = default;
  // getter
  bool get_is_valid() const { return _is_valid; }
  int32_t get_order() const { return _order; }
  double get_total_capacitance() const { return _total_capacitance; }
  double get_sqrt_total_capacitance() const { return _sqrt_total_capacitance; }
  std::vector<std::string>& get_term_node_list() { return _term_node_list; }
  std::vector<std::string>& get_term_pin_list() { return _term_pin_list; }
  std::map<std::string, std::size_t>& get_term_index_map() { return _term_index_map; }
  std::vector<double>& get_diagonal_list() { return _diagonal_list; }
  std::vector<double>& get_off_diagonal_list() { return _off_diagonal_list; }
  std::vector<std::vector<double>>& get_projection_list() { return _projection_list; }
  // setter
  void set_is_valid(const bool is_valid) { _is_valid = is_valid; }
  void set_order(const int32_t order) { _order = order; }
  void set_total_capacitance(const double total_capacitance) { _total_capacitance = total_capacitance; }
  void set_sqrt_total_capacitance(const double sqrt_total_capacitance) { _sqrt_total_capacitance = sqrt_total_capacitance; }
  void set_term_node_list(const std::vector<std::string>& term_node_list) { _term_node_list = term_node_list; }
  void set_term_pin_list(const std::vector<std::string>& term_pin_list) { _term_pin_list = term_pin_list; }
  void set_term_index_map(const std::map<std::string, std::size_t>& term_index_map) { _term_index_map = term_index_map; }
  void set_diagonal_list(const std::vector<double>& diagonal_list) { _diagonal_list = diagonal_list; }
  void set_off_diagonal_list(const std::vector<double>& off_diagonal_list) { _off_diagonal_list = off_diagonal_list; }
  void set_projection_list(const std::vector<std::vector<double>>& projection_list) { _projection_list = projection_list; }
  // function

 private:
  bool _is_valid = false;
  int32_t _order = 0;
  double _total_capacitance = 0.0;
  double _sqrt_total_capacitance = 0.0;
  std::vector<std::string> _term_node_list;
  std::vector<std::string> _term_pin_list;
  std::map<std::string, std::size_t> _term_index_map;
  std::vector<double> _diagonal_list;
  std::vector<double> _off_diagonal_list;
  std::vector<std::vector<double>> _projection_list;
};

class ParasiticArnoldiPoleResidue
{
 public:
  ParasiticArnoldiPoleResidue() = default;
  ~ParasiticArnoldiPoleResidue() = default;
  // getter
  bool get_is_valid() const { return _is_valid; }
  int32_t get_order() const { return _order; }
  std::vector<double>& get_pole_list() { return _pole_list; }
  std::vector<std::vector<double>>& get_residue_list() { return _residue_list; }
  // setter
  void set_is_valid(const bool is_valid) { _is_valid = is_valid; }
  void set_order(const int32_t order) { _order = order; }
  void set_pole_list(const std::vector<double>& pole_list) { _pole_list = pole_list; }
  void set_residue_list(const std::vector<std::vector<double>>& residue_list) { _residue_list = residue_list; }
  // function

 private:
  bool _is_valid = false;
  int32_t _order = 0;
  std::vector<double> _pole_list;
  std::vector<std::vector<double>> _residue_list;
};

class ParasiticArnoldiTimingResult
{
 public:
  ParasiticArnoldiTimingResult() = default;
  ~ParasiticArnoldiTimingResult() = default;
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
