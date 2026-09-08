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

#include "Config.hpp"
#include "DataManager.hpp"
#include "Database.hpp"
#include "Monitor.hpp"
#include "PRCandidate.hpp"
#include "PRModel.hpp"
#include "RTHeader.hpp"

namespace irt {

#define RTPR (irt::PlanarRouter::getInst())

struct PREdgeCost
{
  double unit_cost = 0.0;
  bool is_saturated = false;
  bool is_hotspot = false;
  bool is_overflow = false;

  double getTotalCost(double overflow_unit, double congestion_cost) const { return overflow_unit * unit_cost + congestion_cost; }
};

enum class PRRouteMode
{
  kLZPattern,
  kAllPattern,
  kAStar
};

enum class PRTopoMode
{
  kNormal,
  kCongestion
};

struct PRRouteParam
{
  const char* route_name;
  PRRouteMode pr_route_mode;
  PRTopoMode pr_topo_mode;
  bool is_partial_rip_up = false;
};

struct PRPartialRoute
{
  std::vector<Segment<PlanarCoord>> kept_segment_list;
  std::vector<Segment<PlanarCoord>> rip_up_segment_list;
  std::vector<Segment<PlanarCoord>> planar_topo_list;
};

struct PRAStarTask
{
  PlanarCoord start_coord;
  PlanarCoord end_coord;
  PlanarRect owned_rect;
  bool has_owned_rect = false;
};

struct PRAStarState
{
  uint64_t search_stamp = 0;
  bool closed = false;
  int32_t parent_state_idx = -1;
  double known_cost = DBL_MAX;
};

struct PRAStarQueueNode
{
  int32_t state_idx = -1;
  double known_cost = 0;
  double total_cost = 0;
};

struct CmpPRAStarQueueNode
{
  bool operator()(const PRAStarQueueNode& a, const PRAStarQueueNode& b) const
  {
    if (a.total_cost != b.total_cost) {
      return a.total_cost > b.total_cost;
    }
    if (a.known_cost != b.known_cost) {
      return a.known_cost < b.known_cost;
    }
    return a.state_idx > b.state_idx;
  }
};

struct PRAStarWorkspace
{
  PlanarRect workspace_rect;
  int32_t x_size = 0;
  int32_t y_size = 0;
  uint64_t search_stamp = 0;
  std::vector<PRAStarState> state_list;
  std::vector<PRAStarQueueNode> open_heap;
};

class PlanarRouter
{
 public:
  static void initInst();
  static PlanarRouter& getInst();
  static void destroyInst();
  // function
  void generate();

 private:
  // singleton
  static PlanarRouter* _pr_instance;
  PlanarRouter() = default;
  PlanarRouter(const PlanarRouter& other) = delete;
  PlanarRouter(PlanarRouter&& other) = delete;
  ~PlanarRouter() = default;
  PlanarRouter& operator=(const PlanarRouter& other) = delete;
  PlanarRouter& operator=(PlanarRouter&& other) = delete;

  // initialization
  PRModel initPRModel();
  void setPRComParam(PRModel& pr_model);
  void initPRTaskList(PRModel& pr_model);
  void buildPlanarRoutingEdgeMap();
  void initMacroGridRectList();

  // routing edge
  PREdgeCost getRoutingEdgeCost(int32_t supply, int32_t demand);
  PREdgeCost getRoutingEdgeCost(const RoutingEdge& routing_edge);
  double getTopologyEdgeCost(RoutingEdge& routing_edge, int32_t net_idx, double overflow_unit, const std::unordered_set<RoutingEdge*>& routing_edge_set);
  double getTopologySegmentCost(double overflow_unit, const PRNet& pr_net, const PlanarCoord& first_coord, const PlanarCoord& second_coord);
  void updateRoutingEdgeToGraph(RoutingEdge& routing_edge, PREdgeCost& edge_cost, int32_t curr_net_idx, ChangeType change_type,
                                std::unordered_set<RoutingEdge*>& routing_edge_set);
  void updateRoutingSegmentListToGraph(PRNet& pr_net, std::span<const Segment<PlanarCoord>> routing_segment_list, ChangeType change_type);

  // routing flow
  void runRouteFlow(PRModel& pr_model);
  void routePRNetList(PRModel& pr_model, const std::vector<PRNet*>& pr_net_list, const PRRouteParam& pr_route_param);
  void routePRNet(PRModel& pr_model, PRNet& pr_net, const PRRouteParam& pr_route_param);
  void splitLongPlanarTopoList(const PRComParam& pr_com_param, PRNet& pr_net, std::vector<Segment<PlanarCoord>>& planar_topo_list);
  bool routePlanarTopoList(PRModel& pr_model, PRNet& pr_net, std::vector<Segment<PlanarCoord>>& planar_topo_list, PRRouteMode pr_route_mode,
                           std::vector<Segment<PlanarCoord>>& routing_segment_list);
  void updateCongestion(PRModel& pr_model);
  std::vector<PRNet*> getOverflowPRNetList(PRModel& pr_model);
  PRPartialRoute getPartialRoute(PRNet& pr_net);
  void expandRipUpGuard(const std::vector<Segment<PlanarCoord>>& unit_segment_list,
                        const std::map<PlanarCoord, std::vector<int32_t>, CmpPlanarCoordByXASC>& coord_edge_idx_map,
                        std::vector<int32_t>& rip_up_distance_list);
  std::vector<Segment<PlanarCoord>> getOverflowPlanarTopoList(PRNet& pr_net, const std::vector<Segment<PlanarCoord>>& unit_segment_list,
                                                              const std::map<PlanarCoord, std::vector<int32_t>, CmpPlanarCoordByXASC>& coord_edge_idx_map,
                                                              const std::vector<int32_t>& rip_up_distance_list);
  bool shouldUseCongestionFlute(double overflow_unit, const PRNet& pr_net, size_t unique_pin_num);
  std::vector<Segment<PlanarCoord>> getPlanarTopoList(double overflow_unit, PRNet& pr_net, PRTopoMode pr_topo_mode);

