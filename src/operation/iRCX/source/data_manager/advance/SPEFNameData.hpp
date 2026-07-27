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

#include "Direction.hpp"
#include "RCXHeader.hpp"

namespace ircx {

class SPEFNameData
{
 public:
  SPEFNameData() = default;
  ~SPEFNameData() = default;
  // getter
  std::vector<std::string>& get_net_name_list() { return _net_name_list; }
  std::vector<std::string>& get_port_name_list() { return _port_name_list; }
  std::vector<Direction>& get_port_direction_list() { return _port_direction_list; }
  std::vector<std::string>& get_instance_name_list() { return _instance_name_list; }
  std::map<std::string, std::string>& get_instance_name_to_cell_name_map() { return _instance_name_to_cell_name_map; }
  // setter
  void set_net_name_list(const std::vector<std::string>& net_name_list) { _net_name_list = net_name_list; }
  void set_port_name_list(const std::vector<std::string>& port_name_list) { _port_name_list = port_name_list; }
  void set_port_direction_list(const std::vector<Direction>& port_direction_list) { _port_direction_list = port_direction_list; }
  void set_instance_name_list(const std::vector<std::string>& instance_name_list) { _instance_name_list = instance_name_list; }
  void set_instance_name_to_cell_name_map(const std::map<std::string, std::string>& instance_name_to_cell_name_map)
  {
    _instance_name_to_cell_name_map = instance_name_to_cell_name_map;
  }
  // function

 private:
  std::vector<std::string> _net_name_list;
  std::vector<std::string> _port_name_list;
  std::vector<Direction> _port_direction_list;
  std::vector<std::string> _instance_name_list;
  std::map<std::string, std::string> _instance_name_to_cell_name_map;
};

}  // namespace ircx
