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

#include "FPHeader.hpp"

namespace ifp {

class PGModel
{
 public:
  PGModel() = default;
  ~PGModel() = default;
  // getter
  std::string& get_default_power_net_name() { return _default_power_net_name; }
  std::string& get_default_ground_net_name() { return _default_ground_net_name; }
  std::set<std::string>& get_via_key_set() { return _via_key_set; }

  // const getter
  const std::string& get_default_power_net_name() const { return _default_power_net_name; }
  const std::string& get_default_ground_net_name() const { return _default_ground_net_name; }
  const std::set<std::string>& get_via_key_set() const { return _via_key_set; }

  // setter
  void set_default_power_net_name(std::string default_power_net_name) { _default_power_net_name = default_power_net_name; }
  void set_default_ground_net_name(std::string default_ground_net_name) { _default_ground_net_name = default_ground_net_name; }

  // function

 private:
  std::string _default_power_net_name;
  std::string _default_ground_net_name;
  std::set<std::string> _via_key_set;
};

}  // namespace ifp
