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

class Net
{
 public:
  Net() = default;
  ~Net() = default;
  // getter
  std::string& get_net_name() { return _net_name; }
  std::string& get_driver_pin() { return _driver_pin; }
  std::vector<std::string>& get_driver_pin_list() { return _driver_pin_list; }
  std::vector<std::string>& get_load_pin_list() { return _load_pin_list; }
  std::vector<std::string>& get_pin_name_list() { return _pin_name_list; }
  // setter
  void set_net_name(const std::string& net_name) { _net_name = net_name; }
  void set_driver_pin(const std::string& driver_pin) { _driver_pin = driver_pin; }
  void set_driver_pin_list(const std::vector<std::string>& driver_pin_list) { _driver_pin_list = driver_pin_list; }
  void set_load_pin_list(const std::vector<std::string>& load_pin_list) { _load_pin_list = load_pin_list; }
  void set_pin_name_list(const std::vector<std::string>& pin_name_list) { _pin_name_list = pin_name_list; }
  // function

 private:
  std::string _net_name;
  std::string _driver_pin;
  std::vector<std::string> _driver_pin_list;
  std::vector<std::string> _load_pin_list;
  std::vector<std::string> _pin_name_list;
};

}  // namespace ista
