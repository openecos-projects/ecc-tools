// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "context/DatabaseContext.h"
#include "design/common/DesignTypes.h"
#include "design/guide/model/GuideComponents.h"
#include "design/pin_access/model/PinAccessComponents.h"
#include "design/routing/component/RoutingComponents.h"
#include "design/via/component/ViaComponents.h"
#include "tech/common/TechLayerIds.h"
#include "tech/via_master/model/ViaMasterComponents.h"

namespace eccdb {

class DesignStore;

// A physical routing segment after path-relative wire data has been expanded.
// Endpoint extensions are present only when the corresponding DEF point
// carried an explicit extension.
struct RouteSegment
{
  TechRoutingLayerId layer;
  Point begin;
  Point end;
  // Retained in the generic shape type, but ordinary DEF NET replacement
  // rejects it because explicit width belongs to SPECIALNET syntax.
  std::optional<int32_t> width;
  std::optional<int32_t> begin_extension;
  std::optional<int32_t> end_extension;
  bool tapered = false;
};

using RouteViaDefinition = std::variant<TechViaMasterId, DesignViaId>;

struct RouteVia
{
  RouteViaDefinition definition;
  Point origin;
  DesignOrientation orientation = DesignOrientation::kN;
  bool tapered = false;
};

// A patch wire rectangle in absolute design coordinates.
struct RoutePatch
{
  TechRoutingLayerId layer;
  Rect rectangle;
  bool tapered = false;
};

using RouteShape = std::variant<RouteSegment, RouteVia, RoutePatch>;

// One persisted DesignWire projected into route geometry. The source Wire ID
// and status make fixed/cover/shield geometry distinguishable from routable
// geometry without exposing Storage internals to adapters.
struct NetRouteWireSnapshot
{
  DesignWireId wire;
  DesignWireStatus status = DesignWireStatus::kRouted;
  std::string shield_net;
  std::vector<RouteShape> shapes;
};

struct NetRouteSnapshot
{
  DesignNetId net;
  std::vector<NetRouteWireSnapshot> wires;
};

// Optional diagnostic counters, accumulated once per routed-Wire replacement.
// Runtime only: no per-Shape clocks and no persisted profiling state.
struct NetRouteWriteProfile
{
  uint64_t calls = 0;
  uint64_t input_shapes = 0;
  uint64_t replaced_wires = 0;
  uint64_t replaced_shapes = 0;
  uint64_t select_wires_ns = 0;
  uint64_t convert_shapes_ns = 0;
  uint64_t create_wire_ns = 0;
  uint64_t append_shapes_ns = 0;
  uint64_t destroy_old_ns = 0;
  uint64_t service_total_ns = 0;
};

enum class RoutedResultOutcome : uint8_t { kPreserve, kClear, kReplace, kSkipped, kFailed };

// Borrowed native results; empty geometry only clears when explicitly requested.
struct RoutedNetResult
{
  DesignNetId net;
  RoutedResultOutcome outcome = RoutedResultOutcome::kPreserve;
  std::span<const DesignRouteShape> shapes;
  // Optional parallel span for cold native DEF attributes. Empty means absent
  // on every shape; otherwise its size must match shapes exactly.
  std::span<const std::optional<DesignRouteShapeAttributes>> attributes;
};

// Business boundary for routing-owned design data. Storage keeps the canonical
// Shape/Wire/Via records and their indexes; this service coordinates their
// cross-storage lifecycle and emits one context notification per logical edit.
// It deliberately owns no route cache or path-stream compatibility layer.
class RoutingService
{
 public:
  explicit RoutingService(DatabaseContext& context);

  [[nodiscard]] DesignStore& design() noexcept;
  [[nodiscard]] const DesignStore& design() const noexcept;

  // Design-local VIA definitions.
  [[nodiscard]] DesignViaId createVia(DesignViaInput via);
  [[nodiscard]] DesignViaId findVia(std::string_view name) const;
  [[nodiscard]] const DesignVia& via(DesignViaId id) const;
  void updateVia(DesignViaId id, DesignViaInput via);
  [[nodiscard]] bool destroyVia(DesignViaId id);

