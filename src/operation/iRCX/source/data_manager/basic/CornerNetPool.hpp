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

#include "CornerNetIdx.hpp"

namespace ircx {

template <typename T>
class CornerNetPool
{
 public:
  CornerNetPool() = default;
  ~CornerNetPool() = default;
  // getter
  int32_t get_corner_num() const { return _corner_num; }
  int32_t get_net_num() const { return _net_num; }
  std::vector<T>& get_item_list() { return _item_list; }
  // setter
  void set_corner_num(int32_t corner_num) { _corner_num = corner_num; }
  void set_net_num(int32_t net_num) { _net_num = net_num; }
  void set_item_list(const std::vector<T>& item_list) { _item_list = item_list; }
  // function
  void init(int32_t corner_num, int32_t net_num)
  {
    _corner_num = corner_num;
    _net_num = net_num;
    _item_list.clear();
    _item_list.resize(_corner_num * _net_num);
  }
  T& get_item(CornerNetIdx corner_net_idx) { return _item_list[get_item_idx(corner_net_idx)]; }

 private:
  int32_t get_item_idx(CornerNetIdx corner_net_idx) const
  {
    return corner_net_idx.get_corner_idx() * _net_num + corner_net_idx.get_net_idx();
  }

  int32_t _corner_num = 0;
  int32_t _net_num = 0;
  std::vector<T> _item_list;
};

}  // namespace ircx
