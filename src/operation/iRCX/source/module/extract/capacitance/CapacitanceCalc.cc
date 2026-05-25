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
#include <algorithm>

#include "CapacitanceCalc.hh"
#include "LayerTable.hh"
#include "ProcessCorner.hpp"
#include "CapTable.hpp"
#include "log/Log.hh"
namespace ircx {

namespace {

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

AdjacentKind classifyAdjacent(const TopoEdge* adjacent, Size net_idx)
{
  if (adjacent == nullptr) {
    return AdjacentKind::kNone;
  }
  if (adjacent->net_id() == kSpecialNetId) {
    return AdjacentKind::kSpecialNet;
  }
  if (adjacent->net_id() == net_idx) {
    return AdjacentKind::kSameNet;
  }
  return AdjacentKind::kOtherNet;
}

SideContext makeSideContext(Micron spacing, const TopoEdge* adjacent, Size net_idx)
{
  return SideContext{spacing, adjacent, classifyAdjacent(adjacent, net_idx)};
}

void resolveCrossLayers(
    const LayerTable& layer_table,
    const CrossOverlapSub* cross_seg,
    Str& below_layer,
    Str& above_layer)
{
  below_layer = "SUBSTRATE";
  above_layer.clear();

  if (cross_seg == nullptr) {
    return;
  }

  if (cross_seg->blw_layer != 0) {
    const Size process_layer_id = layer_table.design_to_process_id(cross_seg->blw_layer);
    below_layer = layer_table.process_name(process_layer_id);
  }
  if (cross_seg->abv_layer != 0) {
    const Size process_layer_id = layer_table.design_to_process_id(cross_seg->abv_layer);
    above_layer = layer_table.process_name(process_layer_id);
  }
}

class EdgeCapAccumulator {
 public:
  EdgeCapAccumulator(
      const parser::CapTable& cap_table,
      const TopoPool& topo_pool,
      RCTable& rc_table,
      std::span<F64> edge_ground_caps,
      const Str& layer_name,
      Size corner_idx,
      Size net_idx,
      Size edge_idx,
      Size edge_global_id)
      : cap_table_(cap_table),
        topo_pool_(topo_pool),
        rc_table_(rc_table),
        edge_ground_caps_(edge_ground_caps),
        layer_name_(layer_name),
        corner_idx_(corner_idx),
        net_idx_(net_idx),
        edge_idx_(edge_idx),
        edge_global_id_(edge_global_id)
  {
  }

  void accumulateSpan(
      Micron span_length,
      const Str& below_layer,
      const Str& above_layer,
      const SideContext& low_side,
      const SideContext& high_side)
  {
    if (span_length <= 0.0) {
      return;
    }

    if (low_side.occupied() && high_side.occupied()) {
      // Two occupied sides:
      //   gcap = L * (cg_lo + cg_hi)
      //   ccap(side) = L * cc_side / 2
      //   same-net side keeps only half of its ground term
      const parser::CapacitanceResult low_side_cap =
          queryNearCap(below_layer, above_layer, low_side.spacing);
      const parser::CapacitanceResult high_side_cap =
          queryNearCap(below_layer, above_layer, high_side.spacing);

      accumulateGround(low_side, span_length * low_side_cap.ground_cap);
      accumulateGround(high_side, span_length * high_side_cap.ground_cap);
      foldCoupling(low_side, span_length * low_side_cap.coupling_cap / 2.0);
      foldCoupling(high_side, span_length * high_side_cap.coupling_cap / 2.0);
      return;
    }

    if (low_side.occupied() || high_side.occupied()) {
      // One occupied side:
      //   gcap = 2 * L * cg
      //   ccap = L * cc
      //   same-net side keeps only half of the ground term
      const SideContext& occupied_side = low_side.occupied() ? low_side : high_side;
      const parser::CapacitanceResult occupied_side_cap =
          queryNearCap(below_layer, above_layer, occupied_side.spacing);

      accumulateGround(occupied_side, 2.0 * span_length * occupied_side_cap.ground_cap);
      foldCoupling(occupied_side, span_length * occupied_side_cap.coupling_cap);
      return;
    }

    // No occupied sides:
    //   take the farthest table row and use
    //   gcap = 2 * L * (cg + cc)
    const parser::CapacitanceResult far_cap =
        queryFarthestCap(below_layer, above_layer);
    edge_ground_caps_[edge_idx_] +=
        2.0 * span_length * (far_cap.ground_cap + far_cap.coupling_cap);
  }

