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

class EnvAxis
{
 public:
  EnvAxis() = default;
  EnvAxis(int32_t origin, int32_t count, int32_t step) : _origin(origin), _count(count), _step(step) {}
  ~EnvAxis() = default;
  // getter
  int32_t get_origin() const { return _origin; }
  int32_t get_count() const { return _count; }
  int32_t get_step() const { return _step; }
  // setter
  void set_origin(int32_t origin) { _origin = origin; }
  void set_count(int32_t count) { _count = count; }
  void set_step(int32_t step) { _step = step; }
  // function

 private:
  int32_t _origin = -1;
  int32_t _count = -1;
  int32_t _step = -1;
};

}  // namespace ircx
