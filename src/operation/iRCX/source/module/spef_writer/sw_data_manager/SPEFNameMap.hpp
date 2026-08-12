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

class SPEFNameMap
{
 public:
  SPEFNameMap() = default;
  ~SPEFNameMap() = default;
  // getter
  std::unordered_map<std::string, int32_t>& get_net_name_to_idx_map() { return _net_name_to_idx_map; }
  std::unordered_map<std::string, int32_t>& get_port_name_to_idx_map() { return _port_name_to_idx_map; }
  std::unordered_map<std::string, int32_t>& get_instance_name_to_idx_map() { return _instance_name_to_idx_map; }
  std::map<int32_t, std::string>& get_idx_to_net_name_map() { return _idx_to_net_name_map; }
  std::map<int32_t, std::string>& get_idx_to_port_name_map() { return _idx_to_port_name_map; }
  std::map<int32_t, std::string>& get_idx_to_instance_name_map() { return _idx_to_instance_name_map; }
  int32_t get_next_idx() const { return _next_idx; }
  // setter
  void set_next_idx(int32_t next_idx) { _next_idx = next_idx; }
  // function

 private:
  std::unordered_map<std::string, int32_t> _net_name_to_idx_map;
  std::unordered_map<std::string, int32_t> _port_name_to_idx_map;
  std::unordered_map<std::string, int32_t> _instance_name_to_idx_map;
  std::map<int32_t, std::string> _idx_to_net_name_map;
  std::map<int32_t, std::string> _idx_to_port_name_map;
  std::map<int32_t, std::string> _idx_to_instance_name_map;
  int32_t _next_idx = 1;
};

}  // namespace ircx
