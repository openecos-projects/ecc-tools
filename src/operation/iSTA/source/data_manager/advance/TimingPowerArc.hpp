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
#include "TimingTable.hpp"
#include "TransType.hpp"

namespace ista {

class TimingPowerArc
{
 public:
  TimingPowerArc() = default;
  ~TimingPowerArc() = default;
  // getter
  std::string& get_source_port() { return _source_port; }
  std::string& get_sink_port() { return _sink_port; }
  std::string& get_related_pg_port() { return _related_pg_port; }
  LogicExpression& get_when_expression() { return _when_expression; }
  std::map<TransType, TimingTable>& get_energy_table_map() { return _energy_table_map; }
  double get_time_unit_scale() const { return _time_unit_scale; }
  double get_cap_unit_scale() const { return _cap_unit_scale; }
  // setter
  void set_source_port(const std::string& source_port) { _source_port = source_port; }
  void set_sink_port(const std::string& sink_port) { _sink_port = sink_port; }
  void set_related_pg_port(const std::string& related_pg_port) { _related_pg_port = related_pg_port; }
  void set_when_expression(const LogicExpression& when_expression) { _when_expression = when_expression; }
  void set_energy_table_map(const std::map<TransType, TimingTable>& energy_table_map) { _energy_table_map = energy_table_map; }
  void set_time_unit_scale(const double time_unit_scale) { _time_unit_scale = time_unit_scale; }
  void set_cap_unit_scale(const double cap_unit_scale) { _cap_unit_scale = cap_unit_scale; }
  // function

 private:
  std::string _source_port;
  std::string _sink_port;
  std::string _related_pg_port;
  LogicExpression _when_expression;
  std::map<TransType, TimingTable> _energy_table_map;
  double _time_unit_scale = 1.0;
  double _cap_unit_scale = 1.0;
};

}  // namespace ista
