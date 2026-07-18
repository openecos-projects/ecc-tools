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
 * @file TopoPool.hh
 * @brief Contiguous topology node and edge pools.
 */
#pragma once

#include <span>
#include <string>
#include <vector>

#include "Geometry.hh"
#include "Types.hh"

namespace ircx {

class TopoPool;

// ============================================================
// TopoNode
// ============================================================
class TopoNode {
 public:
  explicit TopoNode(Size net_id) : net_id_(net_id) {}
  TopoNode() = delete;
  ~TopoNode() = default;

  // Per-net local id assigned by TopoPool::addNet().
  // For a global flat-pool index, use TopoPool::get_node_index(node).
  Size get_id() const { return id_; }
  Size get_net_id() const { return net_id_; }

  Size get_layer_id() const { return layer_id_; }
  void set_layer_id(Size layer_id) { layer_id_ = layer_id; }

  const GtlPointI& get_point() const { return point_; }
  void set_point(const GtlPointI& v) { point_ = v; }

  const GtlRectI& get_shape() const { return shape_; }
  void set_shape(const GtlRectI& v) { shape_ = v; }

  // pin
  bool is_pin_node() const { return !pin_name_.empty(); }
  const std::string& get_pin_name() const { return pin_name_; }
  void set_pin_name(const std::string& v) { pin_name_ = v; }

 private:
  friend class TopoPool;          // only pool can assign local ids
  void set_local_id(Size local_id) { id_ = local_id; }
  Size id_{kMaxSize};

  Size net_id_{kMaxSize};

  Size layer_id_{kMaxSize};
  GtlPointI point_;
  GtlRectI shape_;

  std::string pin_name_;
};

// ============================================================
// TopoEdge
// ============================================================
class TopoEdge {
 public:
  explicit TopoEdge(Size net_id) : net_id_(net_id) {}
  TopoEdge() = default;
  ~TopoEdge() = default;

  // Per-net local id assigned by TopoPool::addNet().
  // For a global flat-pool index, use TopoPool::get_edge_index(edge).
  Size get_id() const { return id_; }
  Size get_net_id() const { return net_id_; }

  // via
  bool is_via() const { return !via_name_.empty(); }
  std::string get_via_name() const { return via_name_; }
  void set_via_name(const std::string& name) { via_name_ = name; }

  // u_ and v_ are GLOBAL node indices (direct index into TopoPool::node_pool_).
  // kMaxSize when no graph node is associated (e.g. special-net edges, vias with no top/btm).
  Size get_u() const { return u_; }
  void set_u(Size u) { u_ = u; }

  Size get_v() const { return v_; }
  void set_v(Size v) { v_ = v; }

  Size get_layer_id() const { return layer_id_; }
  void set_layer_id(Size layer_id) { layer_id_ = layer_id; }

  const GtlRectI& get_shape() const { return shape_; }
  void set_shape(const GtlRectI& v);

  Dbu get_half_width() const { return half_width_; }
  Dbu get_width() const { return width_; }
  Dbu get_length() const { return length_; }
  GtlPointI get_center() const { return center_; }

  const LineSegmentI& get_line_segment() const { return line_seg_; }
  bool is_horz() const { return line_seg_.is_horz; }
  Dbu get_coord() const { return line_seg_.coord; }
  Dbu get_lo() const { return line_seg_.lo; }
  Dbu get_hi() const { return line_seg_.hi; }

 private:
  friend class TopoPool;          // only pool can assign local ids
  void set_local_id(Size local_id) { id_ = local_id; }

  Size id_{kMaxSize};
  Size net_id_{kMaxSize};
  std::string via_name_;
  // GLOBAL node indices into TopoPool::node_pool_.
  // kMaxSize → no associated node (special-net edges, incomplete vias).
  Size u_{kMaxSize};
  Size v_{kMaxSize};

  Size layer_id_{kMaxSize};
  GtlRectI shape_;

