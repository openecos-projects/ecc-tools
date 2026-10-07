#pragma once

#include <concepts>
#include <functional>
#include <optional>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "design/geometry/DesignGeometryComponents.h"
#include "design/common/DesignGeometryValidation.h"
#include "design/netlist/model/NetlistComponents.h"
#include "design/guide/model/GuideComponents.h"
#include "design/via/component/ViaComponents.h"
#include "geometry/DomainGeometryComponents.h"
#include "design/routing/storage/DesignRoutingStorage.h"

namespace eccdb {

// A GUI handle is the EnTT entity itself. There is no owner-plus-ordinal
// encoding and no second pool locator.
using GeometryShapeHandle = GeometryShapeId;

using DesignGeometryPayload = std::variant<std::monostate,
                                           const DesignRouteShape*,
                                           const DesignGeometryRectangle*,
                                           const DesignGeometryPolygon*,
                                           const DesignGeometryNetVia*,
                                           const GeometryRectangle*,
                                           const GeometryPolygon*,
                                           const GeometryBoundary*,
                                           const DesignPinRectangle*,
                                           const DesignPinPolygon*,
                                           const DesignPinVia*,
                                           const DesignViaRectangle*,
                                           const DesignViaPolygon*,
                                           const DesignRouteGuide*,
                                           const DesignTrackAssignmentShape*>;

struct DesignGeometryShapeView
{
  GeometryShapeHandle handle;
  const GeometryShape* metadata = nullptr;
  DesignGeometryPayload payload;

  [[nodiscard]] bool valid() const noexcept { return metadata != nullptr && !std::holds_alternative<std::monostate>(payload); }
};

// Design-domain view/edit API over DesignRegistry's shared GeometryRegistry.
// It follows KLayout's typed proxy model: the view
// resolves an entity to its typed component and never owns a geometry copy.
class DesignGeometryStorage
{
 public:
  using ViaLayerBoundsVisitor = DesignRoutingStorage::ViaLayerBoundsVisitor;

  explicit DesignGeometryStorage(DesignRoutingStorage& routing) noexcept : _routing(routing) {}

  [[nodiscard]] GeometryRegistry& registry() noexcept { return _routing.geometryRegistry(); }
  [[nodiscard]] const GeometryRegistry& registry() const noexcept { return _routing.geometryRegistry(); }
  [[nodiscard]] bool contains(GeometryShapeId id) const noexcept { return registry().contains(id); }
  [[nodiscard]] GeometryOwner owner(GeometryShapeId id) const noexcept
  {
    return registry().contains(id) ? registry().shape(id).owner : GeometryOwner{};
  }

  // Geometry edits share the routing storage revision. A caller can keep a
  // snapshot and detect stale GUI/query views without a transaction layer.
  [[nodiscard]] uint64_t geometryEpoch() const noexcept { return _routing.geometryEpoch(); }
  [[nodiscard]] bool dirtySince(uint64_t epoch) const noexcept { return geometryEpoch() != epoch; }

  [[nodiscard]] DesignGeometryShapeView shape(GeometryShapeId id) const;
  // Layer metadata on generic owner shapes may be absent. Resolve the
  // canonical typed payload/owner rather than treating an invalid layer as M1.
  [[nodiscard]] TechLayerId effectiveLayer(GeometryShapeId id) const;
  [[nodiscard]] Rect bounds(GeometryShapeId id) const;
  // Compute a box without populating the per-shape Bounds cache.  Cold
  // derived indexes use this path so a full-chip rebuild does not materialize
  // a runtime component for every shape merely to copy its box into R-tree
  // nodes.
  [[nodiscard]] Rect boundsUncached(GeometryShapeId id) const;
  // Resolves only placed VIA shapes. It visits canonical per-layer primitive
  // bounds without constructing a detached physical-geometry payload.
  [[nodiscard]] bool forEachViaLayerBounds(GeometryShapeId id, const ViaLayerBoundsVisitor& visitor) const;
  [[nodiscard]] Rect localBounds(GeometryShapeId id) const;
  void validateRestoredState() const;
  [[nodiscard]] std::vector<GeometryShapeId> shapes(GeometryOwner owner) const;

