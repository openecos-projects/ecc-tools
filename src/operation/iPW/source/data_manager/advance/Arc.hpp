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
#include "PWHeader.hpp"
#include "TransType.hpp"

namespace ipw {

class TimingCellArc;

class Arc
{
 public:
  Arc() = default;
  ~Arc() = default;
  // getter
  std::string& get_source_pin() { return _source_pin; }
  std::string& get_sink_pin() { return _sink_pin; }
  std::string& get_owner_name() { return _owner_name; }
  std::string& get_library_source_port() { return _library_source_port; }
  std::string& get_library_sink_port() { return _library_sink_port; }
  ArcType get_type() const { return _type; }
  TimingCellArc* get_timing_cell_arc() { return _timing_cell_arc; }
  bool get_is_clock_arc() const { return _is_clock_arc; }
  bool get_is_disable_arc() const { return _is_disable_arc || _is_case_analysis_disable; }
  bool get_is_loop_disable() const { return _is_loop_disable; }
  // setter
  void set_source_pin(const std::string& source_pin) { _source_pin = source_pin; }
  void set_sink_pin(const std::string& sink_pin) { _sink_pin = sink_pin; }
  void set_owner_name(const std::string& owner_name) { _owner_name = owner_name; }
  void set_library_source_port(const std::string& library_source_port) { _library_source_port = library_source_port; }
  void set_library_sink_port(const std::string& library_sink_port) { _library_sink_port = library_sink_port; }
  void set_type(const ArcType& type) { _type = type; }
  void set_timing_cell_arc(TimingCellArc* timing_cell_arc) { _timing_cell_arc = timing_cell_arc; }
  void set_is_clock_arc(const bool is_clock_arc) { _is_clock_arc = is_clock_arc; }
  void set_is_disable_arc(const bool is_disable_arc) { _is_disable_arc = is_disable_arc; }
  void set_is_case_analysis_disable(const bool value) { _is_case_analysis_disable = value; }
  void set_is_loop_disable(const bool is_loop_disable) { _is_loop_disable = is_loop_disable; }
 private:
  std::string _source_pin;
  std::string _sink_pin;
  std::string _owner_name;
  std::string _library_source_port;
  std::string _library_sink_port;
  ArcType _type = ArcType::kNone;
  TimingCellArc* _timing_cell_arc = nullptr;
  bool _is_clock_arc = false;
  bool _is_disable_arc = false;
  bool _is_case_analysis_disable = false;
  bool _is_loop_disable = false;
};

}  // namespace ipw
