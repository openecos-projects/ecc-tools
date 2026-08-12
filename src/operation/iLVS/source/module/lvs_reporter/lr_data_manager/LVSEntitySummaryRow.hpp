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

namespace ilvs {

class LVSEntitySummaryRow
{
 public:
  LVSEntitySummaryRow() = default;
  ~LVSEntitySummaryRow() = default;
  // getter
  std::string& get_entity() { return _entity; }
  int64_t get_netlist_num() const { return _netlist_num; }
  int64_t get_def_num() const { return _def_num; }
  int64_t get_difference_num() const { return _difference_num; }
  // const getter
  const std::string& get_entity() const { return _entity; }
  // setter
  void set_entity(const std::string& entity) { _entity = entity; }
  void set_netlist_num(const int64_t netlist_num) { _netlist_num = netlist_num; }
  void set_def_num(const int64_t def_num) { _def_num = def_num; }
  void set_difference_num(const int64_t difference_num) { _difference_num = difference_num; }

 private:
  std::string _entity;
  int64_t _netlist_num = 0;
  int64_t _def_num = 0;
  int64_t _difference_num = 0;
};

}  // namespace ilvs
