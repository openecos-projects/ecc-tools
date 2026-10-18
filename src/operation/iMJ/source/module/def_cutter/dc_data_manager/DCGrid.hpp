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

#include "MJHeader.hpp"

namespace imj {

class DCGrid
{
 public:
  DCGrid() = default;
  ~DCGrid() = default;
  // getter
  int32_t get_source_idx() { return _source_idx; }
  int32_t get_axis() { return _axis; }
  std::array<int32_t, 2>& get_origin() { return _origin; }
  std::array<int32_t, 2>& get_count() { return _count; }
  std::array<int32_t, 2>& get_step() { return _step; }
  std::array<int32_t, 2>& get_extent() { return _extent; }
  // const getter
  int32_t get_source_idx() const { return _source_idx; }
  int32_t get_axis() const { return _axis; }
  const std::array<int32_t, 2>& get_origin() const { return _origin; }
  const std::array<int32_t, 2>& get_count() const { return _count; }
  const std::array<int32_t, 2>& get_step() const { return _step; }
  const std::array<int32_t, 2>& get_extent() const { return _extent; }
  // setter
  void set_source_idx(int32_t source_idx) { _source_idx = source_idx; }
  void set_axis(int32_t axis) { _axis = axis; }
  void set_origin(const std::array<int32_t, 2>& origin) { _origin = origin; }
  void set_count(const std::array<int32_t, 2>& count) { _count = count; }
  void set_step(const std::array<int32_t, 2>& step) { _step = step; }
  void set_extent(const std::array<int32_t, 2>& extent) { _extent = extent; }
  // function

 private:
  int32_t _source_idx = -1;
  int32_t _axis = -1;
  std::array<int32_t, 2> _origin = {0, 0};
  std::array<int32_t, 2> _count = {0, 0};
  std::array<int32_t, 2> _step = {0, 0};
  std::array<int32_t, 2> _extent = {0, 0};
};

}  // namespace imj
