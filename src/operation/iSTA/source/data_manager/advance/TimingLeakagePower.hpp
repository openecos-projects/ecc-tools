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

#include "LogicExpression.hpp"
#include "STAHeader.hpp"

namespace ista {

class TimingLeakagePower
{
 public:
  TimingLeakagePower() = default;
  ~TimingLeakagePower() = default;
  // getter
  std::string& get_related_pg_port() { return _related_pg_port; }
  LogicExpression& get_when_expression() { return _when_expression; }
  double get_leakage_power() const { return _leakage_power; }
  // setter
  void set_related_pg_port(const std::string& related_pg_port) { _related_pg_port = related_pg_port; }
  void set_when_expression(const LogicExpression& when_expression) { _when_expression = when_expression; }
  void set_leakage_power(const double leakage_power) { _leakage_power = leakage_power; }
  // function

 private:
  std::string _related_pg_port;
  LogicExpression _when_expression;
  double _leakage_power = 0.0;
};

}  // namespace ista
