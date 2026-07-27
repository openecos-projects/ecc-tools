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
/**
 * @file SpefTopologyBuilder.hh
 * @brief Build iRCX topology from StarRC SPEF connectivity and annotations.
 */
#pragma once

#include <string>

namespace ircx {

class LayerTable;
class LayoutData;
class TopoPool;

namespace run_rcx_from_topology {

class SpefTopologyBuilder
{
 public:
  explicit SpefTopologyBuilder(TopoPool& topologies) : topo_pool_(&topologies) {}
  SpefTopologyBuilder() = delete;
  ~SpefTopologyBuilder() = default;

  auto build(const LayoutData& layout,
             const LayerTable& layer_table,
             const std::string& spef_file,
             bool strict) const -> bool;

 private:
  TopoPool* topo_pool_{nullptr};
};

}  // namespace run_rcx_from_topology
}  // namespace ircx
