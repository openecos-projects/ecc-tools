// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
// ***************************************************************************************
#pragma once

#include "Database.hpp"
#include "PlanarCoord.hpp"
#include "Segment.hpp"

namespace irt {

class PRNet;
class PRGridGraph;

struct PREdgeCost
{
  double unit_cost = 0.0;
  bool is_saturated = false;
  bool is_hotspot = false;
  bool is_overflow = false;

  double getTotalCost(double overflow_unit, double congestion_cost) const { return overflow_unit * unit_cost + congestion_cost; }
};

struct PREdgeId
{
  uint32_t value = std::numeric_limits<uint32_t>::max();

  bool operator==(const PREdgeId& other) const { return value == other.value; }
  bool operator!=(const PREdgeId& other) const { return !(*this == other); }
};

struct HashPREdgeId
{
  size_t operator()(const PREdgeId& edge_id) const { return edge_id.value; }
};

using PREdgeSet = std::unordered_set<PREdgeId, HashPREdgeId>;

struct PRGridNode
{
  PlanarRect rect;
  bool is_blocked = false;
};

struct PRGridEdge
{
  int32_t supply = 0;
  int32_t demand = 0;
  std::vector<int32_t> demand_net_idx_list;
  std::vector<int32_t> ignore_net_idx_list;
  double congestion_cost = 0.0;

  int32_t get_overflow() const { return std::max(0, demand - supply); }
  bool isIgnoreNet(int32_t net_idx) const { return std::find(ignore_net_idx_list.begin(), ignore_net_idx_list.end(), net_idx) != ignore_net_idx_list.end(); }
};

class PRGridGraph
{
 public:
  class NetRouteTrial
  {
   public:
    NetRouteTrial(PRGridGraph& graph, PRNet& pr_net);
    NetRouteTrial(const NetRouteTrial&) = delete;
    NetRouteTrial& operator=(const NetRouteTrial&) = delete;

    int32_t get_net_idx() const { return _net_idx; }
    PRNet* get_pr_net() const { return _pr_net; }
    const PREdgeSet& get_old_edge_set() const { return _old_edge_set; }
    const PREdgeSet& get_trial_edge_set() const { return _trial_edge_set; }
    void append(std::span<const Segment<PlanarCoord>> routing_segment_list);
    void commit(std::vector<Segment<PlanarCoord>>&& routing_segment_list);
    void rollback();

   private:
    friend class PRGridGraph;
    PRGridGraph* _graph = nullptr;
    PRNet* _pr_net = nullptr;
    PREdgeSet _old_edge_set;
    int32_t _net_idx = -1;
    // Edges selected by this trial; their demand is added only on commit.
    PREdgeSet _trial_edge_set;
    bool _is_finished = false;
  };

  PRGridGraph() = default;
  ~PRGridGraph() = default;

  void build(Database& database);

  int32_t get_x_size() const { return _x_size; }
  int32_t get_y_size() const { return _y_size; }
  int32_t get_horizontal_x_size() const { return std::max(0, _x_size - 1); }
  int32_t get_vertical_y_size() const { return std::max(0, _y_size - 1); }
  const PRGridNode& get_node(const PlanarCoord& coord) const;
  const PlanarRect& get_gcell_rect(const PlanarCoord& coord) const { return get_node(coord).rect; }
  bool isCellLegal(const PlanarCoord& coord) const;
  const std::vector<PlanarRect>& get_macro_grid_rect_list() const { return _macro_grid_rect_list; }

  PREdgeId get_edge_id(const PlanarCoord& first_coord, const PlanarCoord& second_coord) const;
  const PRGridEdge& get_edge(PREdgeId edge_id) const;
  const PRGridEdge& get_horizontal_edge(int32_t x, int32_t y) const;
  const PRGridEdge& get_vertical_edge(int32_t x, int32_t y) const;
  const std::vector<PRGridEdge>& get_horizontal_edge_list() const { return _horizontal_edge_list; }
  const std::vector<PRGridEdge>& get_vertical_edge_list() const { return _vertical_edge_list; }

  PREdgeCost get_edge_cost(int32_t supply, int32_t demand) const;
  PREdgeCost get_edge_cost(PREdgeId edge_id) const;
  double get_edge_cost(PREdgeId edge_id, double overflow_unit) const;
  double get_segment_cost(int32_t net_idx, const PlanarCoord& first_coord, const PlanarCoord& second_coord,
                          double overflow_unit) const;

  NetRouteTrial begin_route(PRNet& pr_net) { return NetRouteTrial(*this, pr_net); }
  void update_congestion(double overflow_unit);

 private:
  friend class NetRouteTrial;

  PREdgeSet get_edge_set(std::span<const Segment<PlanarCoord>> routing_segment_list) const;
  void commit_route(NetRouteTrial& trial, std::vector<Segment<PlanarCoord>>&& routing_segment_list);
  void update_edge(PREdgeId edge_id, int32_t net_idx, int32_t delta);
  void update_congestion(std::vector<PRGridEdge>& edge_list, int32_t x_size, int32_t y_size, double overflow_unit);
  uint32_t get_horizontal_edge_index(int32_t x, int32_t y) const;
  uint32_t get_vertical_edge_index(int32_t x, int32_t y) const;

  int32_t _x_size = 0;
  int32_t _y_size = 0;
  std::vector<PRGridNode> _node_list;
  std::vector<PlanarRect> _macro_grid_rect_list;
  std::vector<PRGridEdge> _horizontal_edge_list;
  std::vector<PRGridEdge> _vertical_edge_list;
};

}  // namespace irt
