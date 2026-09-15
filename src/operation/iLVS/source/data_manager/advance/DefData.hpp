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

#include "DefRoutingData.hpp"
#include "DesignData.hpp"
#include "Die.hpp"
#include "PhysicalGraph.hpp"

namespace ilvs {

class DefData : public DesignData
{
 public:
  DefData() = default;
  ~DefData() = default;
  // getter
  Die& get_die() { return _die; }
  DefRoutingData& get_def_routing_data() { return _def_routing_data; }
  PhysicalGraph& get_physical_graph() { return _physical_graph; }
  // const getter
  const Die& get_die() const { return _die; }
  const DefRoutingData& get_def_routing_data() const { return _def_routing_data; }
  const PhysicalGraph& get_physical_graph() const { return _physical_graph; }
  // function
  void reset()
  {
    DesignData::reset();
    _die = Die();
    _def_routing_data.reset();
    _physical_graph.reset();
  }

 private:
  Die _die;
  DefRoutingData _def_routing_data;
  PhysicalGraph _physical_graph;
};

}  // namespace ilvs
