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
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include <map>
#include <string>
#include <vector>

#include "common/AnalysisType.hpp"
#include "network/PinDirection.hpp"

namespace ista {

// Value model shared by extraction and Liberty output. No extraction state lives here.
class ScalarTable
{
 public:
  ScalarTable() = default;
  ~ScalarTable() = default;
  // getter
  std::string& get_table_name() { return _table_name; }
  double get_value() const { return _value; }
  // setter
  void set_table_name(const std::string& table_name) { _table_name = table_name; }
  void set_value(const double value) { _value = value; }
  // function

 private:
  std::string _table_name;
  double _value = 0.0;
};

class CharacterizedPort
{
 public:
  CharacterizedPort() = default;
  ~CharacterizedPort() = default;
  // getter
  std::string& get_port_name() { return _port_name; }
  PinDirection get_direction() const { return _direction; }
  bool get_is_clock() const { return _is_clock; }
  double get_capacitance() const { return _capacitance; }
  // setter
  void set_port_name(const std::string& port_name) { _port_name = port_name; }
  void set_direction(const PinDirection& direction) { _direction = direction; }
  void set_is_clock(const bool is_clock) { _is_clock = is_clock; }
  void set_capacitance(const double capacitance) { _capacitance = capacitance; }
  // function

 private:
  std::string _port_name;
  PinDirection _direction = PinDirection::kNone;
  bool _is_clock = false;
  double _capacitance = 0.0;
};

class CharacterizedTimingArc
{
 public:
  CharacterizedTimingArc() = default;
  ~CharacterizedTimingArc() = default;
  // getter
  std::string& get_source_port() { return _source_port; }
  std::string& get_sink_port() { return _sink_port; }
  std::string& get_timing_type() { return _timing_type; }
  std::string& get_timing_sense() { return _timing_sense; }
  bool get_is_check_arc() const { return _is_check_arc; }
  std::vector<ScalarTable>& get_scalar_table_list() { return _scalar_table_list; }
  // setter
  void set_source_port(const std::string& source_port) { _source_port = source_port; }
  void set_sink_port(const std::string& sink_port) { _sink_port = sink_port; }
  void set_timing_type(const std::string& timing_type) { _timing_type = timing_type; }
  void set_timing_sense(const std::string& timing_sense) { _timing_sense = timing_sense; }
  void set_is_check_arc(const bool is_check_arc) { _is_check_arc = is_check_arc; }
  void set_scalar_table_list(const std::vector<ScalarTable>& scalar_table_list) { _scalar_table_list = scalar_table_list; }
  void addScalarTable(const std::string& table_name, double value)
  {
    ScalarTable table;
    table.set_table_name(table_name);
    table.set_value(value);
    _scalar_table_list.push_back(table);
  }

 private:
  std::string _source_port;
  std::string _sink_port;
  std::string _timing_type;
  std::string _timing_sense;
  bool _is_check_arc = false;
  std::vector<ScalarTable> _scalar_table_list;
};

class CharacterizedLibrary
{
 public:
  CharacterizedLibrary() = default;
  ~CharacterizedLibrary() = default;
  // getter
  std::string& get_design_name() { return _design_name; }
  AnalysisType get_analysis_type() const { return _analysis_type; }
  double get_area() const { return _area; }
  std::map<std::string, CharacterizedPort>& get_port_map() { return _port_map; }
  std::vector<CharacterizedTimingArc>& get_timing_arc_list() { return _timing_arc_list; }
  // setter
  void set_design_name(const std::string& design_name) { _design_name = design_name; }
  void set_analysis_type(const AnalysisType& analysis_type) { _analysis_type = analysis_type; }
  void set_area(const double area) { _area = area; }
  void set_port_map(const std::map<std::string, CharacterizedPort>& port_map) { _port_map = port_map; }
  void set_timing_arc_list(const std::vector<CharacterizedTimingArc>& timing_arc_list) { _timing_arc_list = timing_arc_list; }
  // function

 private:
  std::string _design_name;
  AnalysisType _analysis_type = AnalysisType::kNone;
  double _area = 0.0;
  std::map<std::string, CharacterizedPort> _port_map;
  std::vector<CharacterizedTimingArc> _timing_arc_list;
};

class TimingModel
{
 public:
  TimingModel() = default;
  ~TimingModel() = default;
  // getter
  std::vector<CharacterizedLibrary>& get_lib_list() { return _lib_list; }
  // setter
  void set_lib_list(const std::vector<CharacterizedLibrary>& lib_list) { _lib_list = lib_list; }
  // function

 private:
  std::vector<CharacterizedLibrary> _lib_list;
};

}  // namespace ista
