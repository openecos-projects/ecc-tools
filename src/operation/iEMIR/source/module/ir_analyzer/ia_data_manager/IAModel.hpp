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
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "EMIRHeader.hpp"

namespace iemir {

class IAModel
{
 public:
  IAModel() = default;
  ~IAModel() = default;
  // getter
  std::set<std::size_t>& get_source_node_id_set() { return _source_node_id_set; }
  std::map<std::size_t, std::size_t>& get_node_id_to_matrix_idx_map() { return _node_id_to_matrix_idx_map; }
  std::vector<std::size_t>& get_matrix_idx_to_node_id_list() { return _matrix_idx_to_node_id_list; }
  std::map<std::size_t, double>& get_node_current_map() { return _node_current_map; }
  double get_source_voltage() { return _source_voltage; }
  // setter
  void set_source_voltage(double source_voltage) { _source_voltage = source_voltage; }
  // function

 private:
  std::set<std::size_t> _source_node_id_set;
  std::map<std::size_t, std::size_t> _node_id_to_matrix_idx_map;
  std::vector<std::size_t> _matrix_idx_to_node_id_list;
  std::map<std::size_t, double> _node_current_map;
  double _source_voltage = 0.0;
};

}  // namespace iemir
