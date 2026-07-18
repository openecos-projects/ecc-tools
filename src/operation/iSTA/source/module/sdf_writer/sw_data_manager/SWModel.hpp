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

#include "STAHeader.hpp"

namespace ista {

class Arc;

class SWModel
{
 public:
  SWModel() = default;
  ~SWModel() = default;
  // getter
  std::map<std::string, std::vector<Arc*>>& get_instance_cell_arc_map() { return _instance_cell_arc_map; }
  // setter
  void set_instance_cell_arc_map(const std::map<std::string, std::vector<Arc*>>& instance_cell_arc_map)
  {
    _instance_cell_arc_map = instance_cell_arc_map;
  }
  // function

 private:
  std::map<std::string, std::vector<Arc*>> _instance_cell_arc_map;
};

}  // namespace ista
