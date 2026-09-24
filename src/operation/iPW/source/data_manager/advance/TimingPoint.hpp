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
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "PWHeader.hpp"

namespace ipw {

class TimingPoint
{
 public:
  TimingPoint() = default;
  ~TimingPoint() = default;
  // getter
  std::map<AnalysisType, std::map<TransType, double>>& get_clock_slew_map() { return _clock_slew_map; }
  std::map<AnalysisType, std::map<TransType, double>>& get_data_slew_map() { return _data_slew_map; }
  bool get_is_clock_point() const { return _is_clock_point; }
  // setter
  void set_clock_slew_map(const std::map<AnalysisType, std::map<TransType, double>>& clock_slew_map) { _clock_slew_map = clock_slew_map; }
  void set_is_clock_point(const bool is_clock_point) { _is_clock_point = is_clock_point; }
  // function

 private:
  std::map<AnalysisType, std::map<TransType, double>> _clock_slew_map;
  std::map<AnalysisType, std::map<TransType, double>> _data_slew_map;
  bool _is_clock_point = false;
};

}  // namespace ipw
