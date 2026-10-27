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

#include <map>
#include <string>
#include <vector>

namespace ista {

class ParasiticNode
{
 public:
  ParasiticNode() = default;
  ~ParasiticNode() = default;
  // getter
  std::string& get_node_name() { return _node_name; }
  double get_capacitance() const { return _capacitance; }
  double get_x() const { return _x; }
  double get_y() const { return _y; }
  // setter
  void set_node_name(const std::string& node_name) { _node_name = node_name; }
  void set_capacitance(const double capacitance) { _capacitance = capacitance; }
  void set_x(const double x) { _x = x; }
  void set_y(const double y) { _y = y; }
  // function

 private:
  std::string _node_name;
  double _capacitance = 0.0;
  double _x = 0.0;
  double _y = 0.0;
};

class ParasiticResistor
{
 public:
  ParasiticResistor() = default;
  ~ParasiticResistor() = default;
  // getter
  std::string& get_source_node() { return _source_node; }
  std::string& get_sink_node() { return _sink_node; }
  double get_resistance() const { return _resistance; }
  // setter
  void set_source_node(const std::string& source_node) { _source_node = source_node; }
  void set_sink_node(const std::string& sink_node) { _sink_node = sink_node; }
  void set_resistance(const double resistance) { _resistance = resistance; }
  // function

 private:
  std::string _source_node;
  std::string _sink_node;
  double _resistance = 0.0;
};

class ParasiticNet
{
 public:
  ParasiticNet() = default;
  ~ParasiticNet() = default;
  // getter
  std::string& get_net_name() { return _net_name; }
  std::map<std::string, ParasiticNode>& get_node_map() { return _node_map; }
  std::vector<ParasiticResistor>& get_resistor_list() { return _resistor_list; }
  double get_lumped_capacitance() const { return _lumped_capacitance; }
  // setter
  void set_net_name(const std::string& net_name) { _net_name = net_name; }
  void set_node_map(const std::map<std::string, ParasiticNode>& node_map) { _node_map = node_map; }
  void set_resistor_list(const std::vector<ParasiticResistor>& resistor_list) { _resistor_list = resistor_list; }
  void set_lumped_capacitance(const double lumped_capacitance) { _lumped_capacitance = lumped_capacitance; }
  // function

 private:
  std::string _net_name;
  std::map<std::string, ParasiticNode> _node_map;
  std::vector<ParasiticResistor> _resistor_list;
  double _lumped_capacitance = 0.0;
};

class Parasitics
{
 public:
  Parasitics() = default;
  ~Parasitics() = default;
  // getter
  std::string& get_spef_file_path() { return _spef_file_path; }
  std::string& get_capacitive_unit() { return _capacitive_unit; }
  std::string& get_resistance_unit() { return _resistance_unit; }
  std::map<std::string, ParasiticNet>& nets() { return _net_map; }
  const std::map<std::string, ParasiticNet>& nets() const { return _net_map; }
  // setter
  void set_spef_file_path(const std::string& spef_file_path) { _spef_file_path = spef_file_path; }
  void set_capacitive_unit(const std::string& capacitive_unit) { _capacitive_unit = capacitive_unit; }
  void set_resistance_unit(const std::string& resistance_unit) { _resistance_unit = resistance_unit; }
  void set_net_map(const std::map<std::string, ParasiticNet>& net_map) { _net_map = net_map; }
  // function

 private:
  std::string _spef_file_path;
  std::string _capacitive_unit;
  std::string _resistance_unit;
  std::map<std::string, ParasiticNet> _net_map;
};

}  // namespace ista
