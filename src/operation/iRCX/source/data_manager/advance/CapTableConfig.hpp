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

#include "CapTableEntry.hpp"
#include "CapTableType.hpp"

namespace ircx {

class CapTableConfig
{
 public:
  CapTableConfig() = default;
  ~CapTableConfig() = default;
  // getter
  CapTableType get_type() const { return _type; }
  std::string& get_layer_name() { return _layer_name; }
  std::string& get_over_layer_name() { return _over_layer_name; }
  std::string& get_under_layer_name() { return _under_layer_name; }
  std::vector<CapTableEntry>& get_entry_list() { return _entry_list; }
  // const getter
  const std::vector<CapTableEntry>& get_entry_list() const { return _entry_list; }
  // setter
  void set_type(CapTableType type) { _type = type; }
  void set_layer_name(const std::string& layer_name) { _layer_name = layer_name; }
  void set_over_layer_name(const std::string& over_layer_name) { _over_layer_name = over_layer_name; }
  void set_under_layer_name(const std::string& under_layer_name) { _under_layer_name = under_layer_name; }
  void set_entry_list(const std::vector<CapTableEntry>& entry_list) { _entry_list = entry_list; }
  // function

 private:
  CapTableType _type = CapTableType::kNone;
  std::string _layer_name;
  std::string _over_layer_name;
  std::string _under_layer_name;
  std::vector<CapTableEntry> _entry_list;
};

}  // namespace ircx
