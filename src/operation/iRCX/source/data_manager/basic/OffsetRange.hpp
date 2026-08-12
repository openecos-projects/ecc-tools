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

class OffsetRange
{
 public:
  OffsetRange() = default;
  OffsetRange(int32_t offset, int32_t count) : _offset(offset), _count(count) {}
  ~OffsetRange() = default;
  // getter
  int32_t get_offset() const { return _offset; }
  int32_t get_count() const { return _count; }
  // setter
  void set_offset(int32_t offset) { _offset = offset; }
  void set_count(int32_t count) { _count = count; }
  // function

 private:
  int32_t _offset = -1;
  int32_t _count = 0;
};

}  // namespace ircx
