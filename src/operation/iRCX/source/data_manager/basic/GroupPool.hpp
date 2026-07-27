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

#include "OffsetRange.hpp"

namespace ircx {

template <typename Item>
class GroupPool
{
 public:
  GroupPool() = default;
  ~GroupPool() = default;
  // getter
  std::vector<Item>& get_item_list() { return _item_list; }
  std::vector<OffsetRange>& get_item_range_list() { return _item_range_list; }
  // setter
  void set_item_list(const std::vector<Item>& item_list) { _item_list = item_list; }
  void set_item_range_list(const std::vector<OffsetRange>& item_range_list) { _item_range_list = item_range_list; }
  // function
  void append(std::vector<Item> item_list)
  {
    _item_range_list.emplace_back(static_cast<int32_t>(_item_list.size()), static_cast<int32_t>(item_list.size()));
    _item_list.insert(_item_list.end(), std::make_move_iterator(item_list.begin()), std::make_move_iterator(item_list.end()));
  }
  std::span<Item> get_group_item_list(int32_t group_idx)
  {
    OffsetRange item_range = _item_range_list[group_idx];
    return std::span<Item>(_item_list.data() + item_range.get_offset(), item_range.get_count());
  }
  std::span<const Item> get_group_item_list(int32_t group_idx) const
  {
    OffsetRange item_range = _item_range_list[group_idx];
    return std::span<const Item>(_item_list.data() + item_range.get_offset(), item_range.get_count());
  }
  int32_t get_group_num() const { return static_cast<int32_t>(_item_range_list.size()); }
  int32_t get_item_num() const { return static_cast<int32_t>(_item_list.size()); }
  void reserve_group(int32_t group_num) { _item_range_list.reserve(group_num); }
  void reserve_item(int32_t item_num) { _item_list.reserve(item_num); }

 private:
  std::vector<Item> _item_list;
  std::vector<OffsetRange> _item_range_list;
};

}  // namespace ircx
