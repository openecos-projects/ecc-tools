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
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "LVSHeader.hpp"
#include "Violation.hpp"

namespace ilvs {

class ECModel
{
 public:
  ECModel() = default;
  ~ECModel() = default;
  // getter
  std::vector<std::string>& get_netlist_io_name_list() { return _netlist_io_name_list; }
  std::vector<std::string>& get_def_io_name_list() { return _def_io_name_list; }
  std::vector<std::string>& get_netlist_instance_name_list() { return _netlist_instance_name_list; }
  std::vector<std::string>& get_def_instance_name_list() { return _def_instance_name_list; }
  std::vector<std::string>& get_netlist_net_name_list() { return _netlist_net_name_list; }
  std::vector<std::string>& get_def_net_name_list() { return _def_net_name_list; }
  std::vector<Violation>& get_violation_list() { return _violation_list; }
  // const getter
  const std::vector<std::string>& get_netlist_io_name_list() const { return _netlist_io_name_list; }
  const std::vector<std::string>& get_def_io_name_list() const { return _def_io_name_list; }
  const std::vector<std::string>& get_netlist_instance_name_list() const { return _netlist_instance_name_list; }
  const std::vector<std::string>& get_def_instance_name_list() const { return _def_instance_name_list; }
  const std::vector<std::string>& get_netlist_net_name_list() const { return _netlist_net_name_list; }
  const std::vector<std::string>& get_def_net_name_list() const { return _def_net_name_list; }
  const std::vector<Violation>& get_violation_list() const { return _violation_list; }
  // setter
  void set_netlist_io_name_list(const std::vector<std::string>& netlist_io_name_list) { _netlist_io_name_list = netlist_io_name_list; }
  void set_def_io_name_list(const std::vector<std::string>& def_io_name_list) { _def_io_name_list = def_io_name_list; }
  void set_netlist_instance_name_list(const std::vector<std::string>& netlist_instance_name_list) { _netlist_instance_name_list = netlist_instance_name_list; }
  void set_def_instance_name_list(const std::vector<std::string>& def_instance_name_list) { _def_instance_name_list = def_instance_name_list; }
  void set_netlist_net_name_list(const std::vector<std::string>& netlist_net_name_list) { _netlist_net_name_list = netlist_net_name_list; }
  void set_def_net_name_list(const std::vector<std::string>& def_net_name_list) { _def_net_name_list = def_net_name_list; }

 private:
  std::vector<std::string> _netlist_io_name_list;
  std::vector<std::string> _def_io_name_list;
  std::vector<std::string> _netlist_instance_name_list;
  std::vector<std::string> _def_instance_name_list;
  std::vector<std::string> _netlist_net_name_list;
  std::vector<std::string> _def_net_name_list;
  std::vector<Violation> _violation_list;
};

}  // namespace ilvs