  // Replace a non-route payload in place. Relations and handle identity stay
  // unchanged; route edits retain their dedicated DEF-attribute API.
  template <typename Payload>
  void replaceShape(GeometryShapeId id, Payload value)
  {
    if (!registry().has<Payload>(id)) throw std::invalid_argument("geometry payload type cannot change in place");
    validatePayload(value);
    registry().replace(id, std::move(value));
    notifyEdit(id);
  }
  // Owner relations are the editable order source, just like a KLayout
  // Shapes container.  These spans do not allocate; the IDs remain valid
  // until the corresponding owner is edited.
  [[nodiscard]] std::span<const GeometryShapeId> wireShapeIds(DesignWireId wire) const;
  [[nodiscard]] std::span<const GeometryShapeId> netShapeIds(DesignNetId net) const;
  [[nodiscard]] std::vector<GeometryShapeId> shapes(DesignNetId net) const;
  [[nodiscard]] std::vector<GeometryShapeId> query(DesignNetId net, Rect box) const;

  template <typename Function>
    requires std::invocable<Function&, GeometryShapeId, DesignGeometryShapeView>
  void forEachNetShape(DesignNetId net, Function&& function) const
  {
    for (const auto wire : _routing.wireIds(net)) {
      for (const auto id : wireShapeIds(wire)) std::invoke(function, id, shape(id));
    }
    for (const auto id : netShapeIds(net)) std::invoke(function, id, shape(id));
  }

  template <typename Function>
    requires std::invocable<Function&, GeometryShapeId, DesignGeometryShapeView>
  void forEach(DesignNetId net, Function&& function) const
  {
    forEachNetShape(net, std::forward<Function>(function));
  }

  [[nodiscard]] GeometryShapeId createRouteShape(DesignWireId wire, DesignRouteShape value,
                                                 std::optional<DesignRouteShapeAttributes> attributes = std::nullopt)
  {
    return _routing.createShape(wire, std::move(value), std::move(attributes));
  }
  void updateRouteShape(GeometryShapeId id, DesignRouteShape value,
                        std::optional<DesignRouteShapeAttributes> attributes = std::nullopt)
  {
    _routing.updateShape(id, std::move(value), std::move(attributes));
  }
  // Replace the geometry in place while preserving the entity handle.  The
  // operation is the canonical edit boundary for GUI and router clients;
  // callers must not mutate a payload pointer returned by shape().
  void moveShape(GeometryShapeId id, int32_t dx, int32_t dy);
  // Reorders only the owner relation.  Shape entities and their IDs remain
  // unchanged, which preserves DEF path order and GUI selections.
  void reorderWireShapes(DesignWireId wire, std::span<const GeometryShapeId> order);
  [[nodiscard]] bool destroy(GeometryShapeId id);

  [[nodiscard]] GeometryShapeId appendNetShape(DesignNetId net, DesignNetRectangle value);
  [[nodiscard]] GeometryShapeId appendNetShape(DesignNetId net, DesignNetPolygon value);
  [[nodiscard]] GeometryShapeId appendNetShape(DesignNetId net, DesignNetVia value);
  void replaceNetShape(GeometryShapeId id, DesignNetRectangle value);
  void replaceNetShape(GeometryShapeId id, DesignNetPolygon value);
  void replaceNetShape(GeometryShapeId id, DesignNetVia value);

 private:
  [[nodiscard]] DesignNetGeometry& netGeometry(DesignNetId net);
  [[nodiscard]] static bool intersects(Rect lhs, Rect rhs) noexcept;
  [[nodiscard]] static Rect polygonBounds(const std::vector<Point>& points);
  [[nodiscard]] Rect viaBounds(const DesignGeometryNetVia& via) const;
  [[nodiscard]] Rect computeLocalBounds(GeometryShapeId id) const;
  [[nodiscard]] GeometryShapeId createNetPayload(DesignNetId net, DesignGeometryRectangle value);
  [[nodiscard]] GeometryShapeId createNetPayload(DesignNetId net, DesignGeometryPolygon value);
  [[nodiscard]] GeometryShapeId createNetPayload(DesignNetId net, DesignGeometryNetVia value);

  template <typename Payload>
  static void validatePayload(const Payload& value)
  {
    if constexpr (std::is_same_v<Payload, GeometryBoundary>) {
      static_cast<void>(validateDesignOrthogonalBoundary(value.points, "design boundary"));
    } else if constexpr (requires { value.rectangle; }) {
      if (!value.rectangle.hasArea()) throw std::invalid_argument("geometry rectangle must have area");
    } else if constexpr (requires { value.points; }) {
      if (value.points.size() < 3u) throw std::invalid_argument("geometry has too few points");
    } else if constexpr (requires { value.box; }) {
      if (!value.box.isValid()) throw std::invalid_argument("geometry box is invalid");
    }
  }
  void notifyEdit(GeometryShapeId id);

  DesignRoutingStorage& _routing;
};

}  // namespace eccdb