  // Canonical editable route geometry. A Wire owns an ordered set of Geometry
  // Shape IDs, while each Shape stores the actual segment/VIA/patch/point data.
  [[nodiscard]] DesignWireId createWire(DesignWire wire);
  [[nodiscard]] bool destroyWire(DesignWireId id);
  [[nodiscard]] bool contains(DesignWireId id) const;
  [[nodiscard]] const DesignWire& wire(DesignWireId id) const;

  [[nodiscard]] GeometryShapeId createShape(DesignWireId wire,
                                             DesignRouteShape shape,
                                             std::optional<DesignRouteShapeAttributes> attributes = std::nullopt);
  void updateShape(GeometryShapeId id,
                   DesignRouteShape shape,
                   std::optional<DesignRouteShapeAttributes> attributes = std::nullopt);
  [[nodiscard]] bool destroyShape(GeometryShapeId id);
  [[nodiscard]] bool contains(GeometryShapeId id) const;
  [[nodiscard]] const DesignRouteShape& shape(GeometryShapeId id) const;
  [[nodiscard]] const DesignRouteShapeAttributes* shapeAttributes(GeometryShapeId id) const;
  [[nodiscard]] Rect shapeBoundingBox(GeometryShapeId id) const;

  // Adapter projection for ordinary DEF NETS. Snapshot needs only canonical
  // design data. Replacement additionally requires the context TechStore to
  // resolve route layers and VIA definitions.
  [[nodiscard]] NetRouteSnapshot snapshot(DesignNetId net) const;
  [[nodiscard]] DesignWireId replaceRouted(DesignNetId net,
                                           std::span<const RouteShape> shapes,
                                           NetRouteWriteProfile* profile = nullptr);
  // All new Wires are staged before retiring any selected old Routed Wire.
  // Returned IDs follow the result order (null for non-replacement outcomes).
  [[nodiscard]] std::vector<DesignWireId> commitRoutedResults(std::span<const RoutedNetResult> results);

  // Global-route guides and track-assignment results are Net-owned geometry.
  [[nodiscard]] bool hasGuides(DesignNetId net) const;
  [[nodiscard]] std::span<const GeometryShapeId> guides(DesignNetId net) const;
  [[nodiscard]] const DesignRouteGuide& guide(GeometryShapeId id) const;
  void replaceGuides(DesignNetId net, std::vector<DesignRouteGuide> guides);
  void clearGuides(DesignNetId net);
  // Guide updates invalidate old TA. A combined stage replacement preserves
  // the new TA; nullopt guides implement TA-only output without changing guides.
  void replaceGuideStage(DesignNetId net,
                         std::optional<std::vector<DesignRouteGuide>> guides,
                         std::vector<DesignTrackAssignmentShape> assignments);

  [[nodiscard]] bool hasTrackAssignments(DesignNetId net) const;
  [[nodiscard]] std::span<const GeometryShapeId> trackAssignments(DesignNetId net) const;
  [[nodiscard]] const DesignTrackAssignmentShape& trackAssignment(GeometryShapeId id) const;
  void replaceTrackAssignments(DesignNetId net, std::vector<DesignTrackAssignmentShape> assignments);
  void appendTrackAssignments(DesignNetId net, std::vector<DesignTrackAssignmentShape> assignments);
  void clearTrackAssignments(DesignNetId net);

  // Pin-access candidates are router-derived state. They remain outside the
  // canonical geometry payload but share this service because they reference
  // routing layers and VIA definitions.
  [[nodiscard]] bool hasMasterPortCandidateGroup(LibraryMasterPortId master_port, uint32_t group_index) const;
  [[nodiscard]] std::span<const DesignPinAccessCandidateGroup> masterPortCandidateGroups(LibraryMasterPortId master_port) const;
  [[nodiscard]] const DesignPinAccessCandidateGroup& masterPortCandidateGroup(LibraryMasterPortId master_port,
                                                                                uint32_t group_index) const;
  void setMasterPortCandidateGroup(LibraryMasterPortId master_port,
                                   uint32_t group_index,
                                   std::vector<DesignPinAccessPoint> points);
  [[nodiscard]] bool clearMasterPortCandidateGroup(LibraryMasterPortId master_port, uint32_t group_index);
  [[nodiscard]] bool clearMasterPortCandidateGroups(LibraryMasterPortId master_port);

