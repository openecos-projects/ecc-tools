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

#include "STAHeader.hpp"

namespace ista {

class ParasiticResistor
{
 public:
  ParasiticResistor() = default;
  ~ParasiticResistor() = default;
  // getter
  std::string& get_source_node() { return _source_node; }
  std::string& get_sink_node() { return _sink_node; }
  double get_resistance() const { return _resistance; }
  // setter
  void set_source_node(const std::string& source_node) { _source_node = source_node; }
  void set_sink_node(const std::string& sink_node) { _sink_node = sink_node; }
  void set_resistance(const double resistance) { _resistance = resistance; }
  // function

 private:
  std::string _source_node;
  std::string _sink_node;
  double _resistance = 0.0;
};

}  // namespace ista
