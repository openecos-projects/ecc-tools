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

#include "FPHeader.hpp"
#include "NetPin.hpp"

namespace ifp {

class Net
{
 public:
  Net() = default;
  ~Net() = default;
  // getter
  std::string& get_name() { return _name; }
  std::vector<NetPin>& get_net_pin_list() { return _net_pin_list; }
  int32_t get_pin_num() const { return _pin_num; }
  bool get_clock() const { return _clock; }
  bool get_pdn() const { return _pdn; }
  bool get_power() const { return _power; }
  bool get_ground() const { return _ground; }

  // const getter
  const std::string& get_name() const { return _name; }
  const std::vector<NetPin>& get_net_pin_list() const { return _net_pin_list; }

  // setter
  void set_name(std::string name) { _name = name; }
  void set_net_pin_list(std::vector<NetPin> net_pin_list) { _net_pin_list = net_pin_list; }
  void set_pin_num(int32_t pin_num) { _pin_num = pin_num; }
  void set_clock(bool clock) { _clock = clock; }
  void set_pdn(bool pdn) { _pdn = pdn; }
  void set_power(bool power) { _power = power; }
  void set_ground(bool ground) { _ground = ground; }

  // function

 private:
  std::string _name;
  std::vector<NetPin> _net_pin_list;
  int32_t _pin_num = -1;
  bool _clock = false;
  bool _pdn = false;
  bool _power = false;
  bool _ground = false;
};

}  // namespace ifp
