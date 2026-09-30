// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "common/GeometryTypes.h"
#include "design/common/DesignTypes.h"
#include "design/routing/component/RoutingComponents.h"
#include "tech/common/TechLayerIds.h"
#include "tech/via_master/model/ViaMasterComponents.h"

namespace eccdb {

class DesignStore;
class TechStore;

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
  // DRT represents tapering on individual path segments. The original DEF
  // flag is path-scoped, so a flattened snapshot carries it on every primitive
  // emitted from that path.
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
  // Non-empty only for kShield. Keeping it makes the Wire metadata lossless.
  std::string shield_net;
  std::vector<RouteShape> shapes;
};

struct NetRouteSnapshot
{
  DesignNetId net;
  std::vector<NetRouteWireSnapshot> wires;
};

// Store-level route geometry facade for ordinary DEF NETS. It intentionally
// does not expose ODB types. A snapshot expands the compact Path representation
// and follows layer transitions caused by VIAs; a replacement canonicalizes
// every supplied primitive into its own Path in one routed Wire.
class NetRouteService
{
 public:
  NetRouteService(DesignStore& design, const TechStore& technology);

  // Throws rather than silently dropping stored path or via attributes that
  // cannot be expressed as RouteShape geometry.
  [[nodiscard]] NetRouteSnapshot snapshot(DesignNetId net) const;

  // Replaces only kRouted Wire objects. Non-routed Wire objects retain both
  // their identity and complete Storage payload. An empty input clears routed
  // wires and returns an invalid DesignWireId. This is canonical new geometry,
  // not an in-place path edit: like an ODB routed-wire replacement, it
  // deliberately discards the replaced routed payload, including attributes
  // that RouteShape does not model. DEF serializes explicit width only for
  // SPECIALNET paths, so width is not writeable through this ordinary NET
  // service.
  [[nodiscard]] DesignWireId replaceRouted(DesignNetId net, std::span<const RouteShape> shapes);

 private:
  DesignStore& _design;
  const TechStore& _technology;
};

}  // namespace eccdb
