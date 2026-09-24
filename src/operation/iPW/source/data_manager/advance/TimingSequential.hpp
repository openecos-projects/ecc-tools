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

#include "LogicExpression.hpp"
#include "PWHeader.hpp"

namespace ipw {

class TimingSequential
{
 public:
  TimingSequential() = default;
  ~TimingSequential() = default;
  // getter
  std::string& get_state_port() { return _state_port; }
  std::string& get_inverted_state_port() { return _inverted_state_port; }
  bool get_is_latch() const { return _is_latch; }
  LogicExpression& get_data() { return _data; }
  LogicExpression& get_clock() { return _clock; }
  LogicExpression& get_clear() { return _clear; }
  LogicExpression& get_preset() { return _preset; }
  // setter
  void set_state_port(const std::string& state_port) { _state_port = state_port; }
  void set_inverted_state_port(const std::string& inverted_state_port) { _inverted_state_port = inverted_state_port; }
  void set_is_latch(const bool is_latch) { _is_latch = is_latch; }
  void set_data(const LogicExpression& data) { _data = data; }
  void set_clock(const LogicExpression& clock) { _clock = clock; }
  void set_clear(const LogicExpression& clear) { _clear = clear; }
  void set_preset(const LogicExpression& preset) { _preset = preset; }
  // function

 private:
  std::string _state_port;
  std::string _inverted_state_port;
  bool _is_latch = false;
  LogicExpression _data;
  LogicExpression _clock;
  LogicExpression _clear;
  LogicExpression _preset;
};

}  // namespace ipw
