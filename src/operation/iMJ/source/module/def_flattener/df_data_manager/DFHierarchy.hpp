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
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "MJHeader.hpp"

namespace imj {

class DFHierarchy
{
 public:
  DFHierarchy() = default;
  ~DFHierarchy() = default;
  // getter
  std::string& get_root_master_name() { return _root_master_name; }
  std::map<std::string, std::string>& get_child_master_to_parent_master_name_map()
  {
    return _child_master_to_parent_master_name_map;
  }
  std::vector<std::string>& get_bottom_up_master_name_list() { return _bottom_up_master_name_list; }
  // const getter
  const std::string& get_root_master_name() const { return _root_master_name; }
  const std::map<std::string, std::string>& get_child_master_to_parent_master_name_map() const
  {
    return _child_master_to_parent_master_name_map;
  }
  const std::vector<std::string>& get_bottom_up_master_name_list() const { return _bottom_up_master_name_list; }
  std::string get_parent_master_name(const std::string& child_master_name) const
  {
    std::map<std::string, std::string>::const_iterator iter = _child_master_to_parent_master_name_map.find(child_master_name);
    return iter == _child_master_to_parent_master_name_map.end() ? "" : iter->second;
  }
  // setter
  void set_root_master_name(const std::string& root_master_name) { _root_master_name = root_master_name; }
  // function
  bool add_parent_master_name(const std::string& child_master_name, const std::string& parent_master_name)
  {
    std::map<std::string, std::string>::iterator iter = _child_master_to_parent_master_name_map.find(child_master_name);
    if (iter == _child_master_to_parent_master_name_map.end()) {
      _child_master_to_parent_master_name_map[child_master_name] = parent_master_name;
      return true;
    }
    return iter->second == parent_master_name;
  }
  void add_bottom_up_master_name(const std::string& master_name) { _bottom_up_master_name_list.push_back(master_name); }

 private:
  std::string _root_master_name;
  std::map<std::string, std::string> _child_master_to_parent_master_name_map;
  std::vector<std::string> _bottom_up_master_name_list;
};

}  // namespace imj
