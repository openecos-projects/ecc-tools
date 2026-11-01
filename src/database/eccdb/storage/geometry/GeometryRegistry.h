// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#pragma once

#include <concepts>
#include <entt/entt.hpp>
#include <type_traits>
#include <utility>
#include <span>

#include "common/GeometryBounds.h"
#include "geometry/GeometryEntity.h"
#include "geometry/DomainGeometryComponents.h"

namespace eccdb {

// Canonical owner of editable geometry entities.  This class intentionally
// contains only EnTT registry operations; ordering belongs to owner
// components (DesignWire/DesignNet), and spatial indexes remain derived data.
class GeometryRegistry
{
 public:
  using entity_type = GeometryEntity;
  using registry_type = entt::basic_registry<entity_type>;

  [[nodiscard]] registry_type& registry() noexcept { return _registry; }
  [[nodiscard]] const registry_type& registry() const noexcept { return _registry; }

  template <typename Payload>
    requires requires { GeometryPayloadTraits<Payload>::kind; }
  [[nodiscard]] GeometryShapeId create(GeometryOwner owner, GeometryShape metadata, Payload payload)
  {
    metadata.owner = owner;
    metadata.kind = GeometryPayloadTraits<Payload>::kind;
    const auto entity = _registry.create();
    try {
      _registry.emplace<GeometryShape>(entity, std::move(metadata));
      _registry.emplace<Payload>(entity, std::move(payload));
      // Bounds are a derived cache. Do not allocate a component for every
      // primitive at creation time: large routed designs can contain tens of
      // millions of shapes, while most of them are never queried for an
      // exact box. ensureBounds() materializes the cache on first use.
      return GeometryShapeId{entity};
    } catch (...) {
      if (_registry.valid(entity)) {
        _registry.destroy(entity);
      }
      throw;
    }
  }

  template <typename Payload>
    requires requires { GeometryPayloadTraits<Payload>::kind; }
  [[nodiscard]] GeometryShapeId create(GeometryOwner owner, Payload payload)
  {
    return create<Payload>(owner, GeometryShape{}, std::move(payload));
  }

  [[nodiscard]] bool contains(GeometryShapeId id) const noexcept
  {
    return _registry.valid(id.entity()) && _registry.all_of<GeometryShape>(id.entity());
  }

  [[nodiscard]] bool destroy(GeometryShapeId id) noexcept
  {
    if (!contains(id)) {
      return false;
    }
    _registry.destroy(id.entity());
    return true;
  }

  void destroy(std::span<const GeometryShapeId> ids) noexcept
  {
    for (const auto id : ids) static_cast<void>(destroy(id));
  }

  [[nodiscard]] GeometryShapeId createRectangle(GeometryOwner owner, Rect rectangle, GeometryShape metadata = {})
  {
    return create(owner, metadata, GeometryRectangle{rectangle});
  }
  [[nodiscard]] GeometryShapeId createPolygon(GeometryOwner owner, std::vector<Point> points, GeometryShape metadata = {})
  {
    return create(owner, metadata, GeometryPolygon{std::move(points)});
  }
  [[nodiscard]] GeometryShapeId createBoundary(GeometryOwner owner, std::vector<Point> points)
  {
    return create(owner, GeometryBoundary{std::move(points)});
  }

  [[nodiscard]] GeometryShape& shape(GeometryShapeId id) noexcept
  {
    return _registry.get<GeometryShape>(id.entity());
  }

  [[nodiscard]] const GeometryShape& shape(GeometryShapeId id) const noexcept
  {
    return _registry.get<const GeometryShape>(id.entity());
  }

  // Bounds are derived runtime data.  Keep this accessor tolerant of objects
  // restored from an older archive that predates the component; the canonical
  // shape identity remains unchanged and the cache simply starts dirty.
  [[nodiscard]] GeometryBounds& ensureBounds(GeometryShapeId id)
  {
    return _registry.get_or_emplace<GeometryBounds>(id.entity());
  }

  // Const query services may refresh this derived cache without changing the
  // canonical entity or payload.  This is intentionally the only mutable
  // operation exposed through a const registry facade.
  [[nodiscard]] GeometryBounds& ensureBounds(GeometryShapeId id) const
  {
    return const_cast<GeometryRegistry*>(this)->ensureBounds(id);
  }

  [[nodiscard]] const GeometryBounds* tryBounds(GeometryShapeId id) const noexcept
  {
    return _registry.try_get<const GeometryBounds>(id.entity());
  }

  template <typename Payload>
  [[nodiscard]] Payload& get(GeometryShapeId id) noexcept
  {
    return _registry.get<Payload>(id.entity());
  }

  template <typename Payload>
  [[nodiscard]] const Payload& get(GeometryShapeId id) const noexcept
  {
    return _registry.get<const Payload>(id.entity());
  }

  template <typename Payload>
  [[nodiscard]] Payload* tryGet(GeometryShapeId id) noexcept
  {
    return _registry.try_get<Payload>(id.entity());
  }

  template <typename Payload>
  [[nodiscard]] const Payload* tryGet(GeometryShapeId id) const noexcept
  {
    return _registry.try_get<const Payload>(id.entity());
  }

  template <typename Payload>
  [[nodiscard]] bool has(GeometryShapeId id) const noexcept
  {
    return _registry.valid(id.entity()) && _registry.all_of<Payload>(id.entity());
  }

  template <typename Payload>
  void replace(GeometryShapeId id, Payload payload)
  {
    _registry.replace<Payload>(id.entity(), std::move(payload));
    // A payload replacement preserves the entity and its owner, but any
    // previously computed bounds are no longer trustworthy.
    ensureBounds(id).invalidate();
  }

 private:
  registry_type _registry;
};

}  // namespace eccdb
