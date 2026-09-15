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

class TimingFanoutCheck
{
 public:
  TimingFanoutCheck() = default;
  ~TimingFanoutCheck() = default;
  // getter
  const std::string& get_net_name() const { return _net_name; }
  const std::string& get_driver_pin() const { return _driver_pin; }
  double get_fanout_load() const { return _fanout_load; }
  double get_limit() const { return _limit; }
  double get_slack() const { return _limit - _fanout_load; }
  // setter
  void set_net_name(const std::string& net_name) { _net_name = net_name; }
  void set_driver_pin(const std::string& driver_pin) { _driver_pin = driver_pin; }
  void set_fanout_load(double fanout_load) { _fanout_load = fanout_load; }
  void set_limit(double limit) { _limit = limit; }
  // function

 private:
  std::string _net_name;
  std::string _driver_pin;
  double _fanout_load = 0.0;
  double _limit = 0.0;
};

}  // namespace ista
