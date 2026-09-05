#pragma once

#include <string>
#include <vector>

#include "common/GeometryTypes.h"
#include "design/common/DesignTypes.h"
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

struct DesignWireRoutingHandle
{
  uint32_t path_begin = 0;
  uint32_t path_count = 0;
};

// DesignWireId-backed EnTT component. Parser staging and pool ownership are
// deliberately kept out of the entity component.
struct DesignWire
{
  DesignNetId net;
  DesignWireStatus status = DesignWireStatus::kRouted;
  std::string shield_net;
  DesignWireRoutingHandle routing;
};

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
