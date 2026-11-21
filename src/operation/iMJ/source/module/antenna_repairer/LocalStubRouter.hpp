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

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <boost/geometry.hpp>
#include <boost/geometry/geometries/point_xy.hpp>
#include <boost/geometry/geometries/polygon.hpp>

#include "ARInternalTypes.hpp"
#include "GeometryLayerMetadata.h"
#include "GeometryStore.h"
#include "IdbDesign.h"
#include "IdbNet.h"
#include "IdbSpecialNet.h"

namespace imj {

class LocalStubRouter
{
 public:
  struct CutSpacingRules
  {
    int32_t diff_net = 0;
    int32_t same_net = 0;
    int32_t max_spacing = 0;
    int32_t prl = -1;
    int32_t prl_spacing = 0;
    std::vector<CutAdjacentRule> adjacent_rules;
    std::vector<CutArraySpacingRule> array_rules;
    std::vector<DiffLayerCutSpacingRule> diff_layer_rules;
  };

  bool init(idb::IdbDesign* design);

  int layerIdxByOrder(int order) const;
  int maxLayerIdxForOrder(int max_route_order) const;
  const std::vector<RoutingLayerInfo>& layers() const { return _layers; }
  const ViaOption* viaBetween(int layer_a, int layer_b) const;
  idb::IdbRect wireBox(int layer_idx, int32_t x1, int32_t y1, int32_t x2, int32_t y2) const;
  int32_t requiredSpacing(int lidx, const idb::IdbRect& a, const idb::IdbRect& b, int32_t custom_min_spacing = 0,
                          int32_t b_effective_width = 0) const;
  ecc::geometry::LayerId geomCutLayerIdForOrder(int cut_order) const;
  bool checkMazeViaCutsValid(int low_idx, int32_t cx, int32_t cy, const std::vector<CutShapeBox>& ancestor_cuts,
                             idb::IdbNet* target_net) const;

  bool hasDie() const { return _has_die; }
  const idb::IdbRect& dieBBox() const { return _die_bbox; }
  bool rectInsideDie(const idb::IdbRect& r) const;
  int32_t mfgGrid() const { return _mfg_grid; }
  bool rectOnMfgGrid(const idb::IdbRect& r) const;
  bool coordOnLayerMfgGrid(int lidx, int32_t coord) const;
  int32_t snapCoordDown(int lidx, int32_t coord) const;
  int32_t snapCoordUp(int lidx, int32_t coord) const;

  bool validateCandidateCell(idb::IdbNet* target_net, const CandidateDiodeGeometry& geom) const;
  bool validateProposedRoute(idb::IdbNet* target_net, const CandidateDiodeGeometry& geom, const RoutePlan& plan,
                             std::vector<std::vector<idb::IdbRect>>* out_prl_conflicts = nullptr) const;

  RoutePlan routeStub(idb::IdbNet* target_net, int max_route_order, const CandidateDiodeGeometry& geom,
                      const std::vector<LayerShapeBox>& goal_shapes, int32_t search_radius_dbu) const;

  std::vector<ecc::geometry::ShapeId> addCommittedNetShapes(
      idb::IdbNet* net, const std::vector<LayerShapeBox>& metal_shapes,
      const std::vector<std::pair<ecc::geometry::LayerId, idb::IdbRect>>& cut_shapes);

  std::vector<ecc::geometry::ShapeId> addCommittedSpecialNetShapes(idb::IdbSpecialNet* snet,
                                                                   const std::vector<LayerShapeBox>& metal_shapes,
                                                                   const std::vector<CutShapeBox>& cut_shapes);

  std::vector<ecc::geometry::ShapeId> addCommittedObstacles(const std::vector<LayerShapeBox>& obs_shapes);
  std::vector<ecc::geometry::ShapeId> addCommittedCutObstacles(const std::vector<CutShapeBox>& cut_obs_shapes);

  void removeCommittedShapes(const std::vector<ecc::geometry::ShapeId>& shape_ids);

 private:
  struct PGPinTag
  {
    std::string term_name;
    bool is_power = false;
    bool is_ground = false;
  };
  struct CachedNetLayer
  {
    std::vector<idb::IdbRect> rects;
    std::vector<int32_t> prefix_max_high_x;
  };
  struct MaxViaStackRule
  {
    int32_t max_cuts = 0;
    int min_layer_order = std::numeric_limits<int>::min();
    int max_layer_order = std::numeric_limits<int>::max();
    bool no_single = false;
  };
  struct BlockageRule
  {
    int32_t min_spacing = 0;
    int32_t effective_width = 0;
    bool except_pgnet = false;
    bool is_slots_or_fills = false;
  };
  struct MinAreaDeficit
  {
    int layer_idx = -1;
    idb::IdbRect poly_bbox;
    int64_t cur_area = 0;
    int64_t min_area = 0;
  };

  using DiePoint = boost::geometry::model::d2::point_xy<int64_t>;
  using DiePolygon = boost::geometry::model::polygon<DiePoint>;

