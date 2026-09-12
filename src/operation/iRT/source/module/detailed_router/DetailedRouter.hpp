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

#include <optional>

#include "ChangeType.hpp"
#include "Config.hpp"
#include "DRBoxId.hpp"
#include "DRIterParam.hpp"
#include "DRModel.hpp"
#include "DRNet.hpp"
#include "DRNode.hpp"
#include "DRPatch.hpp"
#include "DataManager.hpp"
#include "Database.hpp"
#include "Net.hpp"
#include "RTHeader.hpp"

namespace irt {

#define RTDR (irt::DetailedRouter::getInst())

class DETask;
class DRShadow;

class DetailedRouter
{
 public:
  static void initInst();
  static DetailedRouter& getInst();
  static void destroyInst();
  // function
  void route();

 private:
  // self
  static DetailedRouter* _dr_instance;

  DetailedRouter() = default;
  DetailedRouter(const DetailedRouter& other) = delete;
  DetailedRouter(DetailedRouter&& other) = delete;
  ~DetailedRouter() = default;
  DetailedRouter& operator=(const DetailedRouter& other) = delete;
  DetailedRouter& operator=(DetailedRouter&& other) = delete;
  // function
  DRModel initDRModel();
  std::vector<DRNet> convertToDRNetList(std::vector<Net>& net_list);
  DRNet convertToDRNet(Net& net);
  void readDRModel(DRModel& dr_model);
  void routeDRModel(DRModel& dr_model);
  void setDRIterParam(DRModel& dr_model, int32_t iter, DRIterParam& dr_iter_param);
  void initDRBoxMap(DRModel& dr_model);
  void resetRoutingState(DRModel& dr_model);
  void buildBoxSchedule(DRModel& dr_model);
  void splitNetResult(DRModel& dr_model);
  std::set<DRBoxId, CmpDRBoxId> getDRBoxIdSet(DRModel& dr_model, PlanarRect real_rect);
  void routeDRBoxMap(DRModel& dr_model);
  void routeDRBoxList(DRModel& dr_model, const std::vector<DRBoxId>& dr_box_id_list);
  void routeDRBox(DRModel& dr_model, DRBox& dr_box);
  void freeDRBoxMap(DRModel& dr_model);
  void updateRouteViolation(DRModel& dr_model, const std::vector<DRBoxId>& dr_box_id_list);
  void buildFixedRect(DRBox& dr_box);
  void buildAccessPoint(DRBox& dr_box);
  void buildNetEnvironment(DRModel& dr_model, const std::vector<DRBoxId>& dr_box_id_list);
  void buildDirtyNetEnvironment(DRModel& dr_model, const std::vector<DRBoxId>& dr_box_id_list);
  void addNetResultToEnvironment(DRModel& dr_model, GridMap<bool>& active_box_map, GridMap<omp_lock_t>& environment_lock_map, int32_t net_idx,
                                 Segment<LayerCoord>& segment);
  void addNetPatchToEnvironment(DRModel& dr_model, GridMap<bool>& active_box_map, GridMap<omp_lock_t>& environment_lock_map, int32_t net_idx,
                                EXTLayerRect& patch);
  void initDRTaskList(DRModel& dr_model, DRBox& dr_box);
  void buildNetTaskList(DRModel& dr_model, DRBox& dr_box, int32_t net_idx);
  void buildRouteViolation(DRModel& dr_model, const std::vector<DRBoxId>& dr_box_id_list);
  bool needRouting(DRBox& dr_box);
  void buildRefineTaskList(DRModel& dr_model, DRBox& dr_box);
  void selectRefineNetList(DRBox& dr_box);
  void refineCleanNets(DRBox& dr_box);
  bool hasCoveredOutsideBoxAccessPoint(DRBox& dr_box, int32_t net_idx);
  bool coverRefineTerminals(DRBox& dr_box, int32_t net_idx, std::vector<Segment<LayerCoord>>& old_result_list,
                            const std::vector<EXTLayerRect>& old_patch_list);
  bool hasNetBoxViolation(DRBox& dr_box, int32_t net_idx, const std::vector<Violation>& violation_list);
  double getNetResultCost(const std::vector<Segment<LayerCoord>>& result_list, const std::vector<EXTLayerRect>& patch_list,
                          const DRIterParam& dr_iter_param);
  double getResultCost(const std::map<int32_t, std::vector<Segment<LayerCoord>>>& net_result_map,
                       const std::map<int32_t, std::vector<EXTLayerRect>>& net_patch_map, const DRIterParam& dr_iter_param);
  void buildDRBoxGraph(DRBox& dr_box);
  void buildBoxTrackAxis(DRBox& dr_box);
  void buildLayerNodeMap(DRBox& dr_box);
  void buildLayerShadowMap(DRBox& dr_box);
  void buildDRNodeNeighbor(DRBox& dr_box);
  void buildOrientNetMap(DRBox& dr_box);
  void buildNetShadowMap(DRBox& dr_box);
  void buildDRShapeIndex(DRBox& dr_box);
  void updateNetShapeIndex(DRBox& dr_box, int32_t net_idx);
  void exemptPinShape(DRModel& dr_model, DRBox& dr_box);
  void routeDRBox(DRBox& dr_box);
  std::vector<int32_t> initTaskSchedule(DRBox& dr_box, std::vector<int32_t>& net_route_order_list);
  void updateGraph(DRBox& dr_box, ChangeType change_type, int32_t net_idx, std::vector<Segment<LayerCoord>>& segment_list,
                   std::vector<EXTLayerRect>& patch_list);
  void resetDRNetResult(DRBox& dr_box, int32_t net_idx, const std::vector<DRTask*>& net_task_list);
  void routeDRNet(DRBox& dr_box, int32_t net_idx);
  void routeDRTask(DRBox& dr_box, DRTask* dr_task);
  void initSingleRouteTask(DRBox& dr_box, DRTask* dr_task);
  bool isConnectedAllEnd(DRBox& dr_box);
  bool routeSinglePath(DRBox& dr_box);
  void initPathHead(DRBox& dr_box);
  bool reachEnd(DRBox& dr_box);
  void expandSearching(DRBox& dr_box);
  bool isViaEdgeAllowedByAP(DRBox& dr_box, DRNode* first_node, DRNode* second_node, ViaMasterIdx& via_master_idx);
  void resetPathHead(DRBox& dr_box);
  void updatePathResult(DRBox& dr_box);
  std::vector<Segment<LayerCoord>> getRoutingSegmentListByNode(DRNode* node);
  void updateDirectionSet(DRBox& dr_box);
  void resetStartAndEnd(DRBox& dr_box);
  void updateTaskResult(DRBox& dr_box);
  std::vector<Segment<LayerCoord>> getRoutingSegmentList(DRBox& dr_box);
  void pushToOpenList(DRBox& dr_box, DRNode* curr_node);
  DRNode* popFromOpenList(DRBox& dr_box);
  double getKnownCost(DRBox& dr_box, DRNode* start_node, DRNode* end_node, Orientation orientation);
  double getNodeCost(DRBox& dr_box, DRNode* curr_node, Orientation orientation);
  double getKnownWireCost(DRBox& dr_box, DRNode* start_node, DRNode* end_node);
  double getKnownViaCost(DRBox& dr_box, DRNode* start_node, DRNode* end_node);
  double getKnownBendCost(DRBox& dr_box, DRNode* start_node, DRNode* end_node);
  double getKnownSelfCost(DRBox& dr_box, DRNode* start_node, DRNode* end_node);
  double getEstimateCostToEnd(DRBox& dr_box, DRNode* curr_node);
  double getEstimateCost(DRBox& dr_box, DRNode* start_node, DRNode* end_node);
  void patchDRTask(DRBox& dr_box, DRTask* dr_task);
  void initSinglePatchTask(DRBox& dr_box, DRTask* dr_task);
  std::vector<Violation> getPatchViolationList(DRBox& dr_box, const std::set<ViolationType>& check_type_set, const std::vector<LayerRect>& check_region_list);
  DETask buildPatchDETask(DRBox& dr_box, const std::set<ViolationType>& check_type_set, const std::vector<LayerRect>& check_region_list);
  bool searchViolation(DRBox& dr_box, GTLPolyInt& patch_poly);
  bool isBoxMinAreaViolation(DRBox& dr_box, const Violation& violation);
  GTLPolyInt getViolationOverlapPoly(DRBox& dr_box, Violation& violation);
  void patchSingleViolation(DRBox& dr_box, const GTLPolyInt& patch_poly);
  std::optional<int32_t> selectPatch(DRBox& dr_box, const GTLPolyInt& patch_poly, std::vector<DRPatch>& candidate_patch_list);
  std::vector<DRPatch> getCandidatePatchList(DRBox& dr_box, const GTLPolyInt& patch_poly);
  std::vector<DRPatch> getCompactPatchList(DRBox& dr_box, const GTLPolyInt& patch_poly, const std::vector<DRPatch>& candidate_patch_list);
  void updatePatchCost(DRBox& dr_box, DRPatch& dr_patch, const std::vector<GTLRectInt>& poly_rect_list);
  std::vector<DRPatch> selectCandidatePatchList(DRBox& dr_box, std::vector<DRPatch>& dr_patch_list);
  bool isPatchImprovement(DRBox& dr_box, const std::vector<Violation>& origin_patch_violation_list, const std::vector<Violation>& curr_patch_violation_list);
  void resetSingleViolation(DRBox& dr_box);
  void updateTaskPatch(DRBox& dr_box);
  void updateRouteViolationList(DRBox& dr_box);
  void buildFixedDETask(DETask& de_task, const std::map<bool, std::map<int32_t, std::map<int32_t, std::set<EXTLayerRect*>>>>& type_layer_net_fixed_rect_map);
  void buildFixedDETask(DETask& de_task, const DRFixedGeometry& fixed_geometry);
  std::vector<Violation> getRouteViolationList(DRBox& dr_box);
  std::vector<Violation> getBoxRouteViolationList(DRBox& dr_box);
  void updateBestResult(DRBox& dr_box);
  void updateTaskSchedule(DRBox& dr_box, const std::vector<int32_t>& net_route_order_list, std::vector<int32_t>& routing_net_list);
  void selectBestResult(DRBox& dr_box);
  void freeDRBox(DRBox& dr_box);
  void updateDRModel(DRModel& dr_model);
  int32_t getRouteViolationNum(DRModel& dr_model);
  void updateNetResult(DRModel& dr_model);
  void updateNetPatch(DRModel& dr_model);
  void updateViolation(DRModel& dr_model);
  void selectViaByMinimumCut(DRModel& dr_model);
  DRBoxId getViolationOwnerBoxId(DRModel& dr_model, const Violation& violation);
  std::vector<Violation> getFullRouteViolationList(DRModel& dr_model, bool check_minimum_cut = false, const PlanarRect* check_rect = nullptr,
                                                   DETask* reusable_task = nullptr);
  std::vector<Violation> getDirtyRouteViolationList(DRModel& dr_model, DRBox& dr_box);
  void updateBestResult(DRModel& dr_model);
  bool stopIteration(DRModel& dr_model, std::vector<DRIterParam>& dr_iter_param_list);
  void selectBestResult(DRModel& dr_model);
  void patchFinalMinArea(DRModel& dr_model);
  void buildFinalPatchBox(DRModel& dr_model, DRBox& dr_box, const std::set<Violation*, CmpViolation>& patch_violation_set);
  void updateFinalPatch(DRBox& dr_box, std::map<int32_t, std::set<LayerRect, CmpLayerRectByXASC>>& uploaded_patch_map,
                        std::map<int32_t, std::vector<EXTLayerRect>>& new_patch_map);
  void uploadDRModel(DRModel& dr_model);

#if 1  // update env
  void updateFixedRectToGraph(DRBox& dr_box, ChangeType change_type, int32_t net_idx, EXTLayerRect* fixed_rect, bool is_routing);
  void updateFixedRectToGraph(DRBox& dr_box, ChangeType change_type, int32_t net_idx, Segment<LayerCoord>* segment);
  void updateRoutedRectToGraph(DRBox& dr_box, ChangeType change_type, int32_t net_idx, Segment<LayerCoord>& segment);
  void updateRoutedRectToGraph(DRBox& dr_box, ChangeType change_type, int32_t net_idx, EXTLayerRect& routed_rect, bool is_routing);
  void addRouteViolationToGraph(DRBox& dr_box, Violation& violation);
  void addRouteViolationToGraph(DRBox& dr_box, LayerRect& searched_rect, std::vector<Segment<LayerCoord>>& overlap_segment_list);
  void updateNetShapeToGraph(DRBox& dr_box, ChangeType change_type, NetShape& net_shape, bool is_fixed);
  void updateRoutingNetShapeToGraph(DRBox& dr_box, ChangeType change_type, NetShape& net_shape, bool is_fixed);
  void updatePlanarRectToGraph(DRBox& dr_box, ChangeType change_type, int32_t net_idx, int32_t layer_idx, const PlanarRect& rect, bool is_fixed);
  void updateViaRectToGraph(DRBox& dr_box, ChangeType change_type, int32_t net_idx, int32_t layer_idx, const PlanarRect& rect, bool is_fixed);
  void updateCutNetShapeToGraph(DRBox& dr_box, ChangeType change_type, NetShape& net_shape, bool is_fixed);
  void updateNodeNetToGraph(DRNode& dr_node, ChangeType change_type, int32_t net_idx, Orientation orientation, bool is_fixed);
  void addFixedRectToShadow(DRBox& dr_box, int32_t net_idx, EXTLayerRect* fixed_rect, bool is_routing);
  void addFixedRectToShadow(DRBox& dr_box, int32_t net_idx, Segment<LayerCoord>* segment);
  void updateRoutedRectToShadow(DRShadow& dr_shadow, ChangeType change_type, int32_t net_idx, const PlanarRect& shadow_shape);
  void updateRoutedRectToShadow(DRBox& dr_box, ChangeType change_type, int32_t net_idx, Segment<LayerCoord>& segment);
  void updateRoutedRectToShadow(DRBox& dr_box, ChangeType change_type, int32_t net_idx, EXTLayerRect& routed_rect, bool is_routing);
  std::vector<PlanarRect> getRoutingShadowShapeList(const NetShape& net_shape);
  std::array<std::pair<int32_t, int32_t>, 2> getRoutingSpacingPairList(const NetShape& net_shape);
#endif

#if 1  // get env
  double getFixedRectCost(DRBox& dr_box, int32_t net_idx, EXTLayerRect& patch);
  double getRoutedRectCost(DRBox& dr_box, int32_t net_idx, EXTLayerRect& patch);
#endif

#if 1  // exhibit
  void updateSummary(DRModel& dr_model);
  void printSummary(DRModel& dr_model);
  void outputNetCSV(DRModel& dr_model);
  void outputViolationCSV(DRModel& dr_model);
#endif

#if 1  // debug
  void debugPlotDRModel(DRModel& dr_model, std::string flag);
  void debugPlotDRBox(DRBox& dr_box, std::string flag);
#endif
};

}  // namespace irt