  Dbu width_{0};
  Dbu half_width_{0};
  Dbu length_{0};
  GtlPointI center_{};
  LineSegmentI line_seg_{};
};

// ============================================================
// TopoPool
// ============================================================
class TopoPool {
 public:
  TopoPool() = default;
  ~TopoPool() = default;

  // Flat pool access (used by environment, process, capacitance modules)
  std::vector<TopoNode>&       get_node_pool()       { return node_pool_; }
  const std::vector<TopoNode>& get_node_pool() const { return node_pool_; }
  std::vector<TopoEdge>&       get_edge_pool()       { return edge_pool_; }
  const std::vector<TopoEdge>& get_edge_pool() const { return edge_pool_; }

  // Global access by index
  // Use e.get_u() / e.get_v() directly as the argument.
  TopoNode&       get_node(Size node_idx)       { return node_pool_[node_idx]; }
  const TopoNode& get_node(Size node_idx) const { return node_pool_[node_idx]; }
  TopoEdge&       get_edge(Size edge_idx)       { return edge_pool_[edge_idx]; }
  const TopoEdge& get_edge(Size edge_idx) const { return edge_pool_[edge_idx]; }

  // Flat-pool index of an object already stored in the regular pool.
  Size get_node_index(const TopoNode& e) const { return &e - node_pool_.data(); }
  Size get_edge_index(const TopoEdge& e) const { return &e - edge_pool_.data(); }

  // Translate (net id, local per-net id) into a flat regular-pool index.
  // This mapping is only defined for node_pool_/edge_pool_, never for special_edge_pool_.
  Size get_node_index(Size net_id,
                      Size local_node_idx) const {
    const auto& [offset, _] = net_node_ranges_[net_id];
    return offset + local_node_idx;
  }
  Size get_edge_index(Size net_id,
                      Size local_edge_idx) const {
    const auto& [offset, _] = net_edge_ranges_[net_id];
    return offset + local_edge_idx;
  }

  // Per-net spans
  std::span<const TopoNode> get_net_nodes(Size net_id) const;
  std::span<const TopoEdge> get_net_edges(Size net_id) const;
  std::span<TopoNode> get_net_nodes(Size net_id);
  std::span<TopoEdge> get_net_edges(Size net_id);

  std::pair<Size, Size> get_net_node_range(Size net_id) const;
  std::pair<Size, Size> get_net_edge_range(Size net_id) const;
  // Special-net edges are stored in a dedicated pool outside the regular flat edge_pool_.
  // Conventions:
  //   1. get_net_id() == kSpecialNetId
  //   2. get_id() is local only within special_edge_pool_
  //   3. get_u()/get_v() stay kMaxSize because special edges do not participate in the RC graph
  //   4. these edges are used only as calculation context and are excluded from regular
  //      per-net traversal and SPEF connectivity output
  std::vector<TopoEdge>&       get_special_edge_pool()       { return special_edge_pool_; }
  const std::vector<TopoEdge>& get_special_edge_pool() const { return special_edge_pool_; }

  // Pre-allocate all pools to avoid incremental reallocation in addNet().
  // Call once before the addNet() loop with the totals across all nets.
  void reserve(Size net_count,
               Size total_nodes,
               Size total_edges);
  void clear();

  // Build interface (called by TopologyBuilder)
  // Assigns per-net local edge/node ids and appends nodes and edges into the flat pools.
  // Node references stored in edges are expected to already use global pool indices.
  void addNet(std::vector<TopoNode> nodes,
              std::vector<TopoEdge> edges);

  // Append special-net edges into the dedicated special_edge_pool_.
  void addSpecialEdges(std::vector<TopoEdge> edges);

 private:
  static void assignLocalIds(std::vector<TopoNode>& nodes);
  static void assignLocalIds(std::vector<TopoEdge>& edges);

  std::vector<TopoNode> node_pool_;
  std::vector<std::pair<Size, Size>> net_node_ranges_;

  std::vector<TopoEdge> edge_pool_;
  std::vector<std::pair<Size, Size>> net_edge_ranges_;

  std::vector<TopoEdge> special_edge_pool_;
};

}  // namespace ircx
