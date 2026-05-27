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

#include <iterator>
#include <span>
#include <utility>
#include <vector>

#include "Types.hh"

namespace ircx {

template <typename Item>
class SpanPool
{
 public:
  SpanPool() = default;
  ~SpanPool() = default;

  void append_group(std::vector<Item> items)
  {
    group_ranges_.emplace_back(item_pool_.size(), items.size());
    item_pool_.insert(item_pool_.end(),
                      std::make_move_iterator(items.begin()),
                      std::make_move_iterator(items.end()));
  }

  std::span<const Item> group_items(Size group_id) const
  {
    if (group_id >= group_ranges_.size()) {
      return {};
    }

    const auto& [offset, length] = group_ranges_[group_id];
    if (length == 0) {
      return {};
    }
    return std::span<const Item>(item_pool_.data() + offset, length);
  }

  std::span<Item> group_items(Size group_id)
  {
    if (group_id >= group_ranges_.size()) {
      return {};
    }

    const auto& [offset, length] = group_ranges_[group_id];
    if (length == 0) {
      return {};
    }
    return std::span<Item>(item_pool_.data() + offset, length);
  }

  void clear()
  {
    item_pool_.clear();
    group_ranges_.clear();
  }

 private:
  std::vector<Item> item_pool_;
  std::vector<std::pair<Size, Size>> group_ranges_;
};

}  // namespace ircx
