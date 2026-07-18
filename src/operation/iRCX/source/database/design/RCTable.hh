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
 * @file RCTable.hh
 * @brief Resistance and capacitance result storage.
 */
#pragma once

#include <span>
#include <unordered_map>
#include <vector>

#include "CornerNetPool.hh"
#include "HashUtils.hh"
#include "TopoPool.hh"
#include "Types.hh"

namespace ircx {

using CouplingKey = hash::UndirectedPairKey<Size>;
using CouplingKeyHash = hash::PairKeyHasher<CouplingKey>;

struct CcapEntry
{
  Size edge_a_global_idx;
  Size edge_b_global_idx;
  Size corner_idx;
  F32 value;
};

class RCTable
{
 public:
  RCTable() = default;
  ~RCTable() = default;

  void clear()
  {
    corner_num_ = 0;
    net_num_ = 0;
    corner_net_res_pools_.clear();
    corner_net_gcap_pools_.clear();
    net_ccap_entries_.clear();
    merged_ccap_.clear();
  }

  /// Pre-allocate all storage. Must be called before any parallel calc.
  void init(Size corner_num,
            Size net_num,
            const TopoPool& topo)
  {
    clear();
    corner_num_ = corner_num;
    net_num_ = net_num;

    corner_net_res_pools_.init(corner_num_, net_num_);
    corner_net_gcap_pools_.init(corner_num_, net_num_);

    for (Size corner_idx = 0; corner_idx < corner_num; ++corner_idx) {
      for (Size net_idx = 0; net_idx < net_num; ++net_idx) {
        const Size edge_count = topo.get_net_edges(net_idx).size();
        const CornerNetId id{corner_idx, net_idx};
        corner_net_res_pools_.at(id).assign(edge_count, 0.0);
        corner_net_gcap_pools_.at(id).assign(edge_count, 0.0);
      }
    }

    net_ccap_entries_.resize(net_num);
  }

  // Resistance: writable span per (corner, net)
  std::span<F64> get_corner_net_res_pool(CornerNetId id) { return corner_net_res_pools_.at(id); }
  std::span<const F64> get_corner_net_res_pool(CornerNetId id) const
  {
    return corner_net_res_pools_.at(id);
  }

  // Ground cap: writable span per (corner, net)
  std::span<F64> get_corner_net_gcap_pool(CornerNetId id) { return corner_net_gcap_pools_.at(id); }
  std::span<const F64> get_corner_net_gcap_pool(CornerNetId id) const
  {
    return corner_net_gcap_pools_.at(id);
  }

  // Coupling cap: per-net accumulation (parallel-safe across nets)
  void appendNetCcapEntry(Size net_idx,
                             Size edge_a_global_idx,
                             Size edge_b_global_idx,
                             Size corner_idx,
                             F32 value)
  {
    net_ccap_entries_[net_idx].push_back({edge_a_global_idx, edge_b_global_idx, corner_idx, value});
  }

  /// Merge per-net ccap entries into the final map.
  /// Call AFTER the parallel loop completes.
  void mergeNetCcapEntries()
  {
    for (auto& net_entries : net_ccap_entries_) {
      for (auto& [edge_a_global_idx, edge_b_global_idx, corner_idx, cap_value] : net_entries) {
        CouplingKey key(edge_a_global_idx, edge_b_global_idx);
        auto it = merged_ccap_.find(key);
        if (it == merged_ccap_.end()) {
          auto& vec = merged_ccap_[key];
          vec.resize(corner_num_, 0.0f);
          vec[corner_idx] += cap_value;
        } else {
          it->second[corner_idx] += cap_value;
        }
      }
      net_entries.clear();
    }
  }

  // Accessors
  Size get_corner_num() const { return corner_num_; }
  Size get_net_num() const { return net_num_; }
  const auto& get_merged_ccap() const { return merged_ccap_; }

 private:
  Size corner_num_{0};
  Size net_num_{0};

  CornerNetPool<std::vector<F64>> corner_net_res_pools_;
  CornerNetPool<std::vector<F64>> corner_net_gcap_pools_;

  // parallel accumulation: indexed by net_idx
  std::vector<std::vector<CcapEntry>> net_ccap_entries_;

  // merged result
  std::unordered_map<CouplingKey, std::vector<F32>, CouplingKeyHash> merged_ccap_;
};

}  // namespace ircx
