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
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "LVSHeader.hpp"
#include "Shape.hpp"

namespace ilvs {

class RoutingCheck
{
 public:
  RoutingCheck() = default;
  ~RoutingCheck() = default;
  // getter
  std::string& get_net_name() { return _net_name; }
  std::string& get_driver_terminal_name() { return _driver_terminal_name; }
  std::vector<std::string>& get_disconnected_terminal_name_list() { return _disconnected_terminal_name_list; }
  std::vector<Shape>& get_disconnected_shape_list() { return _disconnected_shape_list; }
  // const getter
  const std::string& get_net_name() const { return _net_name; }
  const std::string& get_driver_terminal_name() const { return _driver_terminal_name; }
  const std::vector<std::string>& get_disconnected_terminal_name_list() const { return _disconnected_terminal_name_list; }
  const std::vector<Shape>& get_disconnected_shape_list() const { return _disconnected_shape_list; }
  // setter
  void set_net_name(const std::string& net_name) { _net_name = net_name; }
  void set_driver_terminal_name(const std::string& driver_terminal_name) { _driver_terminal_name = driver_terminal_name; }
  void set_disconnected_terminal_name_list(const std::vector<std::string>& disconnected_terminal_name_list)
  {
    _disconnected_terminal_name_list = disconnected_terminal_name_list;
  }
  void set_disconnected_shape_list(const std::vector<Shape>& disconnected_shape_list) { _disconnected_shape_list = disconnected_shape_list; }

 private:
  std::string _net_name;
  std::string _driver_terminal_name;
  std::vector<std::string> _disconnected_terminal_name_list;
  std::vector<Shape> _disconnected_shape_list;
};

}  // namespace ilvs
