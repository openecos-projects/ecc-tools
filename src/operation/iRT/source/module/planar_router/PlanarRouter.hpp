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
#include "PRComParam.hpp"
#include "PRGridGraph.hpp"
#include "PRNet.hpp"
#include "RTHeader.hpp"

namespace irt {

#define RTPR (irt::PlanarRouter::getInst())

enum class PRRouteMode
{
  kInitialLZPattern,
  kCongestionLZPattern,
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
};

struct PRAStarTask
{
  std::set<PlanarCoord, CmpPlanarCoordByXASC> source_coord_set;
  std::set<PlanarCoord, CmpPlanarCoordByXASC> sink_coord_set;
};

struct PRAStarState
{
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

struct PRAStarSearchArea
{
  PlanarRect search_rect;
  int32_t x_size = 0;
  int32_t y_size = 0;

  void set_search_rect(const PlanarRect& rect)
  {
    search_rect = rect;
    x_size = rect.get_ur_x() - rect.get_ll_x() + 1;
    y_size = rect.get_ur_y() - rect.get_ll_y() + 1;
  }
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
  void initPRNetList();
  void initPRGridGraph();
  void setPRComParam();
  void initPRTaskList();

  // routing flow
  void runRouteFlow();
  void routePRNetList(const std::vector<PRNet*>& pr_net_list, const PRRouteParam& pr_route_param);
  void routePRNet(PRNet& pr_net, const PRRouteParam& pr_route_param);
  std::vector<PRNet*> getOverflowPRNetList();

  // A* route
  std::vector<Segment<PlanarCoord>> routeSegmentListByAStar(PRGridGraph::NetRouteTrial& trial, PRNet& pr_net);
  std::vector<Segment<PlanarCoord>> routeSegmentListByAStarPairwise(PRGridGraph::NetRouteTrial& trial, PRNet& pr_net);
  std::vector<std::pair<int32_t, int32_t>> buildTerminalMstPairList(const std::vector<PlanarCoord>& node_coord_list);
  PlanarCoord getAStarSeedCoord(const std::vector<PlanarCoord>& node_coord_list);
  PlanarRect getAStarBaseRect(const std::vector<PlanarCoord>& node_coord_list);
  PlanarRect getAStarBaseRect(const PlanarRect& base_rect);
  std::vector<PlanarCoord> searchAStarPath(const PRGridGraph::NetRouteTrial& trial, const PRAStarTask& astar_task,
                                           const PRAStarSearchArea& search_area);
  int32_t getAStarStateIndex(const PRAStarSearchArea& search_area, const PlanarCoord& coord, bool is_horizontal);
  PlanarCoord getAStarStateCoord(const PRAStarSearchArea& search_area, int32_t state_idx);
  inline int32_t getAStarEstimatedCost(const PRAStarTask& astar_task, const PlanarCoord& coord);
  std::vector<Segment<PlanarCoord>> getRoutingSegmentListByCoordList(const std::vector<PlanarCoord>& coord_list);

  // pattern route
  std::vector<Segment<PlanarCoord>> routeSegmentListByPattern(PRGridGraph::NetRouteTrial& trial, PRNet& pr_net, PRRouteMode pr_route_mode);
  bool needCongestionAware(const PRGridGraph::NetRouteTrial& trial);
  std::vector<Segment<PlanarCoord>> getPlanarTopoList(const PRGridGraph::NetRouteTrial& trial, PRNet& pr_net, PRTopoMode pr_topo_mode);
  std::vector<Segment<PlanarCoord>> getRoutingSegmentListByPattern(const PRGridGraph::NetRouteTrial& trial, Segment<PlanarCoord>& planar_topo,
                                                                  PRRouteMode pr_route_mode);
  bool isBetterCandidate(double corner_weight, const PRCandidate& candidate, const PRCandidate& best_candidate);
  std::vector<PRCandidate> getPRCandidateListByTopo(int32_t expand_step_num, Segment<PlanarCoord>& planar_topo, PRRouteMode pr_route_mode);
  void addPRCandidate(std::vector<PRCandidate>& pr_candidate_list, Segment<PlanarCoord>& planar_topo, std::initializer_list<PlanarCoord> inflection_list);
  void addPRCandidateListByStraight(std::vector<PRCandidate>& pr_candidate_list, Segment<PlanarCoord>& planar_topo);
  void addPRCandidateListByLPattern(std::vector<PRCandidate>& pr_candidate_list, Segment<PlanarCoord>& planar_topo);
  void addPRCandidateListByZPattern(std::vector<PRCandidate>& pr_candidate_list, Segment<PlanarCoord>& planar_topo);
  void addPRCandidateListByUPattern(std::vector<PRCandidate>& pr_candidate_list, int32_t expand_step_num, Segment<PlanarCoord>& planar_topo);
  void addPRCandidateListByInner3Bends(std::vector<PRCandidate>& pr_candidate_list, Segment<PlanarCoord>& planar_topo);
  void addPRCandidateListByOuter3Bends(std::vector<PRCandidate>& pr_candidate_list, int32_t expand_step_num, Segment<PlanarCoord>& planar_topo);
  void updatePRCandidate(const PRGridGraph::NetRouteTrial& trial, PRCandidate& pr_candidate);

  // result
  MTree<PlanarCoord> getCoordTree(PRNet& pr_net, std::vector<Segment<PlanarCoord>>& routing_segment_list);
  void uploadNetList();

#if 1  // exhibit
  void updateSummary();
  void printSummary();
  void outputGuide();
  void outputNetCSV();
  void outputOverflowCSV();
  void outputUsageCSV();
  void outputCongestionCostCSV();
#endif

#if 1  // debug
  void debugPlotPRGraph(std::string flag);
#endif

  // data
  std::vector<PRNet> _pr_net_list;
  std::vector<PRNet*> _pr_task_list;
  PRGridGraph _grid_graph;
  PRComParam _pr_com_param;
};

}  // namespace irt
