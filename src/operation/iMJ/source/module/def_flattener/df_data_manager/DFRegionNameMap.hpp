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

class DFRegionNameMap
{
 public:
  DFRegionNameMap() = default;
  ~DFRegionNameMap() = default;
  // getter
  std::map<std::string, std::string>& get_source_to_output_name_map() { return _source_to_output_name_map; }
  std::string get_output_name(const std::string& source_name) const
  {
    std::map<std::string, std::string>::const_iterator iter = _source_to_output_name_map.find(source_name);
    return iter == _source_to_output_name_map.end() ? "" : iter->second;
  }
  // setter
  void set_output_name(const std::string& source_name, const std::string& output_name) { _source_to_output_name_map[source_name] = output_name; }
  // function

 private:
  std::map<std::string, std::string> _source_to_output_name_map;
};

}  // namespace imj