 private:
  parser::CapacitanceResult queryNearCap(
      const Str& below_layer,
      const Str& above_layer,
      Micron spacing) const
  {
    const Micron lookup_dist = std::max<Micron>(spacing, 0.0);
    if (above_layer.empty()) {
      return cap_table_.queryTwoLayerCap(layer_name_, below_layer, lookup_dist);
    }
    return cap_table_.queryThreeLayerCap(
        layer_name_, below_layer, above_layer, lookup_dist);
  }

  parser::CapacitanceResult queryFarthestCap(
      const Str& below_layer,
      const Str& above_layer) const
  {
    if (above_layer.empty()) {
      return cap_table_.queryTwoLayerFarthestCap(layer_name_, below_layer);
    }
    return cap_table_.queryThreeLayerFarthestCap(
        layer_name_, below_layer, above_layer);
  }

  void accumulateGround(const SideContext& side, double ground_cap_ff)
  {
    if (ground_cap_ff <= 0.0 || !side.occupied()) {
      return;
    }

    // Same-net adjacency only contributes ground cap, with half weight.
    edge_ground_caps_[edge_idx_] += side.sameNet() ? ground_cap_ff / 2.0
                                                   : ground_cap_ff;
  }

  void foldCoupling(const SideContext& side, double coupling_cap_ff)
  {
    if (coupling_cap_ff <= 0.0 || !side.occupied()) {
      return;
    }

    // Special-net coupling is modeled as ground capacitance.
    // Same-net adjacency does not contribute coupling.
    if (side.specialNet()) {
      edge_ground_caps_[edge_idx_] += coupling_cap_ff;
      return;
    }
    if (side.sameNet()) {
      return;
    }

    const Size adjacent_edge_global_id = topo_pool_.edge_index(*side.adjacent);
    rc_table_.append_net_ccap_entry(
        net_idx_,
        edge_global_id_,
        adjacent_edge_global_id,
        corner_idx_,
        static_cast<F32>(coupling_cap_ff));
  }

