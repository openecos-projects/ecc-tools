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

#include "RCXHeader.hpp"

namespace ircx {

class LayerTable
{
 public:
  LayerTable() = default;
  ~LayerTable() = default;
  // getter
  std::unordered_map<int32_t, std::string>& get_design_idx_to_name_map() { return _design_idx_to_name_map; }
  std::unordered_map<std::string, int32_t>& get_design_name_to_idx_map() { return _design_name_to_idx_map; }
  std::unordered_map<int32_t, std::string>& get_process_idx_to_name_map() { return _process_idx_to_name_map; }
  std::unordered_map<std::string, int32_t>& get_process_name_to_idx_map() { return _process_name_to_idx_map; }
  std::unordered_map<std::string, std::string>& get_design_name_to_process_name_map() { return _design_name_to_process_name_map; }
  std::unordered_map<std::string, std::string>& get_process_name_to_design_name_map() { return _process_name_to_design_name_map; }
  // setter
  void set_design_idx_to_name_map(const std::unordered_map<int32_t, std::string>& design_idx_to_name_map)
  {
    _design_idx_to_name_map = design_idx_to_name_map;
  }
  void set_design_name_to_idx_map(const std::unordered_map<std::string, int32_t>& design_name_to_idx_map)
  {
    _design_name_to_idx_map = design_name_to_idx_map;
  }
  void set_process_idx_to_name_map(const std::unordered_map<int32_t, std::string>& process_idx_to_name_map)
  {
    _process_idx_to_name_map = process_idx_to_name_map;
  }
  void set_process_name_to_idx_map(const std::unordered_map<std::string, int32_t>& process_name_to_idx_map)
  {
    _process_name_to_idx_map = process_name_to_idx_map;
  }
  void set_design_name_to_process_name_map(const std::unordered_map<std::string, std::string>& design_name_to_process_name_map)
  {
    _design_name_to_process_name_map = design_name_to_process_name_map;
  }
  void set_process_name_to_design_name_map(const std::unordered_map<std::string, std::string>& process_name_to_design_name_map)
  {
    _process_name_to_design_name_map = process_name_to_design_name_map;
  }
  // function
  void register_design_layer(int32_t design_idx, const std::string& design_name)
  {
    _design_idx_to_name_map[design_idx] = design_name;
    _design_name_to_idx_map[design_name] = design_idx;
  }
  void register_process_layer(int32_t process_idx, const std::string& process_name)
  {
    _process_idx_to_name_map[process_idx] = process_name;
    _process_name_to_idx_map[process_name] = process_idx;
  }
  void register_mapping(const std::string& design_name, const std::string& process_name)
  {
    _design_name_to_process_name_map[design_name] = process_name;
    _process_name_to_design_name_map[process_name] = design_name;
  }
  int32_t get_design_idx(const std::string& design_name) { return _design_name_to_idx_map[design_name]; }
  std::string& get_design_name(int32_t design_idx) { return _design_idx_to_name_map[design_idx]; }
  int32_t get_process_idx(const std::string& process_name) { return _process_name_to_idx_map[process_name]; }
  std::string& get_process_name(int32_t process_idx) { return _process_idx_to_name_map[process_idx]; }
  int32_t get_process_idx_by_design_idx(int32_t design_idx)
  {
    return _process_name_to_idx_map[_design_name_to_process_name_map[_design_idx_to_name_map[design_idx]]];
  }
  int32_t get_design_idx_by_process_idx(int32_t process_idx)
  {
    return _design_name_to_idx_map[_process_name_to_design_name_map[_process_idx_to_name_map[process_idx]]];
  }

 private:
  std::unordered_map<int32_t, std::string> _design_idx_to_name_map;
  std::unordered_map<std::string, int32_t> _design_name_to_idx_map;
  std::unordered_map<int32_t, std::string> _process_idx_to_name_map;
  std::unordered_map<std::string, int32_t> _process_name_to_idx_map;
  std::unordered_map<std::string, std::string> _design_name_to_process_name_map;
  std::unordered_map<std::string, std::string> _process_name_to_design_name_map;
};

}  // namespace ircx
