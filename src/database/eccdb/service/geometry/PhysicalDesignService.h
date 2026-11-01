// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "common/GeometryTypes.h"
#include "design/common/DesignTypes.h"
#include "design/constraint/model/ConstraintComponents.h"
#include "design/floorplan/model/FloorplanComponents.h"
#include "design/geometry/DesignGeometryComponents.h"
#include "design/global/model/DesignGlobalComponents.h"
#include "design/netlist/model/NetlistComponents.h"
#include "design/non_default_rule/component/NonDefaultRuleComponents.h"
#include "design/routing/component/RoutingComponents.h"
#include "design/routing/component/WireRouting.h"
#include "design/via/component/ViaComponents.h"
#include "library/cell_master/model/CellMasterComponents.h"
#include "library/cell_master/model/MasterObsComponents.h"
#include "library/master_term/model/MasterTermComponents.h"
#include "library/site/model/SiteComponents.h"
#include "tech/common/TechLayerTypes.h"
#include "tech/cut_layer/model/CutLayerComponents.h"
#include "tech/cut_layer/model/CutRuleComponents.h"
#include "tech/cut_layer/model/CutRuleIds.h"
#include "tech/masterslice_layer/model/MastersliceLayerComponents.h"
#include "tech/non_default_rule/model/NonDefaultRuleComponents.h"
#include "tech/routing_layer/model/RoutingLayerComponents.h"
#include "tech/via_master/model/ViaMasterComponents.h"
#include "tech/via_rule_generate/model/ViaRuleGenerateComponents.h"

namespace eccdb {

class DesignStore;
class LibraryStore;
class TechStore;
class DatabaseContext;

// Identifies where a shape originated. Attribute fields on the shape retain
// the source schema's raw flag values so adapters can apply their own policy.
enum class PhysicalGeometrySource : uint8_t
{
  kMasterPort,
  kMasterObstruction,
  kIoPin,
  kTechVia,
  kDesignVia,
  kRoutingBlockage
};

struct PhysicalLayerRect
{
  TechLayerId layer;
  Rect rectangle;
  PhysicalGeometrySource source = PhysicalGeometrySource::kMasterPort;
  uint32_t flags = 0;
  uint32_t mask = 0;
  int32_t spacing = 0;
  int32_t design_rule_width = 0;
};

// The point span is valid only for the duration of the geometry callback.
// Callers that need to retain it should copy the points in the callback.
struct PhysicalLayerPolygon
{
  TechLayerId layer;
  std::span<const Point> points;
  PhysicalGeometrySource source = PhysicalGeometrySource::kMasterPort;
  uint32_t flags = 0;
  uint32_t mask = 0;
  int32_t spacing = 0;
  int32_t design_rule_width = 0;
};

// Exactly one of tech_via and design_via is set. The placement orientation is
// part of the geometry transform; a consumer can expand it with the Via APIs.
struct PhysicalViaPlacement
{
  TechViaMasterId tech_via;
  DesignViaId design_via;
  Point origin;
  DesignOrientation orientation = DesignOrientation::kN;
  PhysicalGeometrySource source = PhysicalGeometrySource::kMasterPort;
  uint32_t flags = 0;
  uint32_t top_mask = 0;
  uint32_t cut_mask = 0;
  uint32_t bottom_mask = 0;
};

struct PhysicalGeometryVisitor
{
  std::function<void(const PhysicalLayerRect&)> rectangle;
  std::function<void(const PhysicalLayerPolygon&)> polygon;
  std::function<void(const PhysicalViaPlacement&)> via;
};

struct PhysicalNetPinVisitor
{
  std::function<void(DesignInstancePinId, const DesignInstancePin&, const DesignInstance&, const LibraryMasterTerm&)> instance_pin;
  std::function<void(DesignIoPinId, const DesignIoPin&)> io_pin;
};

// A DesignNet may reference either a technology-scope LEF NDR or a
// design-scope DEF NDR, never both.
struct PhysicalNdrReference
{
  TechNonDefaultRuleId tech_rule;
  DesignNonDefaultRuleId design_rule;