  [[nodiscard]] bool hasBlockPinCandidateGroup(DesignBlockPinId pin, uint32_t group_index) const;
  [[nodiscard]] std::span<const DesignPinAccessCandidateGroup> blockPinCandidateGroups(DesignBlockPinId pin) const;
  [[nodiscard]] const DesignPinAccessCandidateGroup& blockPinCandidateGroup(DesignBlockPinId pin,
                                                                               uint32_t group_index) const;
  void setBlockPinCandidateGroup(DesignBlockPinId pin, uint32_t group_index, std::vector<DesignPinAccessPoint> points);
  void appendBlockPinCandidatePoints(DesignBlockPinId pin,
                                     uint32_t group_index,
                                     std::vector<DesignPinAccessPoint> points);
  [[nodiscard]] bool clearBlockPinCandidateGroup(DesignBlockPinId pin, uint32_t group_index);
  [[nodiscard]] bool clearBlockPinCandidateGroups(DesignBlockPinId pin);

  // Trace compatibility for dbBTerm + port ordinal addressing. Both forms
  // delegate to the same DesignPinAccessStorage data.
  [[nodiscard]] bool hasIoPinPortCandidateGroup(DesignIoPinId pin, uint32_t port_index, uint32_t group_index) const;
  [[nodiscard]] std::span<const DesignPinAccessCandidateGroup> ioPinPortCandidateGroups(DesignIoPinId pin,
                                                                                            uint32_t port_index) const;
  [[nodiscard]] const DesignPinAccessCandidateGroup& ioPinPortCandidateGroup(DesignIoPinId pin,
                                                                                uint32_t port_index,
                                                                                uint32_t group_index) const;
  void setIoPinPortCandidateGroup(DesignIoPinId pin,
                                  uint32_t port_index,
                                  uint32_t group_index,
                                  std::vector<DesignPinAccessPoint> points);
  void appendIoPinPortCandidatePoints(DesignIoPinId pin,
                                      uint32_t port_index,
                                      uint32_t group_index,
                                      std::vector<DesignPinAccessPoint> points);
  [[nodiscard]] bool clearIoPinPortCandidateGroup(DesignIoPinId pin, uint32_t port_index, uint32_t group_index);
  [[nodiscard]] bool clearIoPinPortCandidateGroups(DesignIoPinId pin, uint32_t port_index);

  void setInstancePinAccessGroup(DesignInstanceId instance, uint32_t group_index);
  [[nodiscard]] std::optional<uint32_t> instancePinAccessGroup(DesignInstanceId instance) const;
  [[nodiscard]] bool clearInstancePinAccessGroup(DesignInstanceId instance);

  void setPreferredInstancePinAccess(DesignInstancePinId pin, std::vector<DesignPreferredPinAccessRef> preferences);
  [[nodiscard]] std::span<const DesignPreferredPinAccessRef> preferredInstancePinAccess(DesignInstancePinId pin) const;
  [[nodiscard]] bool clearPreferredInstancePinAccess(DesignInstancePinId pin);
  void replacePinAccess(DesignPinAccessUpdate update);

 private:
  [[nodiscard]] DatabaseChangeDomain routingGeometryDomains() const noexcept;
  void notifyBefore(DatabaseChangeDomain domains,
                    std::span<const DesignInstanceId> instances = {},
                    std::span<const DesignNetId> nets = {},
                    std::span<const GeometryShapeId> shapes = {}) noexcept;
  void notifyAfter(DatabaseChangeDomain domains,
                   std::span<const DesignInstanceId> instances = {},
                   std::span<const DesignNetId> nets = {},
                   std::span<const GeometryShapeId> shapes = {},
                   std::span<const GeometryOwner> geometry_replacements = {}) noexcept;

  DatabaseContext& _context;
};

}  // namespace eccdb
