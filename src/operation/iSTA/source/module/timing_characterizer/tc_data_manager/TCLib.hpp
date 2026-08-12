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

#include "AnalysisType.hpp"
#include "STAHeader.hpp"
#include "TCPort.hpp"
#include "TCTimingArc.hpp"

namespace ista {

class TCLib
{
 public:
  TCLib() = default;
  ~TCLib() = default;
  // getter
  std::string& get_design_name() { return _design_name; }
  AnalysisType get_analysis_type() const { return _analysis_type; }
  double get_area() const { return _area; }
  std::map<std::string, TCPort>& get_port_map() { return _port_map; }
  std::vector<TCTimingArc>& get_timing_arc_list() { return _timing_arc_list; }
  // setter
  void set_design_name(const std::string& design_name) { _design_name = design_name; }
  void set_analysis_type(const AnalysisType& analysis_type) { _analysis_type = analysis_type; }
  void set_area(const double area) { _area = area; }
  void set_port_map(const std::map<std::string, TCPort>& port_map) { _port_map = port_map; }
  void set_timing_arc_list(const std::vector<TCTimingArc>& timing_arc_list) { _timing_arc_list = timing_arc_list; }
  // function

 private:
  std::string _design_name;
  AnalysisType _analysis_type = AnalysisType::kNone;
  double _area = 0.0;
  std::map<std::string, TCPort> _port_map;
  std::vector<TCTimingArc> _timing_arc_list;
};

}  // namespace ista
