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
#include "RuleValidator.hpp"

#include "DRCHeader.hpp"
#include "GDSPlotter.hpp"
#include "Monitor.hpp"
#include "PlanarRect.hpp"
#include "RVCluster.hpp"
#include "Utility.hpp"

namespace idrc {

// public

void RuleValidator::initInst()
{
  if (_rv_instance == nullptr) {
    _rv_instance = new RuleValidator();
  }
}

RuleValidator& RuleValidator::getInst()
{
  if (_rv_instance == nullptr) {
    DRCLOG.error(Loc::current(), "The instance not initialized!");
  }
  return *_rv_instance;
}

void RuleValidator::destroyInst()
{
  if (_rv_instance != nullptr) {
    delete _rv_instance;
    _rv_instance = nullptr;
  }
}

// function
std::vector<Violation> RuleValidator::verify(std::vector<DRCShape> drc_env_shape_list, std::vector<DRCShape> drc_result_shape_list,
                                             std::set<ViolationType> drc_check_type_set, std::vector<DRCShape> drc_check_region_list)
{
  Monitor monitor;
  DRCLOG.info(Loc::current(), "Starting...");
  if (drc_env_shape_list.empty() && drc_result_shape_list.empty()) {
    DRCLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
    return {};
  }
  RVModel rv_model(std::move(drc_env_shape_list), std::move(drc_result_shape_list), std::move(drc_check_type_set), std::move(drc_check_region_list));
  setRVComParam(rv_model);
  buildRVClusterList(rv_model);
  verifyRVModel(rv_model);
  buildViolationList(rv_model);
  // debugPlotRVModel(rv_model, "best");
  DRCLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
  return std::move(rv_model.get_violation_list());
}

// private

RuleValidator* RuleValidator::_rv_instance = nullptr;

void RuleValidator::setRVComParam(RVModel& rv_model)
{
  int32_t only_pitch = DRCDM.getOnlyPitch();
  int32_t cluster_size = 100 * only_pitch;
  int32_t expand_size = 5 * only_pitch;
  /**
   * cluster_size, expand_size
   */
  // clang-format off
  RVComParam rv_com_param(cluster_size, expand_size);
  // clang-format on
  DRCLOG.info(Loc::current(), "cluster_size: ", rv_com_param.get_cluster_size());
  DRCLOG.info(Loc::current(), "expand_size: ", rv_com_param.get_expand_size());
  rv_model.set_rv_com_param(rv_com_param);
}

void RuleValidator::buildRVClusterList(RVModel& rv_model)
{
  std::vector<RVCluster>& rv_cluster_list = rv_model.get_rv_cluster_list();
  int32_t cluster_size = rv_model.get_rv_com_param().get_cluster_size();
  int32_t expand_size = rv_model.get_rv_com_param().get_expand_size();

  PlanarRect bounding_box(INT32_MAX, INT32_MAX, INT32_MIN, INT32_MIN);
  int32_t offset_x = -1;
  int32_t offset_y = -1;
  int32_t grid_x_size = -1;
  int32_t grid_y_size = -1;
  {
    for (DRCShape& drc_env_shape : rv_model.get_drc_env_shape_list()) {
      bounding_box.set_ll_x(std::min(bounding_box.get_ll_x(), drc_env_shape.get_ll_x()));
      bounding_box.set_ll_y(std::min(bounding_box.get_ll_y(), drc_env_shape.get_ll_y()));
      bounding_box.set_ur_x(std::max(bounding_box.get_ur_x(), drc_env_shape.get_ur_x()));
      bounding_box.set_ur_y(std::max(bounding_box.get_ur_y(), drc_env_shape.get_ur_y()));
    }
    for (DRCShape& drc_result_shape : rv_model.get_drc_result_shape_list()) {
      bounding_box.set_ll_x(std::min(bounding_box.get_ll_x(), drc_result_shape.get_ll_x()));
      bounding_box.set_ll_y(std::min(bounding_box.get_ll_y(), drc_result_shape.get_ll_y()));
      bounding_box.set_ur_x(std::max(bounding_box.get_ur_x(), drc_result_shape.get_ur_x()));
      bounding_box.set_ur_y(std::max(bounding_box.get_ur_y(), drc_result_shape.get_ur_y()));
    }
    offset_x = bounding_box.get_ll_x();
    offset_y = bounding_box.get_ll_y();
    grid_x_size = bounding_box.getXSpan() / cluster_size + 1;
    grid_y_size = bounding_box.getYSpan() / cluster_size + 1;
  }
  rv_cluster_list.resize(grid_x_size * grid_y_size);
  for (int32_t grid_x = 0; grid_x < grid_x_size; grid_x++) {
    for (int32_t grid_y = 0; grid_y < grid_y_size; grid_y++) {
      RVCluster& rv_cluster = rv_cluster_list[grid_x + grid_y * grid_x_size];
      rv_cluster.set_cluster_idx(grid_x + grid_y * grid_x_size);
      rv_cluster.get_cluster_rect_list().emplace_back(grid_x * cluster_size + offset_x, grid_y * cluster_size + offset_y,
                                                      (grid_x + 1) * cluster_size + offset_x, (grid_y + 1) * cluster_size + offset_y);
      rv_cluster.set_rv_com_param(&rv_model.get_rv_com_param());
    }
  }
  for (DRCShape& drc_env_shape : rv_model.get_drc_env_shape_list()) {
    PlanarRect searched_rect = DRCUTIL.getEnlargedRect(drc_env_shape.get_rect(), expand_size);
    searched_rect = DRCUTIL.getRegularRect(searched_rect, bounding_box);
    int32_t grid_ll_x = (searched_rect.get_ll_x() - offset_x) / cluster_size;
    int32_t grid_ll_y = (searched_rect.get_ll_y() - offset_y) / cluster_size;
    int32_t grid_ur_x = (searched_rect.get_ur_x() - offset_x) / cluster_size;
    int32_t grid_ur_y = (searched_rect.get_ur_y() - offset_y) / cluster_size;
    for (int32_t grid_x = grid_ll_x; grid_x <= grid_ur_x; grid_x++) {
      for (int32_t grid_y = grid_ll_y; grid_y <= grid_ur_y; grid_y++) {
        int32_t cluster_idx = grid_x + grid_y * grid_x_size;
        if (static_cast<int32_t>(rv_cluster_list.size()) <= cluster_idx) {
          DRCLOG.error(Loc::current(), "rv_cluster_list.size() <= cluster_idx!");
        }
        rv_cluster_list[cluster_idx].get_drc_env_shape_list().push_back(&drc_env_shape);
      }
    }
  }
  for (DRCShape& drc_result_shape : rv_model.get_drc_result_shape_list()) {
    PlanarRect searched_rect = DRCUTIL.getEnlargedRect(drc_result_shape.get_rect(), expand_size);
    searched_rect = DRCUTIL.getRegularRect(searched_rect, bounding_box);
    int32_t grid_ll_x = (searched_rect.get_ll_x() - offset_x) / cluster_size;
    int32_t grid_ll_y = (searched_rect.get_ll_y() - offset_y) / cluster_size;
    int32_t grid_ur_x = (searched_rect.get_ur_x() - offset_x) / cluster_size;
    int32_t grid_ur_y = (searched_rect.get_ur_y() - offset_y) / cluster_size;
    for (int32_t grid_x = grid_ll_x; grid_x <= grid_ur_x; grid_x++) {
      for (int32_t grid_y = grid_ll_y; grid_y <= grid_ur_y; grid_y++) {
        int32_t cluster_idx = grid_x + grid_y * grid_x_size;
        if (static_cast<int32_t>(rv_cluster_list.size()) <= cluster_idx) {
          DRCLOG.error(Loc::current(), "rv_cluster_list.size() <= cluster_idx!");
        }
        rv_cluster_list[cluster_idx].get_drc_result_shape_list().push_back(&drc_result_shape);
      }
    }
  }
  for (RVCluster& rv_cluster : rv_cluster_list) {
    rv_cluster.set_drc_check_type_set(&rv_model.get_drc_check_type_set());
    rv_cluster.set_drc_check_region_list(&rv_model.get_drc_check_region_list());
  }
  for (DRCShape& drc_result_shape : rv_model.get_drc_result_shape_list()) {
    if (drc_result_shape.get_net_idx() < 0) {
      DRCLOG.error(Loc::current(), "The drc_result_shape_list exist idx < 0!");
    }
  }
}

void RuleValidator::verifyRVModel(RVModel& rv_model)
{
  Monitor monitor;
  DRCLOG.info(Loc::current(), "Starting...");
#pragma omp parallel for schedule(dynamic)
  for (RVCluster& rv_cluster : rv_model.get_rv_cluster_list()) {
    buildRVCluster(rv_cluster);
    if (needVerifying(rv_cluster)) {
      buildViolationList(rv_cluster);
    }
  }
  DRCLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void RuleValidator::buildRVCluster(RVCluster& rv_cluster)
{
  std::vector<DRCShape>* drc_check_region_list = rv_cluster.get_drc_check_region_list();
  int32_t expand_size = rv_cluster.get_rv_com_param()->get_expand_size();

  if (!drc_check_region_list->empty()) {
    std::vector<DRCShape*> drc_env_shape_list;
    std::vector<DRCShape*> drc_result_shape_list;
    for (DRCShape& drc_check_region : *drc_check_region_list) {
      PlanarRect searched_rect = DRCUTIL.getEnlargedRect(drc_check_region.get_rect(), expand_size);
      std::map<bool, std::set<int32_t>> type_layer_idx_map;
      {
        int32_t layer_idx = drc_check_region.get_layer_idx();
        type_layer_idx_map[true].insert({layer_idx - 1, layer_idx, layer_idx + 1});
        const std::vector<int32_t>& cut_layer_idx_list = DRCDM.getAdjacentCutLayerIdxList(layer_idx);
        type_layer_idx_map[false].insert(cut_layer_idx_list.begin(), cut_layer_idx_list.end());
      }
      for (DRCShape* drc_shape : rv_cluster.get_drc_env_shape_list()) {
        if (DRCUTIL.exist(type_layer_idx_map[drc_shape->get_is_routing()], drc_shape->get_layer_idx())
            && DRCUTIL.isClosedOverlap(searched_rect, drc_shape->get_rect())) {
          drc_env_shape_list.push_back(drc_shape);
        }
      }
      for (DRCShape* drc_shape : rv_cluster.get_drc_result_shape_list()) {
        if (DRCUTIL.exist(type_layer_idx_map[drc_shape->get_is_routing()], drc_shape->get_layer_idx())
            && DRCUTIL.isClosedOverlap(searched_rect, drc_shape->get_rect())) {
          drc_result_shape_list.push_back(drc_shape);
        }
      }
    }
    std::sort(drc_env_shape_list.begin(), drc_env_shape_list.end());
    drc_env_shape_list.erase(std::unique(drc_env_shape_list.begin(), drc_env_shape_list.end()), drc_env_shape_list.end());
    std::sort(drc_result_shape_list.begin(), drc_result_shape_list.end());
    drc_result_shape_list.erase(std::unique(drc_result_shape_list.begin(), drc_result_shape_list.end()), drc_result_shape_list.end());
    rv_cluster.set_drc_env_shape_list(drc_env_shape_list);
    rv_cluster.set_drc_result_shape_list(drc_result_shape_list);
  }
}

bool RuleValidator::needVerifying(RVCluster& rv_cluster)
{
  if (rv_cluster.get_drc_result_shape_list().empty()) {
    return false;
  }
  for (DRCShape* drc_result_shape : rv_cluster.get_drc_result_shape_list()) {
    for (PlanarRect& cluster_rect : rv_cluster.get_cluster_rect_list()) {
      if (DRCUTIL.isOpenOverlap(cluster_rect, drc_result_shape->get_rect())) {
        return true;
      }
    }
  }
  return false;
}

void RuleValidator::buildViolationList(RVCluster& rv_cluster)
{
  prepareRVCluster(rv_cluster);
  verifyRVCluster(rv_cluster);

  // destroy cluster cache after verify
  rv_cluster.get_layer_data().clear();

  processRVCluster(rv_cluster);
}

namespace {

void addShapeToLayerData(std::map<int32_t, RVLayerData>& layer_data, DRCShape* drc_shape, bool is_env_shape);
void prepareRoutingNet(int32_t net_idx, RVRoutingNet& routing_net, RVLayerData& rv_layer_data,
                       std::vector<std::pair<GTLRectInt, int32_t>>& env_rect_rtree_inputs);
void buildLayerSpatialIndexes(RVLayerData& rv_layer_data, const std::vector<std::pair<GTLRectInt, int32_t>>& env_rect_rtree_inputs);

}  // namespace

void RuleValidator::prepareRVCluster(RVCluster& rv_cluster)
{
  std::map<int32_t, RVLayerData>& layer_data = rv_cluster.get_layer_data();
  layer_data.clear();
  for (DRCShape* drc_shape : rv_cluster.get_drc_env_shape_list()) {
    addShapeToLayerData(layer_data, drc_shape, true);
  }
  for (DRCShape* drc_shape : rv_cluster.get_drc_result_shape_list()) {
    addShapeToLayerData(layer_data, drc_shape, false);
  }

  // Each layer owns flat geometry pools and the indexes that refer to them.
  for (auto& layer_entry : layer_data) {
    RVLayerData& rv_layer_data = layer_entry.second;
    size_t env_rect_count = 0;
    for (const auto& [net_idx, routing_net] : rv_layer_data.nets) {
      (void) net_idx;
      env_rect_count += routing_net.env_rect_list.size();
    }
    std::vector<std::pair<GTLRectInt, int32_t>> env_rect_rtree_inputs;
    env_rect_rtree_inputs.reserve(env_rect_count);
    for (auto& [net_idx, routing_net] : rv_layer_data.nets) {
      prepareRoutingNet(net_idx, routing_net, rv_layer_data, env_rect_rtree_inputs);
    }
    buildLayerSpatialIndexes(rv_layer_data, env_rect_rtree_inputs);
  }
}

void RuleValidator::verifyRVCluster(RVCluster& rv_cluster)
{
  if (needVerifying(rv_cluster, ViolationType::kAdjacentCutSpacing)) {
    verifyAdjacentCutSpacing(rv_cluster);
  }
  if (needVerifying(rv_cluster, ViolationType::kCornerFillSpacing)) {
    verifyCornerFillSpacing(rv_cluster);
  }
  if (needVerifying(rv_cluster, ViolationType::kCornerSpacing)) {
    verifyCornerSpacing(rv_cluster);
  }
  if (needVerifying(rv_cluster, ViolationType::kCutEOLSpacing)) {
    verifyCutEOLSpacing(rv_cluster);
  }
  if (needVerifying(rv_cluster, ViolationType::kCutShort)) {
    verifyCutShort(rv_cluster);
  }
  if (needVerifying(rv_cluster, ViolationType::kDifferentLayerCutSpacing)) {
    verifyDifferentLayerCutSpacing(rv_cluster);
  }
  if (needVerifying(rv_cluster, ViolationType::kEnclosure)) {
    verifyEnclosure(rv_cluster);
  }
  if (needVerifying(rv_cluster, ViolationType::kEnclosureEdge)) {
    verifyEnclosureEdge(rv_cluster);
  }
  if (needVerifying(rv_cluster, ViolationType::kEnclosureParallel)) {
    verifyEnclosureParallel(rv_cluster);
  }
  if (needVerifying(rv_cluster, ViolationType::kEndOfLineSpacing)) {
    verifyEndOfLineSpacing(rv_cluster);
  }
  if (needVerifying(rv_cluster, ViolationType::kFloatingPatch)) {
    verifyFloatingPatch(rv_cluster);
  }
  if (needVerifying(rv_cluster, ViolationType::kJogToJogSpacing)) {
    verifyJogToJogSpacing(rv_cluster);
  }
  if (needVerifying(rv_cluster, ViolationType::kMaximumWidth)) {
    verifyMaximumWidth(rv_cluster);
  }
  if (needVerifying(rv_cluster, ViolationType::kMaxViaStack)) {
    verifyMaxViaStack(rv_cluster);
  }
  if (needVerifying(rv_cluster, ViolationType::kMetalShort)) {
    verifyMetalShort(rv_cluster);
  }
  if (needVerifying(rv_cluster, ViolationType::kMinHole)) {
    verifyMinHole(rv_cluster);
  }
  if (needVerifying(rv_cluster, ViolationType::kMinimumArea)) {
    verifyMinimumArea(rv_cluster);
  }
  if (needVerifying(rv_cluster, ViolationType::kMinimumCut)) {
    verifyMinimumCut(rv_cluster);
  }
  if (needVerifying(rv_cluster, ViolationType::kMinimumWidth)) {
    verifyMinimumWidth(rv_cluster);
  }
  if (needVerifying(rv_cluster, ViolationType::kMinStep)) {
    verifyMinStep(rv_cluster);
  }
  if (needVerifying(rv_cluster, ViolationType::kNonsufficientMetalOverlap)) {
    verifyNonsufficientMetalOverlap(rv_cluster);
  }
  if (needVerifying(rv_cluster, ViolationType::kNotchSpacing)) {
    verifyNotchSpacing(rv_cluster);
  }
  if (needVerifying(rv_cluster, ViolationType::kOffGridOrWrongWay)) {
    verifyOffGridOrWrongWay(rv_cluster);
  }
  if (needVerifying(rv_cluster, ViolationType::kOutOfDie)) {
    verifyOutOfDie(rv_cluster);
  }
  if (needVerifying(rv_cluster, ViolationType::kParallelRunLengthSpacing)) {
    verifyParallelRunLengthSpacing(rv_cluster);
  }
  if (needVerifying(rv_cluster, ViolationType::kSameLayerCutSpacing)) {
    verifySameLayerCutSpacing(rv_cluster);
  }
}

bool RuleValidator::needVerifying(RVCluster& rv_cluster, ViolationType violation_type)
{
  std::set<ViolationType>& exist_rule_set = DRCDM.getDatabase().get_exist_rule_set();

  std::set<ViolationType>* drc_check_type_set = rv_cluster.get_drc_check_type_set();

  if (drc_check_type_set->empty()) {
    return DRCUTIL.exist(exist_rule_set, violation_type);
  } else {
    return (DRCUTIL.exist(*drc_check_type_set, violation_type) && DRCUTIL.exist(exist_rule_set, violation_type));
  }
}

void RuleValidator::processRVCluster(RVCluster& rv_cluster)
{
  std::vector<Violation> new_violation_list;
  for (Violation& violation : rv_cluster.get_violation_list()) {
    bool has_overlap = false;
    for (PlanarRect& cluster_rect : rv_cluster.get_cluster_rect_list()) {
      if (DRCUTIL.isOpenOverlap(cluster_rect, violation.get_rect())) {
        has_overlap = true;
        break;
      }
    }
    if (!has_overlap) {
      continue;
    }
    new_violation_list.push_back(violation);
  }
  std::sort(new_violation_list.begin(), new_violation_list.end(), CmpViolation());
  new_violation_list.erase(std::unique(new_violation_list.begin(), new_violation_list.end()), new_violation_list.end());
  rv_cluster.set_violation_list(new_violation_list);
}

void RuleValidator::buildViolationList(RVModel& rv_model)
{
  std::vector<Violation>& violation_list = rv_model.get_violation_list();
  for (RVCluster& rv_cluster : rv_model.get_rv_cluster_list()) {
    for (Violation& violation : rv_cluster.get_violation_list()) {
      violation_list.push_back(violation);
    }
  }
  std::sort(violation_list.begin(), violation_list.end(), CmpViolation());
  violation_list.erase(std::unique(violation_list.begin(), violation_list.end()), violation_list.end());
}

namespace {

using IndexedRect = std::pair<GTLRectInt, int32_t>;
using RectRTree = bgi::rtree<GTLRectInt, bgi::quadratic<16>>;

// Temporary geometry used only while materializing one routing net.
struct NetPrepareContext
{
  bool has_delta_geometry = false;
  RectRTree delta_rect_rtree;
  std::vector<GTLRectInt> delta_overlap_list;
};

Orientation getBoundaryOrient(Rotation rotation, bool is_hole, const PlanarCoord& begin_coord, const PlanarCoord& end_coord)
{
  Orientation travel_orient = DRCUTIL.getOrientation(begin_coord, end_coord);
  bool metal_on_left = (rotation == Rotation::kCounterclockwise);
  if (is_hole) {
    metal_on_left = !metal_on_left;
  }
  return metal_on_left ? DRCUTIL.getCWOrientation(travel_orient) : DRCUTIL.getCCWOrientation(travel_orient);
}

void collectBoundaryEdges(GTLHolePolyInt& check_hole_poly, bool is_hole, int32_t polygon_id, std::vector<BoundaryData>& boundary_pool)
{
  int32_t boundary_begin = static_cast<int32_t>(boundary_pool.size());
  int32_t coord_size = static_cast<int32_t>(check_hole_poly.size());
  if (coord_size < 2) {
    return;
  }

  std::vector<PlanarCoord> coord_list;
  coord_list.reserve(coord_size);
  for (auto iter = check_hole_poly.begin(); iter != check_hole_poly.end(); iter++) {
    coord_list.push_back(DRCUTIL.convertToPlanarCoord(*iter));
  }
  if (coord_list.size() < 2) {
    return;
  }

  Rotation rotation = DRCUTIL.getRotation(check_hole_poly);
  for (int32_t i = 0; i < coord_size; i++) {
    PlanarCoord& pre_coord = coord_list[(i - 1 + coord_size) % coord_size];
    PlanarCoord& curr_coord = coord_list[i];
    if (pre_coord == curr_coord) {
      continue;
    }

    BoundaryData boundary_data;
    boundary_data.edge = DRCUTIL.convertToGTLRectInt(DRCUTIL.getRect(pre_coord, curr_coord));
    boundary_data.begin_coord = pre_coord;
    boundary_data.end_coord = curr_coord;
    boundary_data.orient = getBoundaryOrient(rotation, is_hole, pre_coord, curr_coord);
    boundary_data.polygon_id = polygon_id;
    boundary_data.edge_length = DRCUTIL.getManhattanDistance(pre_coord, curr_coord);
    boundary_data.isHole = is_hole;
    if (coord_size >= 3) {
      PlanarCoord& post_coord = coord_list[(i + 1) % coord_size];
      boundary_data.isConvex = is_hole ? DRCUTIL.isConcaveCorner(rotation, pre_coord, curr_coord, post_coord)
                                       : DRCUTIL.isConvexCorner(rotation, pre_coord, curr_coord, post_coord);
    }

    boundary_pool.push_back(boundary_data);
  }

  int32_t boundary_count = static_cast<int32_t>(boundary_pool.size()) - boundary_begin;
  if (boundary_count < 2) {
    return;
  }
  for (int32_t i = 0; i < boundary_count; i++) {
    BoundaryData& boundary_data = boundary_pool[boundary_begin + i];
    boundary_data.prev_boundary_id = boundary_begin + (i - 1 + boundary_count) % boundary_count;
    boundary_data.next_boundary_id = boundary_begin + (i + 1) % boundary_count;
  }
}

void addShapeToLayerData(std::map<int32_t, RVLayerData>& layer_data, DRCShape* drc_shape, bool is_env_shape)
{
  GTLRectInt gtl_rect = DRCUTIL.convertToGTLRectInt(drc_shape->get_rect());
  RVLayerData& rv_layer_data = layer_data[drc_shape->get_layer_idx()];
  if (!drc_shape->get_is_routing()) {
    rv_layer_data.cut_pool.push_back({gtl_rect, drc_shape->get_net_idx(), is_env_shape, drc_shape->get_source_type()});
    return;
  }

  RVRoutingNet& routing_net = rv_layer_data.nets[drc_shape->get_net_idx()];
  if (is_env_shape) {
    routing_net.env_rect_list.push_back(gtl_rect);
  } else {
    routing_net.result_rect_list.push_back(gtl_rect);
  }
}

void prepareRoutingNet(int32_t net_idx, RVRoutingNet& routing_net, RVLayerData& rv_layer_data,
                       std::vector<std::pair<GTLRectInt, int32_t>>& env_rect_rtree_inputs)
{
  NetPrepareContext prepare_context;
  std::vector<GTLRectInt> env_rect_list = std::move(routing_net.env_rect_list);
  std::vector<GTLRectInt> result_rect_list = std::move(routing_net.result_rect_list);
  bool has_env = !env_rect_list.empty();
  bool has_result = !result_rect_list.empty();

  routing_net.polyset.insert(env_rect_list.begin(), env_rect_list.end());
  routing_net.polyset.insert(result_rect_list.begin(), result_rect_list.end());

  GTLPolySetInt env_polyset;
  if (has_env && has_result) {
    env_polyset.insert(env_rect_list.begin(), env_rect_list.end());
    std::vector<GTLRectInt> env_max_rect_list;
    gtl::get_max_rectangles(env_max_rect_list, env_polyset);
    for (const GTLRectInt& env_max_rect : env_max_rect_list) {
      env_rect_rtree_inputs.emplace_back(env_max_rect, net_idx);
    }
  }

  // result - env equals (env union result) - env without rebuilding result.
  if (has_env && has_result) {
    GTLPolySetInt delta_polyset = routing_net.polyset - env_polyset;
    prepare_context.has_delta_geometry = !gtl::empty(delta_polyset);
    if (prepare_context.has_delta_geometry) {
      std::vector<GTLRectInt> delta_rect_list;
      gtl::get_max_rectangles(delta_rect_list, delta_polyset);
      prepare_context.delta_rect_rtree = RectRTree(delta_rect_list);
    }
  }

  // Materialize combined geometry into contiguous layer pools.
  routing_net.polygon_begin = static_cast<int32_t>(rv_layer_data.polygon_pool.size());
  routing_net.max_rect_begin = static_cast<int32_t>(rv_layer_data.max_rect_pool.size());
  routing_net.boundary_begin = static_cast<int32_t>(rv_layer_data.boundary_pool.size());

  std::vector<GTLHolePolyInt> hole_poly_list;
  routing_net.polyset.get(hole_poly_list);
  for (GTLHolePolyInt& hole_poly : hole_poly_list) {
    int32_t polygon_id = static_cast<int32_t>(rv_layer_data.polygon_pool.size());
    rv_layer_data.polygon_pool.push_back(
        {net_idx, static_cast<int32_t>(rv_layer_data.max_rect_pool.size()), 0, static_cast<int32_t>(rv_layer_data.boundary_pool.size()), 0});
    PolygonData& polygon_data = rv_layer_data.polygon_pool.back();
    polygon_data.hole_poly = std::move(hole_poly);
    GTLHolePolyInt& polygon_hole_poly = polygon_data.hole_poly;
    std::vector<GTLRectInt> rect_list;
    if (polygon_hole_poly.size() == 4 && polygon_hole_poly.begin_holes() == polygon_hole_poly.end_holes()) {
      rect_list.emplace_back();
      gtl::extents(rect_list.back(), polygon_hole_poly);
    } else {
      gtl::get_max_rectangles(rect_list, polygon_hole_poly);
    }
    // A polygon is env only when it is nonempty and every max rectangle decomposed from it is env.
    bool is_polygon_env = has_env && !rect_list.empty();
    for (const GTLRectInt& gtl_rect : rect_list) {
      // A max rectangle is env unless it has an open-area overlap with result-only geometry (result - env).
      bool is_env = has_env;
      if (is_env && prepare_context.has_delta_geometry) {
        prepare_context.delta_overlap_list.clear();
        prepare_context.delta_rect_rtree.query(bgi::intersects(gtl_rect), std::back_inserter(prepare_context.delta_overlap_list));
        PlanarRect max_rect = DRCUTIL.convertToPlanarRect(gtl_rect);
        for (const GTLRectInt& delta_rect : prepare_context.delta_overlap_list) {
          if (DRCUTIL.isOpenOverlap(DRCUTIL.convertToPlanarRect(delta_rect), max_rect)) {
            is_env = false;
            break;
          }
        }
      }
      rv_layer_data.max_rect_pool.push_back({gtl_rect, polygon_id, is_env});
      if (has_env && !has_result) {
        env_rect_rtree_inputs.emplace_back(gtl_rect, net_idx);
      }
      is_polygon_env = is_polygon_env && is_env;
    }
    polygon_data.max_rect_count = static_cast<int32_t>(rv_layer_data.max_rect_pool.size()) - polygon_data.max_rect_begin;
    polygon_data.isEnv = is_polygon_env;

    collectBoundaryEdges(polygon_hole_poly, false, polygon_id, rv_layer_data.boundary_pool);
    for (auto iter = polygon_hole_poly.begin_holes(); iter != polygon_hole_poly.end_holes(); iter++) {
      GTLPolyInt gtl_poly = *iter;
      GTLHolePolyInt check_hole_poly;
      check_hole_poly.set(gtl_poly.begin(), gtl_poly.end());
      collectBoundaryEdges(check_hole_poly, true, polygon_id, rv_layer_data.boundary_pool);
    }
    polygon_data.boundary_count = static_cast<int32_t>(rv_layer_data.boundary_pool.size()) - polygon_data.boundary_begin;
  }

  routing_net.polygon_count = static_cast<int32_t>(rv_layer_data.polygon_pool.size()) - routing_net.polygon_begin;
  routing_net.max_rect_count = static_cast<int32_t>(rv_layer_data.max_rect_pool.size()) - routing_net.max_rect_begin;
  routing_net.boundary_count = static_cast<int32_t>(rv_layer_data.boundary_pool.size()) - routing_net.boundary_begin;
}

void buildLayerSpatialIndexes(RVLayerData& rv_layer_data, const std::vector<std::pair<GTLRectInt, int32_t>>& env_rect_rtree_inputs)
{
  // Pool IDs are final here, so index inputs can be allocated exactly once.
  std::vector<IndexedRect> rect_inputs;
  rect_inputs.reserve(rv_layer_data.max_rect_pool.size());
  for (size_t i = 0; i < rv_layer_data.max_rect_pool.size(); i++) {
    rect_inputs.emplace_back(rv_layer_data.max_rect_pool[i].rect, static_cast<int32_t>(i));
  }

  std::vector<IndexedRect> boundary_inputs;
  boundary_inputs.reserve(rv_layer_data.boundary_pool.size());
  for (size_t i = 0; i < rv_layer_data.boundary_pool.size(); i++) {
    boundary_inputs.emplace_back(rv_layer_data.boundary_pool[i].edge, static_cast<int32_t>(i));
  }

  rv_layer_data.rect_rtrees = decltype(rv_layer_data.rect_rtrees)(rect_inputs);
  rv_layer_data.env_rect_rtree = decltype(rv_layer_data.env_rect_rtree)(env_rect_rtree_inputs);
  rv_layer_data.boundary_rtrees = decltype(rv_layer_data.boundary_rtrees)(boundary_inputs);
  rv_layer_data.cut_rtrees = decltype(rv_layer_data.cut_rtrees)(rv_layer_data.cut_pool);
}

}  // namespace

#if 1  // debug

void RuleValidator::debugPlotRVModel(RVModel& rv_model, std::string flag)
{
  Die& die = DRCDM.getDatabase().get_die();
  std::string& rv_temp_directory_path = DRCDM.getConfig().rv_temp_directory_path;

  GPGDS gp_gds;

  GPStruct base_region_struct("base_region");
  GPBoundary gp_boundary;
  gp_boundary.set_layer_idx(0);
  gp_boundary.set_data_type(0);
  gp_boundary.set_rect(die);
  base_region_struct.push(gp_boundary);
  gp_gds.addStruct(base_region_struct);

  for (DRCShape& drc_env_shape : rv_model.get_drc_env_shape_list()) {
    GPStruct drc_env_shape_struct(DRCUTIL.getString("drc_env_shape(net_", drc_env_shape.get_net_idx(), ")"));
    GPBoundary gp_boundary;
    gp_boundary.set_data_type(static_cast<int32_t>(GPDataType::kEnvShape));
    gp_boundary.set_rect(drc_env_shape.get_rect());
    if (drc_env_shape.get_is_routing()) {
      gp_boundary.set_layer_idx(DRCGP.getGDSIdxByRouting(drc_env_shape.get_layer_idx()));
    } else {
      gp_boundary.set_layer_idx(DRCGP.getGDSIdxByCut(drc_env_shape.get_layer_idx()));
    }
    drc_env_shape_struct.push(gp_boundary);
    gp_gds.addStruct(drc_env_shape_struct);
  }

  for (DRCShape& drc_result_shape : rv_model.get_drc_result_shape_list()) {
    GPStruct drc_result_shape_struct(DRCUTIL.getString("drc_result_shape(net_", drc_result_shape.get_net_idx(), ")"));
    GPBoundary gp_boundary;
    gp_boundary.set_data_type(static_cast<int32_t>(GPDataType::kResultShape));
    gp_boundary.set_rect(drc_result_shape.get_rect());
    if (drc_result_shape.get_is_routing()) {
      gp_boundary.set_layer_idx(DRCGP.getGDSIdxByRouting(drc_result_shape.get_layer_idx()));
    } else {
      gp_boundary.set_layer_idx(DRCGP.getGDSIdxByCut(drc_result_shape.get_layer_idx()));
    }
    drc_result_shape_struct.push(gp_boundary);
    gp_gds.addStruct(drc_result_shape_struct);
  }

  for (Violation& violation : rv_model.get_violation_list()) {
    std::string net_idx_name = DRCUTIL.getString("net");
    for (int32_t violation_net_idx : violation.get_violation_net_set()) {
      net_idx_name = DRCUTIL.getString(net_idx_name, ",", violation_net_idx);
    }
    GPStruct violation_struct(DRCUTIL.getString("violation(", net_idx_name, ")(rs,", violation.get_required_size(), ")"));
    GPBoundary gp_boundary;
    gp_boundary.set_data_type(static_cast<int32_t>(DRCGP.convertGPDataType(violation.get_violation_type())));
    gp_boundary.set_rect(violation.get_rect());
    if (violation.get_is_routing()) {
      gp_boundary.set_layer_idx(DRCGP.getGDSIdxByRouting(violation.get_layer_idx()));
    } else {
      gp_boundary.set_layer_idx(DRCGP.getGDSIdxByCut(violation.get_layer_idx()));
    }
    violation_struct.push(gp_boundary);
    gp_gds.addStruct(violation_struct);
  }

  std::string gds_file_path = DRCUTIL.getString(rv_temp_directory_path, flag, "_rv_model.gds");
  DRCGP.plot(gp_gds, gds_file_path);
}

void RuleValidator::debugPlotRVCluster(RVCluster& rv_cluster, std::string flag)
{
  std::string& rv_temp_directory_path = DRCDM.getConfig().rv_temp_directory_path;

  GPGDS gp_gds;

  GPStruct base_region_struct("base_region");
  for (PlanarRect& cluster_rect : rv_cluster.get_cluster_rect_list()) {
    GPBoundary gp_boundary;
    gp_boundary.set_layer_idx(0);
    gp_boundary.set_data_type(0);
    gp_boundary.set_rect(cluster_rect);
    base_region_struct.push(gp_boundary);
  }
  gp_gds.addStruct(base_region_struct);

  for (DRCShape* drc_env_shape : rv_cluster.get_drc_env_shape_list()) {
    GPStruct drc_env_shape_struct(DRCUTIL.getString("drc_env_shape(net_", drc_env_shape->get_net_idx(), ")"));
    GPBoundary gp_boundary;
    gp_boundary.set_data_type(static_cast<int32_t>(GPDataType::kEnvShape));
    gp_boundary.set_rect(drc_env_shape->get_rect());
    if (drc_env_shape->get_is_routing()) {
      gp_boundary.set_layer_idx(DRCGP.getGDSIdxByRouting(drc_env_shape->get_layer_idx()));
    } else {
      gp_boundary.set_layer_idx(DRCGP.getGDSIdxByCut(drc_env_shape->get_layer_idx()));
    }
    drc_env_shape_struct.push(gp_boundary);
    gp_gds.addStruct(drc_env_shape_struct);
  }

  for (DRCShape* drc_result_shape : rv_cluster.get_drc_result_shape_list()) {
    GPStruct drc_result_shape_struct(DRCUTIL.getString("drc_result_shape(net_", drc_result_shape->get_net_idx(), ")"));
    GPBoundary gp_boundary;
    gp_boundary.set_data_type(static_cast<int32_t>(GPDataType::kResultShape));
    gp_boundary.set_rect(drc_result_shape->get_rect());
    if (drc_result_shape->get_is_routing()) {
      gp_boundary.set_layer_idx(DRCGP.getGDSIdxByRouting(drc_result_shape->get_layer_idx()));
    } else {
      gp_boundary.set_layer_idx(DRCGP.getGDSIdxByCut(drc_result_shape->get_layer_idx()));
    }
    drc_result_shape_struct.push(gp_boundary);
    gp_gds.addStruct(drc_result_shape_struct);
  }

  for (Violation& violation : rv_cluster.get_violation_list()) {
    std::string net_idx_name = DRCUTIL.getString("net");
    for (int32_t violation_net_idx : violation.get_violation_net_set()) {
      net_idx_name = DRCUTIL.getString(net_idx_name, ",", violation_net_idx);
    }
    GPStruct violation_struct(DRCUTIL.getString("violation(", net_idx_name, ")(rs,", violation.get_required_size(), ")"));
    GPBoundary gp_boundary;
    gp_boundary.set_data_type(static_cast<int32_t>(DRCGP.convertGPDataType(violation.get_violation_type())));
    gp_boundary.set_rect(violation.get_rect());
    if (violation.get_is_routing()) {
      gp_boundary.set_layer_idx(DRCGP.getGDSIdxByRouting(violation.get_layer_idx()));
    } else {
      gp_boundary.set_layer_idx(DRCGP.getGDSIdxByCut(violation.get_layer_idx()));
    }
    violation_struct.push(gp_boundary);
    gp_gds.addStruct(violation_struct);
  }

  std::string gds_file_path = DRCUTIL.getString(rv_temp_directory_path, flag, "_rv_cluster_", rv_cluster.get_cluster_idx(), ".gds");

  DRCGP.plot(gp_gds, gds_file_path);
}

#endif

}  // namespace idrc
