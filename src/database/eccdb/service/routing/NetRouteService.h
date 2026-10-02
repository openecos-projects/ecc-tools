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

// Optional diagnostic counters, accumulated once per Wire replacement.
// Runtime only: no per-Shape clocks and no persisted profiling state.
struct NetRouteWriteProfile
{
  uint64_t calls = 0;
  uint64_t input_shapes = 0;
  uint64_t replaced_wires = 0;
  uint64_t replaced_shapes = 0;
  uint64_t select_wires_ns = 0;
  // Retained for historical diagnostic output. Conversion now happens during
  // append_shapes_ns, directly into the staged Wire, without a candidate array.
  uint64_t convert_shapes_ns = 0;
  uint64_t create_wire_ns = 0;
  uint64_t append_shapes_ns = 0;  // conversion, checks, and entity/component insertion
  uint64_t destroy_old_ns = 0;
  uint64_t service_total_ns = 0;
};

// Store-level route geometry facade for ordinary DEF NETS. It intentionally
// does not expose ODB types. A snapshot projects independent Shape entities
// directly; a replacement stages one Shape per supplied primitive in a new
// routed Wire before removing prior routed geometry.
class NetRouteService
{
 public:
  NetRouteService(DesignStore& design, const TechStore& technology);

  // Reads the physical fields represented by RouteShape without per-Shape
  // validation. Complete DEF metadata stays in Storage; this adapter snapshot
  // does not carry masks, arrays, styles, or point-only geometry.
  [[nodiscard]] NetRouteSnapshot snapshot(DesignNetId net) const;

  // Replaces only kRouted Wire objects. Non-routed Wire objects retain both
  // their identity and complete Storage payload. An empty input clears routed
  // wires and returns an invalid DesignWireId. This is canonical new geometry,
  // not an in-place path edit: like an ODB routed-wire replacement, it
  // deliberately discards the replaced routed payload, including attributes
  // that RouteShape does not model. Each DTO becomes one independent Shape
  // and DEF source-path group. DEF serializes explicit width only for
  // SPECIALNET paths, so width is not writeable through this ordinary NET
  // service.
  [[nodiscard]] DesignWireId replaceRouted(DesignNetId net, std::span<const RouteShape> shapes,
                                         NetRouteWriteProfile* profile = nullptr);

 private:
  DesignStore& _design;
  const TechStore& _technology;
};

}  // namespace eccdb
