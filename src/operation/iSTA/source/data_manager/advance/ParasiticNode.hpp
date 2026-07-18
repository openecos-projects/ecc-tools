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

}  // namespace ista