  // A* route
  std::vector<Segment<PlanarCoord>> getRoutingSegmentListByAStar(const PRComParam& pr_com_param, const PRNet& pr_net, const Segment<PlanarCoord>& planar_topo,
                                                                 const std::vector<Segment<PlanarCoord>>& routed_segment_list);
  PRAStarTask initPRAStarTask(const Segment<PlanarCoord>& planar_topo, const std::vector<Segment<PlanarCoord>>& routed_segment_list);
  bool prepareAStarWorkspace(const PlanarRect& workspace_rect, PRAStarWorkspace& workspace);
  int32_t getAStarStateIndex(const PRAStarWorkspace& workspace, const PlanarCoord& coord, bool is_horizontal);
  PlanarCoord getAStarStateCoord(const PRAStarWorkspace& workspace, int32_t state_idx);
  PRAStarState& getAStarState(PRAStarWorkspace& workspace, int32_t state_idx);
  int32_t getAStarEstimatedCost(const PRAStarTask& astar_task, const PlanarCoord& coord, bool has_owned_edge);
  bool searchRoutingSegmentByAStar(const PRComParam& pr_com_param, const PRNet& pr_net, const PRAStarTask& astar_task, PRAStarWorkspace& workspace,
                                   std::vector<Segment<PlanarCoord>>& routing_segment_list);
  PlanarRect getAStarBaseRect(const Segment<PlanarCoord>& planar_topo);
  std::vector<Segment<PlanarCoord>> getRoutingSegmentListByCoordList(const std::vector<PlanarCoord>& coord_list);

  // pattern route
  void getRoutingSegmentListByPattern(const PRComParam& pr_com_param, const PRNet& pr_net, Segment<PlanarCoord>& planar_topo, PRRouteMode pr_route_mode,
                                      std::vector<Segment<PlanarCoord>>& routing_segment_list);
  bool isBetterCandidate(double corner_weight, const PRCandidate& candidate, const PRCandidate& best_candidate);
  std::vector<PRCandidate> getPRCandidateListByTopo(int32_t expand_step_num, Segment<PlanarCoord>& planar_topo, PRRouteMode pr_route_mode);
  void addPRCandidate(std::vector<PRCandidate>& pr_candidate_list, Segment<PlanarCoord>& planar_topo, std::initializer_list<PlanarCoord> inflection_list);
  void addPRCandidateListByStraight(std::vector<PRCandidate>& pr_candidate_list, Segment<PlanarCoord>& planar_topo);
  void addPRCandidateListByLPattern(std::vector<PRCandidate>& pr_candidate_list, Segment<PlanarCoord>& planar_topo);
  void addPRCandidateListByZPattern(std::vector<PRCandidate>& pr_candidate_list, Segment<PlanarCoord>& planar_topo);
  void addPRCandidateListByUPattern(std::vector<PRCandidate>& pr_candidate_list, int32_t expand_step_num, Segment<PlanarCoord>& planar_topo);
  void addPRCandidateListByInner3Bends(std::vector<PRCandidate>& pr_candidate_list, Segment<PlanarCoord>& planar_topo);
  void addPRCandidateListByOuter3Bends(std::vector<PRCandidate>& pr_candidate_list, int32_t expand_step_num, Segment<PlanarCoord>& planar_topo);
  void updatePRCandidate(double overflow_unit, const PRNet& pr_net, PRCandidate& pr_candidate);

  // result
  MTree<PlanarCoord> getCoordTree(PRNet& pr_net, std::vector<Segment<PlanarCoord>>& routing_segment_list);
  void uploadNetList(PRModel& pr_model, const std::vector<PRNet*>& pr_net_list);

  // exhibit
  void updateSummary(PRModel& pr_model);
  void printSummary(PRModel& pr_model);
  void outputGuide(PRModel& pr_model);
  void outputNetCSV(PRModel& pr_model);
  void outputOverflowCSV(PRModel& pr_model);
  void outputUsageCSV(PRModel& pr_model);
  void outputCongestionCostCSV(PRModel& pr_model);

  // debug
  void debugPlotPRModel(std::string flag);

  // data
  PRAStarWorkspace _astar_workspace;
  GridMap<PREdgeCost> _routing_h_edge_cost_map;
  GridMap<PREdgeCost> _routing_v_edge_cost_map;
  std::vector<PlanarRect> _macro_grid_rect_list;
};

}  // namespace irt
