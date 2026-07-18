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
// WHETHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "STAHeader.hpp"

namespace ista {

class TCDelayArcCandidate
{
 public:
  TCDelayArcCandidate() = default;
  ~TCDelayArcCandidate() = default;
  // getter
  std::string& get_source_port() { return _source_port; }
  std::string& get_sink_port() { return _sink_port; }
  std::string& get_timing_type() { return _timing_type; }
  std::string& get_timing_sense() { return _timing_sense; }
  double get_rise_delay() const { return _rise_delay; }
  double get_fall_delay() const { return _fall_delay; }
  bool get_has_rise_delay() const { return _has_rise_delay; }
  bool get_has_fall_delay() const { return _has_fall_delay; }
  // setter
  void set_source_port(const std::string& source_port) { _source_port = source_port; }
  void set_sink_port(const std::string& sink_port) { _sink_port = sink_port; }
  void set_timing_type(const std::string& timing_type) { _timing_type = timing_type; }
  void set_timing_sense(const std::string& timing_sense) { _timing_sense = timing_sense; }
  void set_rise_delay(const double rise_delay) { _rise_delay = rise_delay; }
  void set_fall_delay(const double fall_delay) { _fall_delay = fall_delay; }
  void set_has_rise_delay(const bool has_rise_delay) { _has_rise_delay = has_rise_delay; }
  void set_has_fall_delay(const bool has_fall_delay) { _has_fall_delay = has_fall_delay; }
  // function

 private:
  std::string _source_port;
  std::string _sink_port;
  std::string _timing_type;
  std::string _timing_sense;
  double _rise_delay = 0.0;
  double _fall_delay = 0.0;
  bool _has_rise_delay = false;
  bool _has_fall_delay = false;
};

}  // namespace ista
