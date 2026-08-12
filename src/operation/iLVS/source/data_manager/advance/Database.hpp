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

#include "DefData.hpp"
#include "LVSHeader.hpp"
#include "NetlistData.hpp"
#include "Summary.hpp"

namespace ilvs {

class Database
{
 public:
  Database() = default;
  ~Database() = default;

  // getter
  NetlistData& get_netlist_data() { return _netlist_data; }
  DefData& get_def_data() { return _def_data; }
  Summary& get_summary() { return _summary; }
  // const getter
  const NetlistData& get_netlist_data() const { return _netlist_data; }
  const DefData& get_def_data() const { return _def_data; }
  const Summary& get_summary() const { return _summary; }
  // setter
  void set_netlist_data(NetlistData netlist_data)
  {
    _netlist_data = std::move(netlist_data);
  }
  void set_def_data(DefData def_data)
  {
    _def_data = std::move(def_data);
  }
  // function
  void reset()
  {
    _netlist_data.reset();
    _def_data.reset();
    _summary.reset();
  }

 private:
  NetlistData _netlist_data;
  DefData _def_data;
  Summary _summary;
};

}  // namespace ilvs
