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

class EnvPixelOverlap
{
 public:
  EnvPixelOverlap() = default;
  EnvPixelOverlap(int32_t start_coord, int32_t end_coord) : _start_coord(start_coord), _end_coord(end_coord) {}
  ~EnvPixelOverlap() = default;
  // getter
  int32_t get_start_coord() const { return _start_coord; }
  int32_t get_end_coord() const { return _end_coord; }
  // setter
  void set_start_coord(int32_t start_coord) { _start_coord = start_coord; }
  void set_end_coord(int32_t end_coord) { _end_coord = end_coord; }
  // function
  bool empty() const { return _end_coord <= _start_coord; }

 private:
  int32_t _start_coord = 0;
  int32_t _end_coord = 0;
};

}  // namespace ircx
