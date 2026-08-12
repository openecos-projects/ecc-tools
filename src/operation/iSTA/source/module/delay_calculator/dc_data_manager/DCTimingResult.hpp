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
// WHETHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "STAHeader.hpp"
#include "TransType.hpp"

namespace ista {

class DCTimingResult
{
 public:
  DCTimingResult() = default;
  ~DCTimingResult() = default;
  // getter
  TransType& get_output_trans_type() { return _output_trans_type; }
  double get_delay() { return _delay; }
  double get_slew() { return _slew; }
  // setter
  void set_output_trans_type(const TransType& output_trans_type) { _output_trans_type = output_trans_type; }
  void set_delay(const double delay) { _delay = delay; }
  void set_slew(const double slew) { _slew = slew; }
  // function

 private:
  TransType _output_trans_type = TransType::kNone;
  double _delay = 0.0;
  double _slew = 0.0;
};

}  // namespace ista
