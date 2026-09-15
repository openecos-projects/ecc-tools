// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
// ***************************************************************************************
#pragma once

#include "ZHHeader.hpp"

namespace izh {

class AFComParam
{
 public:
  bool get_enable_fix() const { return _enable_fix; }
  int32_t get_max_iter() const { return _max_iter; }
  const std::vector<std::string>& get_diode_name_list() const { return _diode_name_list; }
  std::vector<std::string>& get_diode_name_list() { return _diode_name_list; }
  const std::string& get_report_dir() const { return _report_dir; }
  std::string& get_report_dir() { return _report_dir; }
  int32_t get_search_radius() const { return _search_radius; }
  int32_t get_max_jog() const { return _max_jog; }
  bool get_enable_drc() const { return _enable_drc; }

  void set_enable_fix(const bool enable_fix) { _enable_fix = enable_fix; }
  void set_max_iter(const int32_t max_iter) { _max_iter = max_iter; }
  void set_report_dir(const std::string& report_dir) { _report_dir = report_dir; }
  void set_search_radius(const int32_t search_radius) { _search_radius = search_radius; }
  void set_max_jog(const int32_t max_jog) { _max_jog = max_jog; }
  void set_enable_drc(const bool enable_drc) { _enable_drc = enable_drc; }

 private:
  bool _enable_fix = true;
  int32_t _max_iter = 3;
  std::vector<std::string> _diode_name_list;
  std::string _report_dir;
  int32_t _search_radius = 0;
  int32_t _max_jog = 0;
  bool _enable_drc = false;
};

}  // namespace izh