  [[nodiscard]] explicit operator bool() const noexcept { return static_cast<bool>(tech_rule) || static_cast<bool>(design_rule); }
};

// Routing-layer endpoints of a concrete VIA definition. The service returns
// them in technology stack order and only for VIAs DRT can use as a routing
// transition.
struct PhysicalRoutingViaLayers
{
  TechRoutingLayerId bottom;
  TechRoutingLayerId top;
};

// These visitors borrow EccDB route/geometry storage. Callback arguments are
// valid only during the callback; callers must not mutate the corresponding
// storage while visiting it.
struct PhysicalNetGeometryVisitor
{
  // These are borrowed canonical GeometryRegistry components.  In
  // particular, polygon points and VIA origins are not copied into an
  // import-boundary DTO for every callback.
  std::function<void(const DesignGeometryRectangle&)> rectangle;
  std::function<void(const DesignGeometryPolygon&)> polygon;
  std::function<void(const DesignGeometryNetVia&)> via;
};

// Read-only, adapter-facing view over the three EccDB storage roots. It does
// not expose ODB/iRT types and does not cache pointers into mutable storage.
class PhysicalDesignService
{
 public:
  using RoutingLayerVisitor
      = std::function<void(TechRoutingLayerId, uint32_t sequence_index, uint32_t routing_level, const TechLayerInfo&, const TechRoutingLayer&)>;
  using CutLayerVisitor = std::function<void(TechCutLayerId, uint32_t sequence_index, const TechLayerInfo&, const TechCutLayer&)>;
  using MastersliceLayerVisitor
      = std::function<void(TechMastersliceLayerId, uint32_t sequence_index, const TechLayerInfo&, const TechMastersliceLayer&)>;
  using TrackGridVisitor = std::function<void(DesignTrackGridId, const DesignTrackGrid&)>;
  using GCellGridVisitor = std::function<void(DesignGCellGridId, const DesignGCellGrid&)>;
  using NetVisitor = std::function<void(DesignNetId, const DesignNet&)>;
  using NetWireVisitor = std::function<void(DesignWireId, const DesignWire&)>;
  using RoutingBlockageVisitor = std::function<void(DesignBlockageId, const DesignBlockage&)>;
  using TechViaVisitor = std::function<void(TechViaMasterId, const TechViaMaster&, const TechViaGeometry&)>;
  using TechViaRuleGenerateVisitor
      = std::function<void(TechViaRuleGenerateId, const TechViaRuleGenerate&, const TechViaRuleGenerateBottomLayer&,
                           const TechViaRuleGenerateCutLayer&, const TechViaRuleGenerateTopLayer&)>;
  using DesignViaVisitor = std::function<void(DesignViaId, const DesignVia&)>;
  using SiteVisitor = std::function<void(LibrarySiteId, const LibrarySite&)>;
  using MasterVisitor = std::function<void(LibraryCellMasterId, const LibraryCellMaster&)>;
  using RowVisitor = std::function<void(DesignRowId, const DesignRow&)>;
  using InstanceVisitor
      = std::function<void(DesignInstanceId, const DesignInstance&, std::span<const DesignInstanceTermId>)>;
  using InstanceTermVisitor = std::function<void(DesignInstanceTermId, const DesignInstanceTerm&)>;
  using BlockTermVisitor = std::function<void(DesignBlockTermId, const DesignBlockTerm&)>;
  using CutSpacingRuleVisitor = std::function<void(TechCutSpacingRuleId, const TechCutSpacingRule&)>;

  PhysicalDesignService(const TechStore& tech, const LibraryStore& library, const DesignStore& design);
  explicit PhysicalDesignService(const DatabaseContext& context);

