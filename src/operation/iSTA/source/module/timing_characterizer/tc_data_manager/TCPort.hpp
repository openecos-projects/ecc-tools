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

#include "PinDirection.hpp"
#include "STAHeader.hpp"

namespace ista {

class TCPort
{
 public:
  TCPort() = default;
  ~TCPort() = default;
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

}  // namespace ista
