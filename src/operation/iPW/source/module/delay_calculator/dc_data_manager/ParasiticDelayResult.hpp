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
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "PWHeader.hpp"

namespace ipw {

class ParasiticDelayResult
{
 public:
  ParasiticDelayResult() = default;
  ~ParasiticDelayResult() = default;
  // getter
  bool get_is_valid() const { return _is_valid; }
  double get_gate_delay() const { return _gate_delay; }
  double get_driver_slew() const { return _driver_slew; }
  std::map<std::string, double>& get_wire_delay_map() { return _wire_delay_map; }
  std::map<std::string, double>& get_load_slew_map() { return _load_slew_map; }
  // setter
  void set_is_valid(bool is_valid) { _is_valid = is_valid; }
  void set_gate_delay(double gate_delay) { _gate_delay = gate_delay; }
  void set_driver_slew(double driver_slew) { _driver_slew = driver_slew; }
  // function

 private:
  bool _is_valid = false;
  double _gate_delay = 0.0;
  double _driver_slew = 0.0;
  std::map<std::string, double> _wire_delay_map;
  std::map<std::string, double> _load_slew_map;
};

}  // namespace ipw
