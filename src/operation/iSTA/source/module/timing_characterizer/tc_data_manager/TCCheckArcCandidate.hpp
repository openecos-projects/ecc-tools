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

class TCCheckArcCandidate
{
 public:
  TCCheckArcCandidate() = default;
  ~TCCheckArcCandidate() = default;
  // getter
  std::string& get_source_port() { return _source_port; }
  std::string& get_sink_port() { return _sink_port; }
  std::string& get_timing_type() { return _timing_type; }
  double get_rise_constraint() const { return _rise_constraint; }
  double get_fall_constraint() const { return _fall_constraint; }
  bool get_has_rise_constraint() const { return _has_rise_constraint; }
  bool get_has_fall_constraint() const { return _has_fall_constraint; }
  // setter
  void set_source_port(const std::string& source_port) { _source_port = source_port; }
  void set_sink_port(const std::string& sink_port) { _sink_port = sink_port; }
  void set_timing_type(const std::string& timing_type) { _timing_type = timing_type; }
  void set_rise_constraint(const double rise_constraint) { _rise_constraint = rise_constraint; }
  void set_fall_constraint(const double fall_constraint) { _fall_constraint = fall_constraint; }
  void set_has_rise_constraint(const bool has_rise_constraint) { _has_rise_constraint = has_rise_constraint; }
  void set_has_fall_constraint(const bool has_fall_constraint) { _has_fall_constraint = has_fall_constraint; }
  // function

 private:
  std::string _source_port;
  std::string _sink_port;
  std::string _timing_type;
  double _rise_constraint = 0.0;
  double _fall_constraint = 0.0;
  bool _has_rise_constraint = false;
  bool _has_fall_constraint = false;
};

}  // namespace ista
