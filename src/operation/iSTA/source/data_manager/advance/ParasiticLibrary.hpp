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

#include "ParasiticNet.hpp"
#include "STAHeader.hpp"

namespace ista {

class ParasiticLibrary
{
 public:
  ParasiticLibrary() = default;
  ~ParasiticLibrary() = default;
  // getter
  std::string& get_spef_file_path() { return _spef_file_path; }
  std::string& get_capacitive_unit() { return _capacitive_unit; }
  std::string& get_resistance_unit() { return _resistance_unit; }
  std::map<std::string, ParasiticNet>& get_net_map() { return _net_map; }
  // setter
  void set_spef_file_path(const std::string& spef_file_path) { _spef_file_path = spef_file_path; }
  void set_capacitive_unit(const std::string& capacitive_unit) { _capacitive_unit = capacitive_unit; }
  void set_resistance_unit(const std::string& resistance_unit) { _resistance_unit = resistance_unit; }
  void set_net_map(const std::map<std::string, ParasiticNet>& net_map) { _net_map = net_map; }
  // function

 private:
  std::string _spef_file_path;
  std::string _capacitive_unit;
  std::string _resistance_unit;
  std::map<std::string, ParasiticNet> _net_map;
};

}  // namespace ista
