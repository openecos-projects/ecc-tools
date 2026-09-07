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

#include "ConnectType.hpp"
#include "LVSHeader.hpp"
#include "Net.hpp"
#include "Utility.hpp"

namespace ilvs {

class DesignData
{
 public:
  DesignData() = default;
  ~DesignData() = default;
  // getter
  std::string& get_design_name() { return _design_name; }
  std::vector<std::string>& get_io_terminal_name_list() { return _io_terminal_name_list; }
  std::map<std::string, ConnectType>& get_terminal_connect_type_map() { return _terminal_connect_type_map; }
  std::set<std::string>& get_instance_name_set() { return _instance_name_set; }
  std::map<std::string, Net>& get_net_map() { return _net_map; }
  // const getter
  const std::string& get_design_name() const { return _design_name; }
  const std::vector<std::string>& get_io_terminal_name_list() const { return _io_terminal_name_list; }
  const std::map<std::string, ConnectType>& get_terminal_connect_type_map() const { return _terminal_connect_type_map; }
  const std::set<std::string>& get_instance_name_set() const { return _instance_name_set; }
  const std::map<std::string, Net>& get_net_map() const { return _net_map; }
  // setter
  void set_design_name(const std::string& design_name) { _design_name = design_name; }
  void set_io_terminal_name_list(const std::vector<std::string>& io_terminal_name_list) { _io_terminal_name_list = io_terminal_name_list; }
  // function
  void normalize()
  {
    _io_terminal_name_list = LVSUTIL.getSortedUniqueList(_io_terminal_name_list);
    for (auto& [net_name, net] : _net_map) {
      (void) net_name;
      net.get_terminal_name_list() = LVSUTIL.getSortedUniqueList(net.get_terminal_name_list());
    }
  }
  void reset()
  {
    _design_name.clear();
    _io_terminal_name_list.clear();
    _terminal_connect_type_map.clear();
    _instance_name_set.clear();
    _net_map.clear();
  }

 private:
  std::string _design_name;
  std::vector<std::string> _io_terminal_name_list;
  std::map<std::string, ConnectType> _terminal_connect_type_map;
  std::set<std::string> _instance_name_set;
  std::map<std::string, Net> _net_map;
};

}  // namespace ilvs