  [[nodiscard]] const TechStore& tech() const noexcept { return _tech; }
  [[nodiscard]] const LibraryStore& library() const noexcept { return _library; }
  [[nodiscard]] const DesignStore& design() const noexcept { return _design; }

  [[nodiscard]] bool hasDesignInfo() const noexcept;
  [[nodiscard]] const DesignInfo& designInfo() const;
  [[nodiscard]] int32_t databaseUnitsPerMicron() const;
  [[nodiscard]] std::optional<int32_t> manufacturingGrid() const noexcept;
  [[nodiscard]] bool hasDieArea() const noexcept;
  [[nodiscard]] std::span<const Point> dieBoundary() const;
  [[nodiscard]] Rect dieBounds() const;
  [[nodiscard]] bool hasCoreArea() const noexcept;
  [[nodiscard]] Rect coreBounds() const;

  [[nodiscard]] TechLayerId findLayer(std::string_view name) const;
  [[nodiscard]] std::optional<uint32_t> layerPosition(TechLayerId layer) const noexcept;
  [[nodiscard]] LibrarySiteId findSite(std::string_view name) const;
  [[nodiscard]] const LibrarySite& site(LibrarySiteId id) const;
  [[nodiscard]] const LibraryCellMaster& master(LibraryCellMasterId id) const;
  [[nodiscard]] const LibraryMasterTerm& masterTerm(LibraryMasterTermId id) const;
  [[nodiscard]] const DesignInstance& instance(DesignInstanceId id) const;
  [[nodiscard]] const DesignInstanceTerm& instanceTerm(DesignInstanceTermId id) const;
  [[nodiscard]] std::span<const DesignInstanceTermId> instanceTerms(DesignInstanceId instance) const;
  [[nodiscard]] const DesignBlockPin& blockPin(DesignBlockPinId id) const;
  [[nodiscard]] const DesignPinRectangle& blockPinRectangle(GeometryShapeId id) const noexcept;
  [[nodiscard]] DesignNetId findSpecialNet(std::string_view name) const;

  void forEachSite(const SiteVisitor& visitor) const;
  void forEachMaster(const MasterVisitor& visitor) const;
  void forEachRow(const RowVisitor& visitor) const;
  void forEachInstance(const InstanceVisitor& visitor) const;
  void forEachInstanceTerm(const InstanceTermVisitor& visitor) const;
  void forEachBlockTerm(const BlockTermVisitor& visitor) const;
  [[nodiscard]] Rect rowBounds(DesignRowId row) const;

  [[nodiscard]] GeometryLayerKind layerKind(TechLayerId layer) const;
  [[nodiscard]] bool isMastersliceLayer(TechLayerId layer) const;
  [[nodiscard]] const TechLayerInfo& layerInfo(TechLayerId layer) const;
  [[nodiscard]] const TechRoutingLayer& routingLayer(TechRoutingLayerId layer) const;
  [[nodiscard]] int32_t routingSpacing(TechRoutingLayerId layer, int32_t width, int32_t parallel_length = 0) const;
  [[nodiscard]] const TechCutLayer& cutLayer(TechCutLayerId layer) const;
  [[nodiscard]] std::optional<uint32_t> routingLevel(TechRoutingLayerId layer) const noexcept;
  void forEachRoutingLayer(const RoutingLayerVisitor& visitor) const;
  void forEachCutLayer(const CutLayerVisitor& visitor) const;
  void forEachMastersliceLayer(const MastersliceLayerVisitor& visitor) const;
  void forEachCutSpacingRule(TechCutLayerId layer, const CutSpacingRuleVisitor& visitor) const;

