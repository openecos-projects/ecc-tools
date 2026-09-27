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
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "MJHeader.hpp"

namespace imj {

class DFConfig
{
 public:
  DFConfig() = default;
  ~DFConfig() = default;
  // getter
  std::vector<std::string>& get_hierarchy_def_path_list() { return _hierarchy_def_path_list; }
  // const getter
  const std::vector<std::string>& get_hierarchy_def_path_list() const { return _hierarchy_def_path_list; }
  // setter
  void set_hierarchy_def_path_list(const std::vector<std::string>& hierarchy_def_path_list)
  {
    _hierarchy_def_path_list = hierarchy_def_path_list;
  }
  // function

 private:
  std::vector<std::string> _hierarchy_def_path_list;
};

}  // namespace imj
