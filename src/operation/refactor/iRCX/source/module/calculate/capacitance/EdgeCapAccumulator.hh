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
/**
 * @file EdgeCapAccumulator.hh
 * @brief iRCX module implementation detail.
 */
#pragma once

#include <span>

#include "CapTableQuery.hh"
#include "RCTable.hh"
#include "TopoPool.hh"

namespace ircx {

enum class AdjacentKind {
  kNone,
  kSpecialNet,
  kSameNet,
  kOtherNet
};

struct SideContext {
  Micron spacing{0};
  const TopoEdge* adjacent{nullptr};
  AdjacentKind kind{AdjacentKind::kNone};

  bool occupied() const { return adjacent != nullptr; }
  bool sameNet() const { return kind == AdjacentKind::kSameNet; }
  bool specialNet() const { return kind == AdjacentKind::kSpecialNet; }
};

auto makeSideContext(Micron spacing,
                     const TopoEdge* adjacent,
                     Size net_idx) -> SideContext;

class EdgeCapAccumulator
{
 public:
  EdgeCapAccumulator(const CapTableQuery& cap_query,
                     const TopoPool& topo_pool,
                     RCTable& rc_table,
                     std::span<F64> edge_ground_caps,
                     Size corner_idx,
                     Size net_idx,
                     Size edge_idx,
                     Size edge_global_idx);

  void accumulateSpan(Micron span_length,
                      const std::string& below_layer,
                      const std::string& above_layer,
                      const SideContext& low_side,
                      const SideContext& high_side);

 private:
  void accumulateGround(const SideContext& side,
                        F64 ground_cap_ff);
  void foldCoupling(const SideContext& side,
                    F64 coupling_cap_ff);

  const CapTableQuery& cap_query_;
  const TopoPool& topo_pool_;
  RCTable& rc_table_;
  std::span<F64> edge_ground_caps_;
  Size corner_idx_;
  Size net_idx_;
  Size edge_idx_;
  Size edge_global_idx_;
};

}  // namespace ircx
