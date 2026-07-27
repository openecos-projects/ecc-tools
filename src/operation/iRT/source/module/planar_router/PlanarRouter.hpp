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

namespace irt {

#define RTPR (irt::PlanarRouter::getInst())

class PlanarRouter
{
 public:
  static void initInst();
  static PlanarRouter& getInst();
  static void destroyInst();
  // function
  void generate();

 private:
  // self
  static PlanarRouter* _pr_instance;

  PlanarRouter() = default;
  PlanarRouter(const PlanarRouter& other) = delete;
  PlanarRouter(PlanarRouter&& other) = delete;
  ~PlanarRouter() = default;
  PlanarRouter& operator=(const PlanarRouter& other) = delete;
  PlanarRouter& operator=(PlanarRouter&& other) = delete;
  // function
  PRModel initPRModel();
  std::vector<PRNet> convertToPRNetList(std::vector<Net>& net_list);
  PRNet convertToPRNet(Net& net);
  void setPRComParam(PRModel& pr_model);
  void initPRTaskList(PRModel& pr_model);
  void buildPRNodeMap(PRModel& pr_model);
  void buildPRNodeNeighbor(PRModel& pr_model);
  void buildOrientSupply(PRModel& pr_model);
  void buildPRMacroRegion(PRModel& pr_model);
  void generatePRModel(PRModel& pr_model);
  void routePRTask(PRModel& pr_model, PRNet* pr_net);
  void initSingleTask(PRModel& pr_model, PRNet* pr_net);
  std::vector<Segment<PlanarCoord>> getRoutingSegmentList(PRModel& pr_model);
  using PRShadowDemandMap = std::map<PlanarCoord, std::set<Orientation>, CmpPlanarCoordByXASC>;
  uint8_t getShadowOrientMask(const PRShadowDemandMap* shadow_demand_map, const PlanarCoord& coord);
  bool isBetterCandidate(PRModel& pr_model, PRCandidate& candidate, PRCandidate& current_best);
  std::vector<PRCandidate> getPRCandidateListByTopo(PRModel& pr_model, int32_t topo_idx, Segment<PlanarCoord>& planar_topo,
                                                    const std::set<PlanarCoord, CmpPlanarCoordByXASC>& terminal_coord_set,
                                                    const PRShadowDemandMap* shadow_demand_map = nullptr);
  std::vector<PRCandidate> getPRCandidateList(PRModel& pr_model, std::vector<Segment<PlanarCoord>>& planar_topo_list);
  std::vector<Segment<PlanarCoord>> getPlanarTopoList(PRModel& pr_model);
  std::set<PlanarCoord, CmpPlanarCoordByXASC> getCurrTerminalCoordSet(PRModel& pr_model);
  bool isMacroForbiddenCoord(PRModel& pr_model, const PlanarCoord& coord);
  bool isSameMacroBodyCoord(PRModel& pr_model, const PlanarCoord& first_coord, const PlanarCoord& second_coord);
  int32_t getPRMacroRegionId(PRModel& pr_model, const PlanarCoord& coord);
  bool isMacroBlockedSegment(PRModel& pr_model, Segment<PlanarCoord>& planar_segment,
                             const std::set<PlanarCoord, CmpPlanarCoordByXASC>& terminal_coord_set);
  bool isMacroBlockedRoutingSegmentList(PRModel& pr_model, std::vector<Segment<PlanarCoord>>& routing_segment_list,
                                        const std::set<PlanarCoord, CmpPlanarCoordByXASC>& terminal_coord_set);
  struct PRAStarEscapeNode
  {
    PlanarCoord terminal_coord;
    PlanarCoord route_coord;
    std::vector<Segment<PlanarCoord>> stub_segment_list;
    double cost = 0;
  };
  struct PRAStarNodeState
  {
    uint64_t search_stamp = 0;
    bool closed = false;
    int32_t parent_idx = -1;
    double known_cost = DBL_MAX;
  };
  struct PRAStarNodeCostCache
  {
    uint64_t context_stamp = 0;
    uint8_t valid_mask = 0;
    std::array<double, 2> cost = {0, 0};
  };
  struct PRAStarQueueNode
  {
    int32_t node_idx = -1;
    double known_cost = 0;
    double estimated_cost = 0;
    double getTotalCost() const { return known_cost + estimated_cost; }
  };
  struct PRAStarPairTask
  {
    int32_t start_idx = -1;
    int32_t end_idx = -1;
    PlanarRect search_rect;
    double lower_bound = 0;
    bool need_search = true;
  };
  struct PRAStarWorkspace
  {
    PlanarRect workspace_rect;
    int32_t x_size = 0;
    int32_t y_size = 0;
    uint64_t search_stamp = 0;
    uint64_t context_stamp = 0;
    std::vector<PRAStarNodeState> node_state_list;
    std::vector<PRAStarNodeCostCache> node_cost_list;
    std::vector<PRAStarQueueNode> open_heap;
  };
  PRAStarWorkspace _astar_workspace;
  std::vector<PRAStarEscapeNode> getAStarEscapeNodeList(PRModel& pr_model, const PlanarCoord& terminal_coord,
                                                        const std::set<PlanarCoord, CmpPlanarCoordByXASC>& terminal_coord_set,
                                                        const PRShadowDemandMap* shadow_demand_map = nullptr);
  std::vector<Segment<PlanarCoord>> getRoutingSegmentListByAStarWithEscape(
      PRModel& pr_model, Segment<PlanarCoord>& planar_topo,
      const std::set<PlanarCoord, CmpPlanarCoordByXASC>& terminal_coord_set,
      const PRShadowDemandMap* shadow_demand_map = nullptr);
  double getLegalRoutingSegmentListScore(PRModel& pr_model, std::vector<Segment<PlanarCoord>>& routing_segment_list,
                                         const PRShadowDemandMap* shadow_demand_map = nullptr);
  void prepareAStarWorkspace(PRModel& pr_model, const PlanarRect& workspace_rect, PRAStarWorkspace& workspace);
  int32_t getAStarNodeIndex(const PRAStarWorkspace& workspace, const PlanarCoord& coord);
  PlanarCoord getAStarNodeCoord(const PRAStarWorkspace& workspace, int32_t node_idx);
  bool searchRoutingSegmentByAStar(PRModel& pr_model, const PlanarCoord& start_coord, const PlanarCoord& end_coord,
                                   const PlanarRect& search_rect,
                                   const std::set<PlanarCoord, CmpPlanarCoordByXASC>& terminal_coord_set,
                                   const PRShadowDemandMap* shadow_demand_map, PRAStarWorkspace& workspace,
                                   std::vector<Segment<PlanarCoord>>& routing_segment_list);
  PlanarRect getAStarSearchRect(PRModel& pr_model, Segment<PlanarCoord>& planar_topo);
  bool isAStarAccessibleCoord(PRModel& pr_model, const PlanarCoord& coord, Segment<PlanarCoord>& planar_topo,
                              const std::set<PlanarCoord, CmpPlanarCoordByXASC>& terminal_coord_set);
  double getAStarStepCost(PRModel& pr_model, const PlanarCoord& start_coord, const PlanarCoord& end_coord,
                          const PlanarCoord& parent_coord, const PRShadowDemandMap* shadow_demand_map,
                          PRAStarWorkspace& workspace);
  double getAStarNodeCost(PRModel& pr_model, const PlanarCoord& coord, Direction direction,
                          const PRShadowDemandMap* shadow_demand_map, PRAStarWorkspace& workspace);
  double getAStarEstimateCost(PRModel& pr_model, const PlanarCoord& start_coord, const PlanarCoord& end_coord);
  std::vector<Segment<PlanarCoord>> getRoutingSegmentListByCoordList(std::vector<PlanarCoord>& coord_list);
  bool isLongObliqueTopo(PRModel& pr_model, Segment<PlanarCoord>& planar_topo);
  std::vector<std::vector<Segment<PlanarCoord>>> getRoutingSegmentListByStraight(PRModel& pr_model, Segment<PlanarCoord>& planar_topo);
  std::vector<std::vector<Segment<PlanarCoord>>> getRoutingSegmentListByLPattern(PRModel& pr_model, Segment<PlanarCoord>& planar_topo);
  std::vector<std::vector<Segment<PlanarCoord>>> getRoutingSegmentListByZPattern(PRModel& pr_model, Segment<PlanarCoord>& planar_topo);
  std::vector<int32_t> getMidIndexList(int32_t first_idx, int32_t second_idx);
  std::vector<std::vector<Segment<PlanarCoord>>> getRoutingSegmentListByUPattern(PRModel& pr_model, Segment<PlanarCoord>& planar_topo);
  std::vector<std::vector<Segment<PlanarCoord>>> getRoutingSegmentListByInner3Bends(PRModel& pr_model, Segment<PlanarCoord>& planar_topo);
  std::vector<std::vector<Segment<PlanarCoord>>> getRoutingSegmentListByLowCostLane3Bends(
      PRModel& pr_model, Segment<PlanarCoord>& planar_topo,
      const std::set<PlanarCoord, CmpPlanarCoordByXASC>& terminal_coord_set,
      const PRShadowDemandMap* shadow_demand_map = nullptr);
  double getPatternSegmentScore(PRModel& pr_model, Segment<PlanarCoord>& segment,
                                const std::set<PlanarCoord, CmpPlanarCoordByXASC>& terminal_coord_set,
                                const PRShadowDemandMap* shadow_demand_map = nullptr);
  double getPatternSegmentCost(PRModel& pr_model, Segment<PlanarCoord>& segment,
                               const PRShadowDemandMap* shadow_demand_map = nullptr);
  std::vector<std::vector<Segment<PlanarCoord>>> getRoutingSegmentListByOuter3Bends(PRModel& pr_model, Segment<PlanarCoord>& planar_topo);
  void updatePRCandidate(PRModel& pr_model, PRCandidate& pr_candidate,
                         const PRShadowDemandMap* shadow_demand_map = nullptr);
  MTree<PlanarCoord> getCoordTree(PRModel& pr_model, std::vector<Segment<PlanarCoord>>& routing_segment_list);
  void uploadNetResult(PRModel& pr_model, MTree<PlanarCoord>& coord_tree);
  void resetSingleTask(PRModel& pr_model);

#if 1  // update env
  void updateDemandToGraph(PRModel& pr_model, ChangeType change_type, MTree<PlanarCoord>& coord_tree);
  void addCandidateToShadow(PRShadowDemandMap& shadow_map, PRCandidate& pr_candidate);
#endif

#if 1  // exhibit
  void updateSummary(PRModel& pr_model);
  void printSummary(PRModel& pr_model);
  void outputGuide(PRModel& pr_model);
  void outputNetCSV(PRModel& pr_model);
  void outputOverflowCSV(PRModel& pr_model);
  void outputJson(PRModel& pr_model);
  std::string outputNetJson(PRModel& pr_model);
  std::string outputOverflowJson(PRModel& pr_model);
  std::string outputSummaryJson(PRModel& pr_model);
#endif

#if 1  // debug
  void debugPlotPRModel(PRModel& pr_model, std::string flag);
  void debugCheckPRModel(PRModel& pr_model);
#endif
};

}  // namespace irt