  const parser::CapTable& cap_table_;
  const TopoPool& topo_pool_;
  RCTable& rc_table_;
  std::span<F64> edge_ground_caps_;
  const Str& layer_name_;
  Size corner_idx_;
  Size net_idx_;
  Size edge_idx_;
  Size edge_global_id_;
};

void accumulateSegmentCap(
    const LayerTable& layer_table,
    Micron dbu_to_micron,
    Dbu segment_lo_dbu,
    Dbu segment_hi_dbu,
    const CrossOverlapSub* cross_overlap,
    const SideContext& low_side,
    const SideContext& high_side,
    EdgeCapAccumulator& accumulator)
{
  if (segment_hi_dbu <= segment_lo_dbu) {
    return;
  }

  Str below_layer;
  Str above_layer;
  resolveCrossLayers(layer_table, cross_overlap, below_layer, above_layer);
  accumulator.accumulateSpan(
      (segment_hi_dbu - segment_lo_dbu) * dbu_to_micron,
      below_layer,
      above_layer,
      low_side,
      high_side);
}

}  // namespace

bool CapacitanceCalc::calc()
{
  if (!validateInputs()) {
    return false;
  }

  const Size corner_count = corners_.size();
  const Size net_count = layout_data_->regular_net_count();

  if (cap_tables_.size() != corner_count) {
    LOG_ERROR << "cap table count does not match corner count.";
    return false;
  }

  for (Size corner_idx = 0; corner_idx < corner_count; ++corner_idx) {
    const parser::CapTable* cap_table = cap_tables_[corner_idx];
    if (cap_table == nullptr) {
      LOG_ERROR << "cap table missing for corner " << corners_[corner_idx]->get_technology();
      return false;
    }

    #pragma omp parallel for schedule(dynamic)
    for (Size net_idx = 0; net_idx < net_count; ++net_idx) {
      calcNet(
          corner_idx,
          net_idx,
          *cap_table,
          (*corner_net_etch_pools_)[corner_idx * net_count + net_idx],
          (*net_env_pools_)[net_idx]);
    }
  }

  // merge per-net coupling cap entries (sequential)
  rc_table_->merge_net_ccap_entries();
  return true;
}

bool CapacitanceCalc::validateInputs() const
{
  if (layout_data_ == nullptr) {
    LOG_ERROR << "calculate capacitance failed: layout data not set.";
    return false;
  }
  if (net_env_pools_ == nullptr) {
    LOG_ERROR << "calculate capacitance failed: environment pools not set.";
    return false;
  }
  if (corner_net_etch_pools_ == nullptr) {
    LOG_ERROR << "calculate capacitance failed: etch pools not set.";
    return false;
  }
  if (layer_table_ == nullptr) {
    LOG_ERROR << "calculate capacitance failed: layer table not set.";
    return false;
  }
  if (topo_pool_ == nullptr) {
    LOG_ERROR << "calculate capacitance failed: topology pool not set.";
    return false;
  }
  if (rc_table_ == nullptr) {
    LOG_ERROR << "calculate capacitance failed: RC table not set.";
    return false;
  }
  if (corners_.empty()) {
    LOG_ERROR << "calculate capacitance failed: process corners not set.";
    return false;
  }
  if (cap_tables_.empty()) {
    LOG_ERROR << "calculate capacitance failed: cap tables not set.";
    return false;
  }

  return true;
}

void CapacitanceCalc::calcNet(
    Size corner_idx,
    Size net_idx,
    const parser::CapTable& cap_table,
    const EtchPool& corner_net_etch_pool,
    const EnvPool& net_env_pool)
{
  const auto net_edges = topo_pool_->net_edges(net_idx);
  auto edge_ground_caps = rc_table_->corner_net_gcap_pool(corner_idx, net_idx);

  for (Size edge_idx = 0; edge_idx < net_edges.size(); ++edge_idx) {
    calcEdge(
        corner_idx,
        net_idx,
        edge_idx,
        net_edges[edge_idx],
        cap_table,
        edge_ground_caps,
        net_env_pool,
        corner_net_etch_pool);
  }
}

void CapacitanceCalc::calcEdge(
    Size corner_idx,
    Size net_idx,
    Size edge_idx,
    const TopoEdge& edge,
    const parser::CapTable& cap_table,
    std::span<F64> edge_ground_caps,
    const EnvPool& net_env_pool,
    const EtchPool& corner_net_etch_pool)
{
  if (edge.is_via()) {
    return;
  }

  const Size process_layer_id = layer_table_->design_to_process_id(edge.layer_id());
  const Str& layer_name = layer_table_->process_name(process_layer_id);

  // Global index of current edge in TopoPool::edge_pool()
  const Size edge_global_id = topo_pool_->edge_index(edge);

  const auto env_intervals = net_env_pool.edge_env_interval_pool(edge_idx);
  const auto etch_intervals = corner_net_etch_pool.edge_etch_interval_pool(edge_idx);
  LOG_ERROR_IF(env_intervals.size() != etch_intervals.size())
      << "environment/etch interval count mismatch for net "
      << net_idx << ", edge " << edge_idx << ".";
  const Size interval_count = std::min(env_intervals.size(), etch_intervals.size());

  // Keep all per-edge lookup and accumulation state in one helper so the
  // outer flow stays focused on interval traversal.
  EdgeCapAccumulator accumulator(
      cap_table,
      *topo_pool_,
      *rc_table_,
      edge_ground_caps,
      layer_name,
      corner_idx,
      net_idx,
      edge_idx,
      edge_global_id);

  for (Size interval_idx = 0; interval_idx < interval_count; ++interval_idx) {
    const EnvInterval& env_interval = env_intervals[interval_idx];
    const EtchInterval& etch_interval = etch_intervals[interval_idx];

    const Dbu interval_lo_dbu = env_interval.a0;
    const Dbu interval_hi_dbu = env_interval.a1;
    if (interval_hi_dbu <= interval_lo_dbu) {
      continue;
    }

    const SideContext low_side = makeSideContext(
        etch_interval.lo_spacing, env_interval.lo_adjacent, net_idx);
    const SideContext high_side = makeSideContext(
        etch_interval.hi_spacing, env_interval.hi_adjacent, net_idx);

    Dbu cursor_dbu = interval_lo_dbu;
    for (const CrossOverlapSub& cross_overlap : env_interval.cross_segs) {
      if (cursor_dbu < cross_overlap.a0) {
        accumulateSegmentCap(
            *layer_table_,
            dbu_to_micron_,
            cursor_dbu,
            cross_overlap.a0,
            nullptr,
            low_side,
            high_side,
            accumulator);
        cursor_dbu = cross_overlap.a0;
      }

      const Dbu overlap_hi_dbu = std::min(interval_hi_dbu, cross_overlap.a1);
      if (cursor_dbu < overlap_hi_dbu) {
        accumulateSegmentCap(
            *layer_table_,
            dbu_to_micron_,
            cursor_dbu,
            overlap_hi_dbu,
            &cross_overlap,
            low_side,
            high_side,
            accumulator);
        cursor_dbu = overlap_hi_dbu;
      }
    }

    if (cursor_dbu < interval_hi_dbu) {
      accumulateSegmentCap(
          *layer_table_,
          dbu_to_micron_,
          cursor_dbu,
          interval_hi_dbu,
          nullptr,
          low_side,
          high_side,
          accumulator);
    }
  }
}

}  // namespace ircx
