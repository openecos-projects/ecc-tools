// SPDX-License-Identifier: Apache-2.0

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
#include "design/global/model/DesignGlobalComponents.h"
#include "design/netlist/model/NetlistComponents.h"
#include "design/non_default_rule/component/NonDefaultRuleComponents.h"
#include "design/via/component/ViaComponents.h"
#include "library/cell_master/model/CellMasterComponents.h"
#include "library/cell_master/model/MasterObsComponents.h"
#include "library/master_term/model/MasterTermComponents.h"
#include "tech/common/TechLayerTypes.h"
#include "tech/cut_layer/model/CutLayerComponents.h"
#include "tech/non_default_rule/model/NonDefaultRuleComponents.h"
#include "tech/routing_layer/model/RoutingLayerComponents.h"
#include "tech/via_master/model/ViaMasterComponents.h"

namespace eccdb {

class DesignStore;
class LibraryStore;
class TechStore;

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

// Read-only, adapter-facing view over the three EccDB storage roots. It does
// not expose ODB/iRT types and does not cache pointers into mutable storage.
class PhysicalDesignService
{
 public:
  using RoutingLayerVisitor
      = std::function<void(TechRoutingLayerId, uint32_t sequence_index, uint32_t routing_level, const TechLayerInfo&, const TechRoutingLayer&)>;
  using CutLayerVisitor = std::function<void(TechCutLayerId, uint32_t sequence_index, const TechLayerInfo&, const TechCutLayer&)>;
  using TrackGridVisitor = std::function<void(DesignTrackGridId, const DesignTrackGrid&)>;
  using GCellGridVisitor = std::function<void(DesignGCellGridId, const DesignGCellGrid&)>;
  using NetVisitor = std::function<void(DesignNetId, const DesignNet&)>;
  using RoutingBlockageVisitor = std::function<void(DesignBlockageId, const DesignBlockage&)>;
  using TechViaVisitor = std::function<void(TechViaMasterId, const TechViaMaster&, const TechViaGeometry&)>;
  using DesignViaVisitor = std::function<void(DesignViaId, const DesignVia&)>;

  PhysicalDesignService(const TechStore& tech, const LibraryStore& library, const DesignStore& design);

  [[nodiscard]] const TechStore& tech() const noexcept { return _tech; }
  [[nodiscard]] const LibraryStore& library() const noexcept { return _library; }
  [[nodiscard]] const DesignStore& design() const noexcept { return _design; }

  [[nodiscard]] bool hasDesignInfo() const noexcept;
  [[nodiscard]] int32_t databaseUnitsPerMicron() const;
  [[nodiscard]] bool hasDieArea() const noexcept;
  [[nodiscard]] const DesignDieArea& dieArea() const;
  [[nodiscard]] Rect dieBounds() const;

  [[nodiscard]] GeometryLayerKind layerKind(TechLayerId layer) const;
  [[nodiscard]] const TechLayerInfo& layerInfo(TechLayerId layer) const;
  [[nodiscard]] const TechRoutingLayer& routingLayer(TechRoutingLayerId layer) const;
  [[nodiscard]] const TechCutLayer& cutLayer(TechCutLayerId layer) const;
  [[nodiscard]] std::optional<uint32_t> routingLevel(TechRoutingLayerId layer) const noexcept;
  void forEachRoutingLayer(const RoutingLayerVisitor& visitor) const;
  void forEachCutLayer(const CutLayerVisitor& visitor) const;

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
  [[nodiscard]] DesignIoPinId findIoPin(std::string_view name) const;
  [[nodiscard]] LibraryCellMasterId findMaster(std::string_view name) const;
  [[nodiscard]] LibraryMasterTermId findMasterTerm(LibraryCellMasterId master, std::string_view name) const;
  // Preserves source LEF PORT order. The span is invalidated if the term's
  // port storage is changed.
  [[nodiscard]] std::span<const LibraryMasterPortId> masterPorts(LibraryMasterTermId term) const;
  // Requires intact pin relations and live cached instance/master-term objects;
  // the read path does not revalidate them.
  void forEachNetPin(DesignNetId net, const PhysicalNetPinVisitor& visitor) const;
  [[nodiscard]] PhysicalNdrReference nonDefaultRule(DesignNetId net) const;

  [[nodiscard]] bool isPlaced(DesignInstanceId instance) const;
  void forEachPlacedInstancePinGeometry(DesignInstancePinId pin, const PhysicalGeometryVisitor& visitor) const;
  void forEachIoPinGeometry(DesignIoPinId pin, const PhysicalGeometryVisitor& visitor) const;
  void forEachPlacedMasterObstructionGeometry(DesignInstanceId instance, const PhysicalGeometryVisitor& visitor) const;

  void forEachRoutingBlockage(const RoutingBlockageVisitor& visitor) const;
  void forEachRoutingBlockageGeometry(DesignBlockageId blockage, const PhysicalGeometryVisitor& visitor) const;

  void forEachTechVia(const TechViaVisitor& visitor) const;
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
