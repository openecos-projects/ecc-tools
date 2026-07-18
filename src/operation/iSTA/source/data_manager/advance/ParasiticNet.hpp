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

#include "ParasiticNode.hpp"
#include "ParasiticResistor.hpp"
#include "STAHeader.hpp"

namespace ista {

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

}  // namespace ista
