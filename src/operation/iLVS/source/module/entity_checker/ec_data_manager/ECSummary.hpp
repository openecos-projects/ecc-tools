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
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "LVSHeader.hpp"
#include "Violation.hpp"

namespace ilvs {

class ECSummary
{
 public:
  ECSummary() = default;
  ~ECSummary() = default;
  int64_t netlist_io_num = 0;
  int64_t def_io_num = 0;
  int64_t io_difference_num = 0;
  int64_t netlist_instance_num = 0;
  int64_t def_instance_num = 0;
  int64_t instance_difference_num = 0;
  int64_t netlist_net_num = 0;
  int64_t def_net_num = 0;
  int64_t net_difference_num = 0;
  std::vector<Violation> violation_list;

  void reset() { *this = {}; }
};

}  // namespace ilvs
