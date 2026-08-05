// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of the Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "ZHHeader.hpp"

namespace izh {

class MIFillShape
{
 public:
  MIFillShape() = default;
  MIFillShape(int32_t width, int32_t height) : _width(width), _height(height) {}
  ~MIFillShape() = default;
  // getter
  int32_t get_width() const { return _width; }
  int32_t get_height() const { return _height; }
  // setter
  void set_width(int32_t width) { _width = width; }
  void set_height(int32_t height) { _height = height; }
  // function
  bool isValid() const { return _width > 0 && _height > 0; }

 private:
  int32_t _width = 0;
  int32_t _height = 0;
};

}  // namespace izh
