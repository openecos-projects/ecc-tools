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
#include "ViolationType.hpp"

namespace ilvs {

class Violation
{
 public:
  Violation() = default;
  ~Violation() = default;
  // getter
  ViolationType get_violation_type() const { return _violation_type; }
  std::string& get_net_name() { return _net_name; }
  std::vector<std::string>& get_terminal_name_list() { return _terminal_name_list; }
  std::vector<int32_t>& get_component_id_list() { return _component_id_list; }
  std::vector<std::string>& get_related_net_name_list() { return _related_net_name_list; }
  std::string& get_instance_name() { return _instance_name; }
  std::string& get_driver_terminal_name() { return _driver_terminal_name; }
  std::vector<Shape>& get_shape_list() { return _shape_list; }
  // const getter
  const std::string& get_net_name() const { return _net_name; }
  const std::vector<std::string>& get_terminal_name_list() const { return _terminal_name_list; }
  const std::vector<int32_t>& get_component_id_list() const { return _component_id_list; }
  const std::vector<std::string>& get_related_net_name_list() const { return _related_net_name_list; }
  const std::string& get_instance_name() const { return _instance_name; }
  const std::string& get_driver_terminal_name() const { return _driver_terminal_name; }
  const std::vector<Shape>& get_shape_list() const { return _shape_list; }
  // setter
  void set_violation_type(const ViolationType violation_type) { _violation_type = violation_type; }
  void set_net_name(const std::string& net_name) { _net_name = net_name; }
  void set_terminal_name_list(const std::vector<std::string>& terminal_name_list) { _terminal_name_list = terminal_name_list; }
  void set_component_id_list(const std::vector<int32_t>& component_id_list) { _component_id_list = component_id_list; }
  void set_related_net_name_list(const std::vector<std::string>& related_net_name_list)
  {
    _related_net_name_list = related_net_name_list;
  }
  void set_instance_name(const std::string& instance_name) { _instance_name = instance_name; }
  void set_driver_terminal_name(const std::string& driver_terminal_name) { _driver_terminal_name = driver_terminal_name; }
  void set_shape_list(const std::vector<Shape>& shape_list) { _shape_list = shape_list; }

 private:
  ViolationType _violation_type = ViolationType::kNone;
  std::string _net_name;
  std::vector<std::string> _terminal_name_list;
  std::vector<int32_t> _component_id_list;
  std::vector<std::string> _related_net_name_list;
  std::string _instance_name;
  std::string _driver_terminal_name;
  std::vector<Shape> _shape_list;
};

}  // namespace ilvs
