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

#include "RCXHeader.hpp"

namespace ircx {

class SWNodeCouplingKey
{
 public:
  SWNodeCouplingKey() = default;
  SWNodeCouplingKey(int32_t local_node_idx, const std::string& peer_node_name) : _local_node_idx(local_node_idx), _peer_node_name(peer_node_name) {}
  ~SWNodeCouplingKey() = default;
  // getter
  int32_t get_local_node_idx() const { return _local_node_idx; }
  std::string& get_peer_node_name() { return _peer_node_name; }
  const std::string& get_peer_node_name() const { return _peer_node_name; }
  // setter
  void set_local_node_idx(int32_t local_node_idx) { _local_node_idx = local_node_idx; }
  void set_peer_node_name(const std::string& peer_node_name) { _peer_node_name = peer_node_name; }
  // function
  bool operator<(const SWNodeCouplingKey& other) const
  {
    if (_local_node_idx != other._local_node_idx) {
      return _local_node_idx < other._local_node_idx;
    }
    return _peer_node_name < other._peer_node_name;
  }

 private:
  int32_t _local_node_idx = -1;
  std::string _peer_node_name;
};

}  // namespace ircx
