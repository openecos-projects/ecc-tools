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

#include "DesignData.hpp"

namespace ilvs {

class NetlistData : public DesignData
{
 public:
  NetlistData() = default;

  // Provenance supplied by the logical importer. Keys are canonical logical
  // net names; values are the original symbols safely folded into that net.
  std::unordered_map<std::string, std::vector<std::string>>& get_net_alias_map() { return _net_alias_map; }
  const std::unordered_map<std::string, std::vector<std::string>>& get_net_alias_map() const { return _net_alias_map; }
  void reset()
  {
    DesignData::reset();
    _net_alias_map.clear();
  }

 private:
  std::unordered_map<std::string, std::vector<std::string>> _net_alias_map;
};

}  // namespace ilvs
