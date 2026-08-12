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
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "STAHeader.hpp"
#include "TCScalarTable.hpp"

namespace ista {

class TCTimingArc
{
 public:
  TCTimingArc() = default;
  ~TCTimingArc() = default;
  // getter
  std::string& get_source_port() { return _source_port; }
  std::string& get_sink_port() { return _sink_port; }
  std::string& get_timing_type() { return _timing_type; }
  std::string& get_timing_sense() { return _timing_sense; }
  bool get_is_check_arc() const { return _is_check_arc; }
  std::vector<TCScalarTable>& get_scalar_table_list() { return _scalar_table_list; }
  // setter
  void set_source_port(const std::string& source_port) { _source_port = source_port; }
  void set_sink_port(const std::string& sink_port) { _sink_port = sink_port; }
  void set_timing_type(const std::string& timing_type) { _timing_type = timing_type; }
  void set_timing_sense(const std::string& timing_sense) { _timing_sense = timing_sense; }
  void set_is_check_arc(const bool is_check_arc) { _is_check_arc = is_check_arc; }
  void set_scalar_table_list(const std::vector<TCScalarTable>& scalar_table_list) { _scalar_table_list = scalar_table_list; }
  // function

 private:
  std::string _source_port;
  std::string _sink_port;
  std::string _timing_type;
  std::string _timing_sense;
  bool _is_check_arc = false;
  std::vector<TCScalarTable> _scalar_table_list;
};

}  // namespace ista
