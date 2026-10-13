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

#include "ParasiticResistor.hpp"
#include "PWHeader.hpp"

namespace ipw {

class ParasiticNet
{
 public:
  ParasiticNet() = default;
  ~ParasiticNet() = default;
  // getter
  std::string& get_net_name() { return _net_name; }
  std::map<std::string, double>& get_node_capacitance_map() { return _node_capacitance_map; }
  std::vector<ParasiticResistor>& get_resistor_list() { return _resistor_list; }
  // setter
  void set_net_name(const std::string& net_name) { _net_name = net_name; }
  // function

 private:
  std::string _net_name;
  std::map<std::string, double> _node_capacitance_map;
  std::vector<ParasiticResistor> _resistor_list;
};

}  // namespace ipw
