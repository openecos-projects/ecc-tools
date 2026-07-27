// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of the License at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "RCXHeader.hpp"

namespace ircx {

class CapTableEntry
{
 public:
  CapTableEntry() = default;
  ~CapTableEntry() = default;
  // getter
  double get_spacing() const { return _spacing; }
  double get_coupling_capacitance() const { return _coupling_capacitance; }
  double get_ground_capacitance() const { return _ground_capacitance; }
  // setter
  void set_spacing(double spacing) { _spacing = spacing; }
  void set_coupling_capacitance(double coupling_capacitance) { _coupling_capacitance = coupling_capacitance; }
  void set_ground_capacitance(double ground_capacitance) { _ground_capacitance = ground_capacitance; }
  // function

 private:
  double _spacing = -1.0;
  double _coupling_capacitance = -1.0;
  double _ground_capacitance = -1.0;
};

}  // namespace ircx
