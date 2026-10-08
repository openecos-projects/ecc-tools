// ***************************************************************************************
// SPDX-License-Identifier: MulanPSL-2.0
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
#include <limits>

#include "common/EnttId.h"

namespace eccdb {

// Forward declarations keep the geometry registry independent from the
// DesignRegistry schema.  Domain headers may provide strongly typed helpers
// for these values without creating an include cycle.
enum class TechEntity : uint32_t;
struct TechRoutingLayer;
using TechRoutingLayerId = EnttId<TechEntity, TechRoutingLayer>;

// Geometry has its own EnTT registry.  It deliberately does not share the
// DesignRegistry entity space: a geometry handle is a first-class design
// object and remains independent from a Wire or a Net entity.
//
// Geometry uses EnTT's 64-bit entity layout: 32 bits for the sparse-set slot
// and 32 bits for the generation.  A full-chip design can contain many
// millions of simultaneously live shapes, so the 32-bit EnTT layout (which
// has only a 20-bit slot index) is not sufficient here.  This remains the
// plain EnTT entity type: there is no pool façade or application-owned
// ordinal encoded into the handle.
enum class GeometryEntity : uint64_t
{
};

struct GeometryShape;
using GeometryShapeId = EnttId<GeometryEntity, GeometryShape>;
using GeometryId = GeometryShapeId;

enum class GeometryShapeKind : uint8_t
{
  kInvalid,
  kSegment,
  kVia,
  kPatch,
  kPoint,
  kRectangle,
  kPolygon,
  kNetVia,
  kPinVia,
  kBoundary,
  kGuide,
  kTrackAssignment,
  kLibraryVia,

  // Transitional spellings for callers being migrated from ShapeHandle. They
  // are aliases, not additional storage kinds.
  kRouteSegment = kSegment,
  kRouteVia = kVia,
  kRoutePatch = kPatch,
  kRoutePoint = kPoint,
  kNetRectangle = kRectangle,
  kNetPolygon = kPolygon,
};

enum class GeometryOwnerDomain : uint8_t
{
  kNone,
  kDesignWire,
  kDesignNet,
  kLibraryMaster,
  kTechnologyVia,
  kDesignBlockPin,
  kDesignVia,
  kDesignInstance,
  kDesignRegion,
  kDesignBlockage,
  kDesignFill,
  kDesignDieArea,
  kDesignCoreArea,
  kDesignGuide,
  kDesignTrackAssignment,
};

// The owner is a relation, not a second copy of a geometry payload.  The
// packed value is the entity value from the owning registry; domain keeps
// the otherwise opaque value type-safe at the API boundary. Library and
// technology stores use this same relation schema in independent registries.
struct GeometryOwner
{
  GeometryOwnerDomain domain = GeometryOwnerDomain::kNone;
  uint64_t value = std::numeric_limits<uint64_t>::max();

  friend bool operator==(GeometryOwner, GeometryOwner) = default;

  [[nodiscard]] bool valid() const noexcept { return domain != GeometryOwnerDomain::kNone; }

  template <typename Id>
  [[nodiscard]] static GeometryOwner from(GeometryOwnerDomain owner_domain, Id id) noexcept
  {
    return {owner_domain, static_cast<uint64_t>(id.packed())};
  }
};

// Common fields stay small and cold metadata is kept out of the typed
// geometry components.  A route layer is empty for layer-independent objects
// (for example a SPECIALNET via).
struct GeometryShape
{
  GeometryOwner owner;
  GeometryShapeKind kind = GeometryShapeKind::kInvalid;
  TechRoutingLayerId layer;
  uint32_t flags = 0;
  uint32_t source_path = std::numeric_limits<uint32_t>::max();
};

// Payload-to-kind mapping is specialized by each geometry domain.  Keeping
// the trait in the common header lets GeometryRegistry stay a small generic
// EnTT wrapper while design/library/technology domains provide their own typed
// payloads.
template <typename Payload>
struct GeometryPayloadTraits;

}  // namespace eccdb
