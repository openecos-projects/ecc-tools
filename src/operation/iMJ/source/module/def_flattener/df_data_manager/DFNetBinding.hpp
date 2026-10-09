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

#include "IdbNet.h"
#include "IdbSpecialNet.h"
#include "MJHeader.hpp"

namespace imj {

class DFNetBinding
{
 public:
  DFNetBinding() = default;
  ~DFNetBinding() = default;
  // getter
  std::map<idb::IdbNet*, std::string>& get_regular_net_to_output_name_map() { return _regular_net_to_output_name_map; }
  std::map<idb::IdbSpecialNet*, std::string>& get_special_net_to_output_name_map() { return _special_net_to_output_name_map; }
  std::string get_regular_net_name(idb::IdbNet* net)
  {
    std::map<idb::IdbNet*, std::string>::iterator iter = _regular_net_to_output_name_map.find(net);
    return iter == _regular_net_to_output_name_map.end() ? "" : iter->second;
  }
  std::string get_special_net_name(idb::IdbSpecialNet* net)
  {
    std::map<idb::IdbSpecialNet*, std::string>::iterator iter = _special_net_to_output_name_map.find(net);
    return iter == _special_net_to_output_name_map.end() ? "" : iter->second;
  }
  // setter
  bool set_regular_net_name(idb::IdbNet* net, const std::string& output_net_name)
  {
    std::map<idb::IdbNet*, std::string>::iterator iter = _regular_net_to_output_name_map.find(net);
    if (iter != _regular_net_to_output_name_map.end() && iter->second != output_net_name) {
      return false;
    }
    _regular_net_to_output_name_map[net] = output_net_name;
    return true;
  }
  bool set_special_net_name(idb::IdbSpecialNet* net, const std::string& output_net_name)
  {
    std::map<idb::IdbSpecialNet*, std::string>::iterator iter = _special_net_to_output_name_map.find(net);
    if (iter != _special_net_to_output_name_map.end() && iter->second != output_net_name) {
      return false;
    }
    _special_net_to_output_name_map[net] = output_net_name;
    return true;
  }
  // function

 private:
  std::map<idb::IdbNet*, std::string> _regular_net_to_output_name_map;
  std::map<idb::IdbSpecialNet*, std::string> _special_net_to_output_name_map;
};

}  // namespace imj
