#pragma once

#include <string>
#include <limits>
#include <variant>
#include <vector>

#include "common/GeometryTypes.h"
#include "design/common/DesignTypes.h"
#include "design/routing/component/WireRouting.h"
#include "tech/common/TechLayerIds.h"
#include "tech/via_master/model/ViaMasterComponents.h"

namespace eccdb {

enum class DesignWireStatus : uint8_t
{
  kRouted,
  kFixed,
  kCover,
  kShield,
  kNoShield
};

// Wire is a status/ownership group, not a DEF program or a topology edge.
// Geometry lives once, in the independent Shape entities listed here.
struct DesignWire
{
  DesignNetId net;
  DesignWireStatus status = DesignWireStatus::kRouted;
  std::string shield_net;
  std::vector<DesignRouteShapeId> shapes;
  uint32_t next_source_path = 0;
};

struct DesignRouteSegment
{
  DesignWirePoint begin;
  DesignWirePoint end;
};

struct DesignRouteVia
{
  DesignWirePoint origin;
  // point_index is unused in canonical geometry; projections set the anchor.
  DesignWireVia via;
  TechRoutingLayerId exit_layer;
};

struct DesignRoutePatch
{
  DesignWirePoint anchor;
  Rect rectangle;  // absolute coordinates; anchor is only a DEF emission hint
};

// A lone or virtual DEF point can carry syntax without manufacturing metal.
struct DesignRoutePoint
{
  DesignWirePoint point;
};

using DesignRouteGeometry = std::variant<DesignRouteSegment, DesignRouteVia, DesignRoutePatch, DesignRoutePoint>;

struct DesignRouteShape
{
  DesignWireId wire;
  TechRoutingLayerId layer;
  uint32_t flags = 0;  // DesignWirePathFlag; cold values are a separate component
  // Import boundary hint only. Editing may split this group into multiple NEW
  // paths. No points, ranges or topology are stored per path.
  uint32_t source_path = std::numeric_limits<uint32_t>::max();
  DesignRouteGeometry geometry;
};

// Optional per-Shape DEF attributes. Ordinary routed geometry needs no strings.
using DesignRouteShapeAttributes = DesignWirePathExtra;

namespace DesignNetGeometryFlag {
constexpr uint32_t kHasMask = 1u << 0;
constexpr uint32_t kHasShape = 1u << 1;
}  // namespace DesignNetGeometryFlag

struct DesignNetRectangle
{
  TechRoutingLayerId layer;
  Rect rectangle;
  DesignWireStatus route_status = DesignWireStatus::kRouted;
  uint32_t flags = 0;
  uint32_t mask = 0;
  std::string shield_net;
  std::string shape;
};

struct DesignNetPolygon
{
  TechRoutingLayerId layer;
  std::vector<Point> points;
  DesignWireStatus route_status = DesignWireStatus::kRouted;
  uint32_t flags = 0;
  uint32_t mask = 0;
  std::string shield_net;
  std::string shape;
};

struct DesignNetVia
{
  TechViaMasterId tech_via;
  DesignViaId design_via;
  std::vector<Point> origins;
  DesignOrientation orientation = DesignOrientation::kN;
  DesignWireStatus route_status = DesignWireStatus::kRouted;
  uint32_t flags = 0;
  uint32_t top_mask = 0;
  uint32_t cut_mask = 0;
  uint32_t bottom_mask = 0;
  std::string shield_net;
  std::string shape;
};

// Optional EnTT component attached directly to a SPECIALNET DesignNet entity.
// These are standalone RECT/POLYGON/VIA clauses, not wire-path primitives.
struct DesignNetGeometry
{
  std::vector<DesignNetRectangle> rectangles;
  std::vector<DesignNetPolygon> polygons;
  std::vector<DesignNetVia> vias;
};

}  // namespace eccdb
