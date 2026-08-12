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

#include "RCXHeader.hpp"

namespace ircx {

class CouplingCapEntry
{
 public:
  CouplingCapEntry() = default;
  CouplingCapEntry(int32_t first_edge_idx, int32_t second_edge_idx, int32_t corner_idx, double coupling_capacitance)
  {
    _first_edge_idx = first_edge_idx;
    _second_edge_idx = second_edge_idx;
    _corner_idx = corner_idx;
    _coupling_capacitance = coupling_capacitance;
  }
  ~CouplingCapEntry() = default;
  // getter
  int32_t get_first_edge_idx() const { return _first_edge_idx; }
  int32_t get_second_edge_idx() const { return _second_edge_idx; }
  int32_t get_corner_idx() const { return _corner_idx; }
  double get_coupling_capacitance() const { return _coupling_capacitance; }
  // setter
  void set_first_edge_idx(int32_t first_edge_idx) { _first_edge_idx = first_edge_idx; }
  void set_second_edge_idx(int32_t second_edge_idx) { _second_edge_idx = second_edge_idx; }
  void set_corner_idx(int32_t corner_idx) { _corner_idx = corner_idx; }
  void set_coupling_capacitance(double coupling_capacitance) { _coupling_capacitance = coupling_capacitance; }
  // function

 private:
  int32_t _first_edge_idx = -1;
  int32_t _second_edge_idx = -1;
  int32_t _corner_idx = -1;
  double _coupling_capacitance = -1.0;
};

}  // namespace ircx
