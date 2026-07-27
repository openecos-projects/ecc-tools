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

template <typename T>
class IntervalRange
{
 public:
  IntervalRange() = default;
  IntervalRange(T start, T end) : _start(start), _end(end) {}
  ~IntervalRange() = default;
  // getter
  T get_start() const { return _start; }
  T get_end() const { return _end; }
  // setter
  void set_start(T start) { _start = start; }
  void set_end(T end) { _end = end; }
  // function

 private:
  T _start = {};
  T _end = {};
};

}  // namespace ircx
