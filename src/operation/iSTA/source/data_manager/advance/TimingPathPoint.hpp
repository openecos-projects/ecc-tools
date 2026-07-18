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

#include "ArcType.hpp"
#include "STAHeader.hpp"
#include "TransType.hpp"

namespace ista {

class TimingPathPoint
{
 public:
  TimingPathPoint() = default;
  ~TimingPathPoint() = default;
  // getter
  std::string& get_pin_name() { return _pin_name; }
  std::string& get_instance_name() { return _instance_name; }
  std::string& get_cell_name() { return _cell_name; }
  std::string& get_net_name() { return _net_name; }
  std::string& get_arc_name() { return _arc_name; }
  std::string& get_source_pin() { return _source_pin; }
  std::string& get_sink_pin() { return _sink_pin; }
  ArcType get_arc_type() const { return _arc_type; }
  double get_arc_delay() const { return _arc_delay; }
  double get_arrival() const { return _arrival; }
  double get_required() const { return _required; }
  double get_slack() const { return _slack; }
  TransType get_trans_type() const { return _trans_type; }
  // setter
  void set_pin_name(const std::string& pin_name) { _pin_name = pin_name; }
  void set_instance_name(const std::string& instance_name) { _instance_name = instance_name; }
  void set_cell_name(const std::string& cell_name) { _cell_name = cell_name; }
  void set_net_name(const std::string& net_name) { _net_name = net_name; }
  void set_arc_name(const std::string& arc_name) { _arc_name = arc_name; }
  void set_source_pin(const std::string& source_pin) { _source_pin = source_pin; }
  void set_sink_pin(const std::string& sink_pin) { _sink_pin = sink_pin; }
  void set_arc_type(const ArcType& arc_type) { _arc_type = arc_type; }
  void set_arc_delay(const double arc_delay) { _arc_delay = arc_delay; }
  void set_arrival(const double arrival) { _arrival = arrival; }
  void set_required(const double required) { _required = required; }
  void set_slack(const double slack) { _slack = slack; }
  void set_trans_type(const TransType& trans_type) { _trans_type = trans_type; }
  // function

 private:
  std::string _pin_name;
  std::string _instance_name;
  std::string _cell_name;
  std::string _net_name;
  std::string _arc_name;
  std::string _source_pin;
  std::string _sink_pin;
  ArcType _arc_type = ArcType::kNone;
  double _arc_delay = 0.0;
  double _arrival = 0.0;
  double _required = 0.0;
  double _slack = 0.0;
  TransType _trans_type = TransType::kNone;
};

}  // namespace ista
