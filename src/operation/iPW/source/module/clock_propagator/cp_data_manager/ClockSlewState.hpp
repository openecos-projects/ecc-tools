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
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "AnalysisType.hpp"
#include "PWHeader.hpp"
#include "TransType.hpp"

namespace ipw {

class ClockSlewState
{
 public:
  ClockSlewState() = default;
  ~ClockSlewState() = default;
  // getter
  std::map<AnalysisType, std::map<TransType, double>>& get_slew_map() { return _slew_map; }
  std::map<AnalysisType, std::map<TransType, double>>& get_physical_slew_map() { return _physical_slew_map; }
  const std::map<AnalysisType, std::map<TransType, double>>& get_physical_slew_map() const { return _physical_slew_map; }
  // setter
  void set_slew_map(const std::map<AnalysisType, std::map<TransType, double>>& slew_map) { _slew_map = slew_map; }
  void set_physical_slew_map(const std::map<AnalysisType, std::map<TransType, double>>& physical_slew_map)
  {
    _physical_slew_map = physical_slew_map;
  }
  // function

 private:
  std::map<AnalysisType, std::map<TransType, double>> _slew_map;
  std::map<AnalysisType, std::map<TransType, double>> _physical_slew_map;
};

}  // namespace ipw
