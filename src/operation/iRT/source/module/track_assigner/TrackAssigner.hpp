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

#include "ChangeType.hpp"
#include "Config.hpp"
#include "DataManager.hpp"
#include "Database.hpp"
#include "Net.hpp"
#include "TAModel.hpp"
#include "TAPanel.hpp"

namespace irt {

#define RTTA (irt::TrackAssigner::getInst())

class TrackAssigner
{
 public:
  static void initInst();
  static TrackAssigner& getInst();
  static void destroyInst();
  // function
  void assign();

 private:
  struct DetailedEnvironment;

  // self
  static TrackAssigner* _ta_instance;

  TrackAssigner() = default;
  TrackAssigner(const TrackAssigner& other) = delete;
  TrackAssigner(TrackAssigner&& other) = delete;
  ~TrackAssigner() = default;
  TrackAssigner& operator=(const TrackAssigner& other) = delete;
  TrackAssigner& operator=(TrackAssigner&& other) = delete;
  // function
  TAModel initTAModel();
  std::vector<TANet> convertToTANetList(std::vector<Net>& net_list);
  TANet convertToTANet(Net& net);
  void setTAComParam(TAModel& ta_model);
  void initTAPanelMap(TAModel& ta_model);
  void buildPanelSchedule(TAModel& ta_model);
  TAPanelId getTAPanelId(const LayerCoord& coord);
  void splitDetailedResult(TAModel& ta_model);
  void assignTAPanelMap(TAModel& ta_model);
  DetailedEnvironment buildDetailedEnvironment(TAModel& ta_model);
  void buildTAPanelEnvironment(TAModel& ta_model, const std::vector<TAPanelId>& ta_panel_id_list, DetailedEnvironment& detailed_env);
  void routeTAPanelList(TAModel& ta_model, const std::vector<TAPanelId>& ta_panel_id_list);
  void updateTAPanelEnvironment(TAModel& ta_model, const std::vector<TAPanelId>& ta_panel_id_list, DetailedEnvironment& detailed_env);
  void freeTAPanelList(TAModel& ta_model, const std::vector<TAPanelId>& ta_panel_id_list);
  void initTATaskList(TAModel& ta_model, TAPanel& ta_panel);
  void buildViolation(TAPanel& ta_panel);
  bool needRouting(TAPanel& ta_panel);
  void buildFixedRect(TAPanel& ta_panel);
  void buildPanelTrackAxis(TAPanel& ta_panel);
  void buildTANodeMap(TAPanel& ta_panel);
  void buildTANodeNeighbor(TAPanel& ta_panel);
  void buildOrientNetMap(TAPanel& ta_panel);
  void routeTAPanel(TAPanel& ta_panel);
  std::vector<TATask*> initTaskSchedule(TAPanel& ta_panel);
  void routeTATask(TAPanel& ta_panel, TATask* ta_task);
  void initSingleTask(TAPanel& ta_panel, TATask* ta_task);
  bool isConnectedAllEnd(TAPanel& ta_panel);
  void routeSinglePath(TAPanel& ta_panel);
  void initPathHead(TAPanel& ta_panel);
  bool searchEnded(TAPanel& ta_panel);
  void expandSearching(TAPanel& ta_panel);
  void resetPathHead(TAPanel& ta_panel);
  void updatePathResult(TAPanel& ta_panel);
  std::vector<Segment<LayerCoord>> getRoutingSegmentListByNode(TANode* node);
  void resetStartAndEnd(TAPanel& ta_panel);
  void resetSinglePath(TAPanel& ta_panel);
  void updateTaskResult(TAPanel& ta_panel);
  std::vector<Segment<LayerCoord>> getRoutingSegmentList(TAPanel& ta_panel);
  void resetSingleTask(TAPanel& ta_panel);
  void pushToOpenList(TAPanel& ta_panel, TANode* curr_node);
  TANode* popFromOpenList(TAPanel& ta_panel);
  double getKnownCost(TAPanel& ta_panel, TANode* start_node, TANode* end_node);
  double getNodeCost(TAPanel& ta_panel, TANode* curr_node, Orientation orientation);
  double getKnownWireCost(TAPanel& ta_panel, TANode* start_node, TANode* end_node);
  double getKnownViaCost(TAPanel& ta_panel, TANode* start_node, TANode* end_node);
  double getEstimateCostToEnd(TAPanel& ta_panel, TANode* curr_node);
  double getEstimateCost(TAPanel& ta_panel, TANode* start_node, TANode* end_node);
  double getEstimateWireCost(TAPanel& ta_panel, TANode* start_node, TANode* end_node);
  double getEstimateViaCost(TAPanel& ta_panel, TANode* start_node, TANode* end_node);
  void updateViolationList(TAPanel& ta_panel);
  std::vector<Violation> getViolationList(TAPanel& ta_panel);
  std::vector<Violation> getViolationListByShort(TAPanel& ta_panel, std::map<int32_t, std::vector<PlanarRect>>& env_net_rect_map,
                                                 std::map<int32_t, std::vector<PlanarRect>>& result_net_rect_map);
  void updateTaskSchedule(TAPanel& ta_panel, std::vector<TATask*>& routing_task_list);
  void updateTAPanel(TAPanel& ta_panel);
  void uploadViolation(TAPanel& ta_panel);
  void freeTAPanel(TAPanel& ta_panel);
  void updateTAModel(TAModel& ta_model);
  void uploadTAModel(TAModel& ta_model);
  int32_t getViolationNum(TAModel& ta_model);

#if 1  // update env
  void updateFixedRectToGraph(TAPanel& ta_panel, ChangeType change_type, int32_t net_idx, EXTLayerRect* fixed_rect, bool is_routing);
  void updateFixedRectToGraph(TAPanel& ta_panel, ChangeType change_type, int32_t net_idx, LayerRect& rect, bool is_routing);
  void updateRoutedRectToGraph(TAPanel& ta_panel, ChangeType change_type, int32_t net_idx, Segment<LayerCoord>& segment);
  void addViolationToGraph(TAPanel& ta_panel, Violation& violation);
  void addViolationToGraph(TAPanel& ta_panel, LayerRect& searched_rect, std::vector<Segment<LayerCoord>>& overlap_segment_list);
  void updateNetShapeToGraph(TAPanel& ta_panel, ChangeType change_type, NetShape& net_shape, bool is_fixed);
  void updateNodeNetToGraph(TANode& ta_node, ChangeType change_type, int32_t net_idx, Orientation orientation, bool is_fixed);
#endif

#if 1  // exhibit
  void updateSummary(TAModel& ta_model);
  void printSummary(TAModel& ta_model);
  void outputNetCSV(TAModel& ta_model);
  void outputViolationCSV(TAModel& ta_model);
#endif

#if 1  // debug
  void debugPlotTAModel(TAModel& ta_model, std::string flag);
  void debugCheckTAPanel(TAPanel& ta_panel);
  void debugPlotTAPanel(TAPanel& ta_panel, std::string flag);
#endif
};

}  // namespace irt