  [[nodiscard]] const TechViaRuleGenerate& techViaRuleGenerate(TechViaRuleGenerateId id) const;
  [[nodiscard]] const TechViaRuleGenerateBottomLayer& techViaRuleGenerateBottomLayer(TechViaRuleGenerateId id) const;
  [[nodiscard]] const TechViaRuleGenerateCutLayer& techViaRuleGenerateCutLayer(TechViaRuleGenerateId id) const;
  [[nodiscard]] const TechViaRuleGenerateTopLayer& techViaRuleGenerateTopLayer(TechViaRuleGenerateId id) const;
  [[nodiscard]] std::vector<TechViaRuleGenerateId> viaRuleGeneratesForLayer(TechLayerId layer) const;
  [[nodiscard]] std::vector<TechViaRuleGenerateId> viaRuleGeneratesForCutLayer(TechCutLayerId layer) const;

  void forEachTrackGrid(const TrackGridVisitor& visitor) const;
  void forEachTrackGrid(TechRoutingLayerId layer, const TrackGridVisitor& visitor) const;
  void forEachGCellGrid(const GCellGridVisitor& visitor) const;
  [[nodiscard]] std::vector<int32_t> trackCoordinates(DesignTrackGridId grid) const;
  [[nodiscard]] std::vector<int32_t> trackCoordinates(TechRoutingLayerId layer, DesignAxis axis) const;

  void forEachRegularNet(const NetVisitor& visitor) const;
  void forEachSpecialNet(const NetVisitor& visitor) const;
  [[nodiscard]] DesignNetId findRegularNet(std::string_view name) const;
  [[nodiscard]] DesignInstanceId findInstance(std::string_view name) const;
  // The pin name is the logical LibraryMasterTerm name, not a physical PORT
  // name. Requires a live instance and library cache, with master-term
  // membership/order unchanged since instance creation.
  [[nodiscard]] DesignInstancePinId findInstancePin(DesignInstanceId instance, std::string_view master_term_name) const;
  // OpenDB-aligned dbITerm spelling. InstancePin remains a compatibility
  // alias for the same independent design-term component.
  [[nodiscard]] DesignInstanceTermId findInstanceTerm(DesignInstanceId instance, std::string_view master_term_name) const;
  [[nodiscard]] const DesignInstanceTerm* findInstanceTermData(DesignInstanceId instance,
                                                                 std::string_view master_term_name) const;
  [[nodiscard]] DesignIoPinId findIoPin(std::string_view name) const;
  // OpenDB-aligned dbBTerm spelling. IoPin remains a compatibility alias for
  // the same block-term entity.
  [[nodiscard]] DesignBlockTermId findBlockTerm(std::string_view name) const;
  [[nodiscard]] LibraryCellMasterId findMaster(std::string_view name) const;
  [[nodiscard]] LibraryMasterTermId findMasterTerm(LibraryCellMasterId master, std::string_view name) const;
  // Preserves source LEF PORT order. The span is invalidated if the term's
  // port storage is changed.
  [[nodiscard]] std::span<const LibraryMasterPortId> masterPorts(LibraryMasterTermId term) const;
  // OpenDB-aligned dbMPin spelling. MasterPort remains a compatibility alias
  // for the same independent library-pin entity.
  [[nodiscard]] std::span<const LibraryMasterPinId> masterPins(LibraryMasterTermId term) const;
  // Preserves source DEF BPin order. The span is invalidated if the block
  // term's pin storage is changed.
  [[nodiscard]] std::span<const DesignBlockPinId> blockPins(DesignBlockTermId term) const;
  using BlockPinVisitor = std::function<void(DesignBlockPinId, const DesignBlockPin&)>;
  void forEachBlockPin(DesignBlockTermId term, const BlockPinVisitor& visitor) const;
  // Requires intact pin relations and live cached instance/master-term objects;
  // the read path does not revalidate them.
  void forEachNetPin(DesignNetId net, const PhysicalNetPinVisitor& visitor) const;
  [[nodiscard]] PhysicalNdrReference nonDefaultRule(DesignNetId net) const;
  [[nodiscard]] bool netHasJumpers(DesignNetId net) const;
  [[nodiscard]] bool isNetConnectedByAbutment(DesignNetId net) const;
  [[nodiscard]] std::optional<std::string_view> nonDefaultRuleName(DesignNetId net) const;

