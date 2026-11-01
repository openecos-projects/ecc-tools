// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "common/GeometryTypes.h"
#include "design/common/DesignTypes.h"
#include "design/routing/component/RoutingComponents.h"
#include "design/routing/view/RoutingViewTypes.h"
#include "geometry/GeometryEntity.h"
#include "tech/common/TechLayerIds.h"
#include "tech/via_master/model/ViaMasterComponents.h"

namespace eccdb {

[[nodiscard]] inline GeometryOwner designWireOwner(DesignWireId id) noexcept
{
  return GeometryOwner::from(GeometryOwnerDomain::kDesignWire, id);
}

[[nodiscard]] inline GeometryOwner designNetOwner(DesignNetId id) noexcept
{
  return GeometryOwner::from(GeometryOwnerDomain::kDesignNet, id);
}

// SPECIALNET payloads deliberately have no owning vector of siblings. A
// geometry entity carries exactly one typed component; owner and source
// metadata live in the common GeometryShape component. Routed geometry keeps
// its lossless DesignRouteShape variant in RoutingComponents.h.
using DesignGeometryRectangle = DesignNetRectangle;
using DesignGeometryPolygon = DesignNetPolygon;
using DesignGeometryNetVia = DesignNetVia;

// Explicit SPECIALNET spellings make call sites readable without creating a
// second payload type or a second storage domain.
using DesignGeometrySpecialRectangle = DesignGeometryRectangle;
using DesignGeometrySpecialPolygon = DesignGeometryPolygon;
using DesignGeometrySpecialVia = DesignGeometryNetVia;

// Route attributes are optional EnTT components, so ordinary segments do not
// pay for DEF-only strings (taper rule/shape/style) unless those fields exist.
using DesignGeometryRouteAttributes = DesignWirePathExtra;

template <>
struct GeometryPayloadTraits<DesignGeometryRectangle>
{
  static constexpr GeometryShapeKind kind = GeometryShapeKind::kRectangle;
};

template <>
struct GeometryPayloadTraits<DesignGeometryPolygon>
{
  static constexpr GeometryShapeKind kind = GeometryShapeKind::kPolygon;
};

template <>
struct GeometryPayloadTraits<DesignGeometryNetVia>
{
  static constexpr GeometryShapeKind kind = GeometryShapeKind::kNetVia;
};

}  // namespace eccdb
