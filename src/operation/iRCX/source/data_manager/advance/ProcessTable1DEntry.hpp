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

class ProcessTable1DEntry
{
 public:
  ProcessTable1DEntry() = default;
  ProcessTable1DEntry(double key, double value) : _key(key), _value(value) {}
  ~ProcessTable1DEntry() = default;
  // getter
  double get_key() const { return _key; }
  double get_value() const { return _value; }
  // setter
  void set_key(double key) { _key = key; }
  void set_value(double value) { _value = value; }
  // function

 private:
  double _key = -1.0;
  double _value = -1.0;
};

}  // namespace ircx
