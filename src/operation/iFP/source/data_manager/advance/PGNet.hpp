// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
//
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "FPHeader.hpp"
#include "IOPinDirection.hpp"
#include "PGNetType.hpp"

namespace ifp {

class PGNet
{
 public:
  PGNet() = default;
  ~PGNet() = default;
  // getter
  std::string& get_name() { return _name; }
  PGNetType get_type() const { return _type; }
  std::map<std::string, IOPinDirection>& get_io_pin_name_to_direction_map() { return _io_pin_name_to_direction_map; }
  std::vector<std::string>& get_instance_pin_name_list() { return _instance_pin_name_list; }

  // const getter
  const std::string& get_name() const { return _name; }
  const std::map<std::string, IOPinDirection>& get_io_pin_name_to_direction_map() const { return _io_pin_name_to_direction_map; }
  const std::vector<std::string>& get_instance_pin_name_list() const { return _instance_pin_name_list; }

  // setter
  void set_name(std::string name) { _name = name; }
  void set_type(PGNetType type) { _type = type; }
  void set_io_pin_name_to_direction_map(const std::map<std::string, IOPinDirection>& io_pin_name_to_direction_map)
  {
    _io_pin_name_to_direction_map = io_pin_name_to_direction_map;
  }
  void set_instance_pin_name_list(const std::vector<std::string>& instance_pin_name_list)
  {
    _instance_pin_name_list = instance_pin_name_list;
  }

  // function
  void add_io_pin(std::string pin_name, IOPinDirection direction) { _io_pin_name_to_direction_map[pin_name] = direction; }
  void add_instance_pin(std::string pin_name) { _instance_pin_name_list.push_back(pin_name); }

 private:
  std::string _name;
  PGNetType _type = PGNetType::kNone;
  std::map<std::string, IOPinDirection> _io_pin_name_to_direction_map;
  std::vector<std::string> _instance_pin_name_list;
};

}  // namespace ifp
