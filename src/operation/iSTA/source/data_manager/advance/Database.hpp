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

#include "Arc.hpp"
#include "Instance.hpp"
#include "Net.hpp"
#include "ParasiticLibrary.hpp"
#include "Pin.hpp"
#include "STAHeader.hpp"
#include "Summary.hpp"
#include "TimingConstraint.hpp"
#include "TimingLibrary.hpp"
#include "TimingPathGroup.hpp"
#include "TimingPoint.hpp"

namespace ista {

class Database
{
 public:
  Database() = default;
  ~Database() = default;
  // getter
  std::string& get_design_name() { return _design_name; }
  std::map<std::string, Instance>& get_instance_map() { return _instance_map; }
  std::map<std::string, Pin>& get_pin_map() { return _pin_map; }
  std::map<std::string, Net>& get_net_map() { return _net_map; }
  std::vector<Arc>& get_arc_list() { return _arc_list; }
  std::map<std::string, std::vector<std::size_t>>& get_outgoing_arc_list_map() { return _outgoing_arc_list_map; }
  std::map<std::string, std::vector<std::size_t>>& get_incoming_arc_list_map() { return _incoming_arc_list_map; }
  std::vector<std::string>& get_start_point_list() { return _start_point_list; }
  std::vector<std::string>& get_end_point_list() { return _end_point_list; }
  std::vector<std::string>& get_timing_order_list() { return _timing_order_list; }
  std::map<std::string, TimingPoint>& get_timing_point_map() { return _timing_point_map; }
  std::vector<TimingPathGroup>& get_timing_path_group_list() { return _timing_path_group_list; }
  TimingLibrary& get_timing_library() { return _timing_library; }
  ParasiticLibrary& get_parasitic_library() { return _parasitic_library; }
  TimingConstraint& get_timing_constraint() { return _timing_constraint; }
  Summary& get_summary() { return _summary; }
  // setter
  void set_design_name(const std::string& design_name) { _design_name = design_name; }
  // function

 private:
  std::string _design_name;
  std::map<std::string, Instance> _instance_map;
  std::map<std::string, Pin> _pin_map;
  std::map<std::string, Net> _net_map;
  std::vector<Arc> _arc_list;
  std::map<std::string, std::vector<std::size_t>> _outgoing_arc_list_map;
  std::map<std::string, std::vector<std::size_t>> _incoming_arc_list_map;
  std::vector<std::string> _start_point_list;
  std::vector<std::string> _end_point_list;
  std::vector<std::string> _timing_order_list;
  std::map<std::string, TimingPoint> _timing_point_map;
  std::vector<TimingPathGroup> _timing_path_group_list;
  TimingLibrary _timing_library;
  ParasiticLibrary _parasitic_library;
  TimingConstraint _timing_constraint;
  Summary _summary;
};

}  // namespace ista