  void forEachNetWire(DesignNetId net, const NetWireVisitor& visitor) const;
  using NetWireShapeVisitor = std::function<void(DesignWireId, const DesignWire&, DesignRouteShapeId,
                                                const DesignRouteShape&, const DesignRouteShapeAttributes*)>;
  // Direct geometry access; no DEF path decoding or per-Shape snapshot.
  void forEachNetWireShape(DesignNetId net, const NetWireShapeVisitor& visitor) const;
  // Resolves width/NDR and DEF endpoint extensions in the owning net context.
  // Exact rectangle for Manhattan segments/patches; conservative bounds for
  // diagonal segments and complex via geometry.
  [[nodiscard]] Rect routeShapeBounds(DesignRouteShapeId shape) const;
  void forEachNetGeometry(DesignNetId net, const PhysicalNetGeometryVisitor& visitor) const;
  [[nodiscard]] std::string_view viaName(TechViaMasterId tech_via, DesignViaId design_via) const;
  [[nodiscard]] PhysicalRoutingViaLayers routingViaLayers(TechViaMasterId tech_via, DesignViaId design_via) const;

  [[nodiscard]] bool isPlaced(DesignInstanceId instance) const;
  // Transforms a master-local point using the same placement transform as
  // the instance geometry visitors. The instance must be placed.
  [[nodiscard]] Point placeInstanceMasterPoint(DesignInstanceId instance, Point point) const;
  // Visits one LEF PORT / dbMPin in master-local coordinates. The visitor
  // borrows only for the callback duration; it does not create design-domain
  // Shape objects for library geometry.
  void forEachMasterPinGeometry(LibraryMasterPinId pin, const PhysicalGeometryVisitor& visitor) const;
  // Visits one LEF PORT as it occurs below one placed dbITerm. This is the
  // precise occurrence counterpart to the aggregate dbITerm method below.
  void forEachPlacedInstanceMasterPinGeometry(DesignInstanceTermId iterm, LibraryMasterPinId pin,
                                              const PhysicalGeometryVisitor& visitor) const;
  void forEachPlacedInstancePinGeometry(DesignInstancePinId pin, const PhysicalGeometryVisitor& visitor) const;
  // Visits one OpenDB dbBPin / DEF PORT. The geometry is transformed from the
  // BPin-local coordinates using only this BPin's placement.
  void forEachBlockPinGeometry(DesignBlockPinId pin, const PhysicalGeometryVisitor& visitor) const;
  // Compatibility aggregate over every dbBPin owned by one dbBTerm.
  void forEachIoPinGeometry(DesignIoPinId pin, const PhysicalGeometryVisitor& visitor) const;
  void forEachPlacedMasterObstructionGeometry(DesignInstanceId instance, const PhysicalGeometryVisitor& visitor) const;

  void forEachRoutingBlockage(const RoutingBlockageVisitor& visitor) const;
  void forEachRoutingBlockageGeometry(DesignBlockageId blockage, const PhysicalGeometryVisitor& visitor) const;

  void forEachTechVia(const TechViaVisitor& visitor) const;
  void forEachTechViaRuleGenerate(const TechViaRuleGenerateVisitor& visitor) const;
  void forEachDesignVia(const DesignViaVisitor& visitor) const;
  void forEachTechViaGeometry(TechViaMasterId via, const PhysicalGeometryVisitor& visitor) const;
  void forEachTechViaGeometry(TechViaMasterId via, Point origin, DesignOrientation orientation,
                              const PhysicalGeometryVisitor& visitor) const;
  void forEachDesignViaGeometry(DesignViaId via, const PhysicalGeometryVisitor& visitor) const;
  void forEachDesignViaGeometry(DesignViaId via, Point origin, DesignOrientation orientation,
                                const PhysicalGeometryVisitor& visitor) const;

 private:
  const TechStore& _tech;
  const LibraryStore& _library;
  const DesignStore& _design;
};

}  // namespace eccdb
