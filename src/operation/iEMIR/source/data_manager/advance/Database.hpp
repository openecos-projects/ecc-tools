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

#include "EMIRHeader.hpp"
#include "InstancePower.hpp"
#include "PowerGraph.hpp"
#include "PowerNet.hpp"

namespace iemir {

class Database
{
 public:
  Database() = default;
  ~Database() = default;
  // getter
  std::string& get_design_name() { return _design_name; }
  int32_t get_micron_dbu() { return _micron_dbu; }
  std::set<uint64_t>& get_instance_id_set() { return _instance_id_set; }
  std::map<std::string, PowerNet>& get_power_net_map() { return _power_net_map; }
  std::map<uint64_t, InstancePower>& get_instance_power_map() { return _instance_power_map; }
  std::map<std::string, PowerGraph>& get_power_graph_map() { return _power_graph_map; }
  // setter
  void set_design_name(const std::string& design_name) { _design_name = design_name; }
  void set_micron_dbu(int32_t micron_dbu) { _micron_dbu = micron_dbu; }
  // function

 private:
  std::string _design_name;
  int32_t _micron_dbu = 0;
  std::set<uint64_t> _instance_id_set;
  std::map<std::string, PowerNet> _power_net_map;
  std::map<uint64_t, InstancePower> _instance_power_map;
  std::map<std::string, PowerGraph> _power_graph_map;
};

}  // namespace iemir
