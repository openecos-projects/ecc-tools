// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of the Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "EMIRHeader.hpp"

namespace iemir {

class EAModel
{
 public:
  EAModel() = default;
  ~EAModel() = default;
  // getter
  std::size_t get_power_edge_num() { return _power_edge_num; }
  double get_total_current() { return _total_current; }
  double get_max_current() { return _max_current; }
  // setter
  void set_power_edge_num(std::size_t power_edge_num) { _power_edge_num = power_edge_num; }
  void set_total_current(double total_current) { _total_current = total_current; }
  void set_max_current(double max_current) { _max_current = max_current; }
  // function

 private:
  std::size_t _power_edge_num = 0;
  double _total_current = 0.0;
  double _max_current = 0.0;
};

}  // namespace iemir
