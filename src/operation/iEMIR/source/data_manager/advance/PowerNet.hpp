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

#include "EMIRHeader.hpp"
#include "PowerNetType.hpp"
#include "PowerPin.hpp"
#include "PowerVia.hpp"
#include "PowerWireSegment.hpp"

namespace iemir {

class PowerNet
{
 public:
  PowerNet() = default;
  ~PowerNet() = default;
  // getter
  std::string& get_net_name() { return _net_name; }
  PowerNetType get_type() { return _type; }
  std::vector<PowerWireSegment>& get_wire_segment_list() { return _wire_segment_list; }
  std::vector<PowerVia>& get_via_list() { return _via_list; }
  std::vector<PowerPin>& get_pin_list() { return _pin_list; }
  // setter
  void set_net_name(const std::string& net_name) { _net_name = net_name; }
  void set_type(PowerNetType type) { _type = type; }
  // function

 private:
  std::string _net_name;
  PowerNetType _type = PowerNetType::kNone;
  std::vector<PowerWireSegment> _wire_segment_list;
  std::vector<PowerVia> _via_list;
  std::vector<PowerPin> _pin_list;
};

}  // namespace iemir
