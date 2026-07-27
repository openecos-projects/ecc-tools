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

class ViaEtch
{
 public:
  ViaEtch() = default;
  ViaEtch(double length, double width) : _length(length), _width(width) {}
  ~ViaEtch() = default;
  // getter
  double get_length() const { return _length; }
  double get_width() const { return _width; }
  // setter
  void set_length(double length) { _length = length; }
  void set_width(double width) { _width = width; }
  // function

 private:
  double _length = 0.0;
  double _width = 0.0;
};

}  // namespace ircx