  void initLayersAndMetadata(idb::IdbLayout* layout,
                             const std::vector<ecc::geometry::GeometryLayerMetadata>& layer_meta);
  void initDieGeometry(idb::IdbLayout* layout);
  void buildVias(idb::IdbDesign* design);
  void buildOwnerMap(idb::IdbDesign* design);
  int32_t cutSpacingForOrder(int cut_order, bool same_net = false) const;
  std::pair<uint8_t, uint64_t> obstacleGroupKey(ecc::geometry::OwnerRef owner) const;
  std::vector<RoutingObstacleBox> queryMergedRoutingObstacles(int lidx, const idb::IdbRect& query_rect,
                                                                idb::IdbNet* exempt_signal_net,
                                                                const PGShapeBox* exempt_pg) const;
  const CachedNetLayer& getMergedNetLayer(idb::IdbNet* target_net, int lidx) const;
  std::vector<idb::IdbRect> collectTouchingTargetNetRects(int lidx, const std::vector<idb::IdbRect>& seeds,
                                                          idb::IdbNet* target_net) const;
  std::vector<idb::IdbRect> collectNearbyTargetNetRects(int lidx, const std::vector<idb::IdbRect>& seeds,
                                                        int32_t margin, idb::IdbNet* target_net) const;
  bool checkNetPolygonDrcWithNearby(int lidx, const std::vector<idb::IdbRect>& new_rects, idb::IdbNet* target_net,
                                    std::vector<idb::IdbRect>* out_same_net_conflicts) const;
  bool checkCutSetValid(const std::vector<CutShapeBox>& proposed_cuts, idb::IdbNet* target_net,
                        const PGCutShapeBox* exempt_pg, const std::vector<CutShapeBox>* extra_peer_cuts = nullptr,
                        const std::vector<CutShapeBox>* extra_foreign_cuts = nullptr) const;
  bool checkNetMetalClear(int lidx, const std::vector<idb::IdbRect>& new_rects, idb::IdbNet* target_net,
                          std::vector<idb::IdbRect>* out_conflicts) const;
  bool checkNetMinArea(int lidx, const std::vector<idb::IdbRect>& new_rects, idb::IdbNet* target_net,
                       std::vector<MinAreaDeficit>* out_deficits) const;
  bool validateProposedRouteImpl(idb::IdbNet* target_net, const CandidateDiodeGeometry& geom, const RoutePlan& plan,
                                 std::vector<std::vector<idb::IdbRect>>* out_prl_conflicts,
                                 std::vector<std::vector<idb::IdbRect>>* out_same_net_conflicts,
                                 std::vector<MinAreaDeficit>* out_min_area_deficits) const;
  std::optional<RoutePlan> tryPatchMinArea(idb::IdbNet* target_net, const CandidateDiodeGeometry& geom,
                                           const RoutePlan& base_plan,
                                           const std::vector<MinAreaDeficit>& initial_deficits) const;

  idb::IdbDesign* _design = nullptr;
  ecc::geometry::GeometryStore _store;
  bool _has_die = false;
  bool _die_is_polygon = false;
  int32_t _mfg_grid = 1;
  idb::IdbRect _die_bbox;
  DiePolygon _die_polygon;
  std::vector<RoutingLayerInfo> _layers;
  std::map<int, int> _order_to_idx;
  std::unordered_map<int, ecc::geometry::LayerId> _order_to_geom_layer_id;
  std::unordered_map<int, CutSpacingRules> _order_to_cut_spacing;
  std::vector<int> _cut_orders_sorted;
  MaxViaStackRule _max_via_stack;
  std::map<int, ViaOption> _vias_by_low_idx;
  std::unordered_map<ecc::geometry::OwnerId, BlockageRule> _blockage_rules;
  std::map<std::pair<ecc::geometry::OwnerId, int>, int32_t> _obs_layer_spacing;
  std::unordered_map<ecc::geometry::ShapeId, int32_t> _committed_obs_spacing;
  std::unordered_map<ecc::geometry::OwnerIndexKey, idb::IdbNet*, ecc::geometry::OwnerIndexKeyHash> _owner_to_net;
  std::unordered_map<idb::IdbNet*, std::vector<ecc::geometry::OwnerIndexKey>> _net_owners;
  std::unordered_map<idb::IdbNet*, ecc::geometry::OwnerId> _net_owner_id;
  mutable std::map<std::pair<idb::IdbNet*, int>, CachedNetLayer> _merged_net_cache;
  std::unordered_map<ecc::geometry::OwnerIndexKey, idb::IdbSpecialNet*, ecc::geometry::OwnerIndexKeyHash>
      _owner_to_special_net;
  std::unordered_map<idb::IdbSpecialNet*, ecc::geometry::OwnerId> _special_net_owner_id;
  std::unordered_map<ecc::geometry::OwnerIndexKey, PGPinTag, ecc::geometry::OwnerIndexKeyHash> _owner_pg_tag;
  uint32_t _next_commit_path = 1;
};

}  // namespace imj
