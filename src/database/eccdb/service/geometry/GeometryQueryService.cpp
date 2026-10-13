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

#include "geometry/GeometryQueryService.h"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <utility>

#include "context/DatabaseContext.h"
#include "design/DesignStore.h"
#include "design/geometry/DesignGeometryOverview.h"
#include "tech/TechStore.h"
#include "geometry/GeometryHitTestService.h"
#include "geometry/GeometryPayloadService.h"

namespace eccdb {

namespace {

GeometryShapeId toShapeId(uint64_t value)
{
  return GeometryShapeId{static_cast<GeometryEntity>(value)};
}

[[nodiscard]] bool isViaKind(GeometryShapeKind kind) noexcept
{
  return kind == GeometryShapeKind::kVia || kind == GeometryShapeKind::kNetVia
      || kind == GeometryShapeKind::kPinVia;
}

[[nodiscard]] bool hasLayerFilter(const GeometryQueryFilter& filter) noexcept
{
  return filter.layer != GeometryQueryFilter::kUnspecified || !filter.layers.empty();
}

[[nodiscard]] bool viaTouchesFilteredLayer(const GeometryPayloadBatch& batch,
                                            const GeometryQueryFilter& filter) noexcept
{
  const auto matches = [&filter](const auto& primitive) { return filter.acceptsLayer(primitive.layer); };
  return std::any_of(batch.rectangles.begin(), batch.rectangles.end(), matches)
      || std::any_of(batch.polygons.begin(), batch.polygons.end(), matches);
}

[[nodiscard]] bool matchesQueryRef(const DesignGeometryStorage& geometry, DatabaseContext& context,
                                    GeometryShapeId id, const GeometryQueryFilter& filter)
{
  if (!geometry.contains(id)) return false;
  if (!hasLayerFilter(filter) || !isViaKind(geometry.shape(id).metadata->kind)) return true;
  const std::array<uint64_t, 1> via_id{id.packed()};
  return viaTouchesFilteredLayer(GeometryPayloadService{context}.fetchShapes(via_id), filter);
}

GeometryShapeRecord recordFor(const DesignStore& design, GeometryShapeId id)
{
  const auto& geometry = design.geometryStorage();
  const auto view = geometry.shape(id);
  // Query records are detached/read-only results. Computing the box without
  // populating the per-shape cache keeps a large GUI query from allocating a
  // Bounds component for every returned shape.
  const auto bounds = geometry.boundsUncached(id);
  const auto& metadata = *view.metadata;
  GeometryShapeRecord result{
      .id = id.packed(),
      .kind = static_cast<uint8_t>(metadata.kind),
      .owner_domain = static_cast<uint8_t>(metadata.owner.domain),
      .owner = metadata.owner.value,
      .layer = geometry.effectiveLayer(id).packed(),
      .ll_x = bounds.ll_x,
      .ll_y = bounds.ll_y,
      .ur_x = bounds.ur_x,
      .ur_y = bounds.ur_y,
      .flags = metadata.flags,
      .source_path = metadata.source_path,
  };
  const auto entity = static_cast<DesignEntity>(metadata.owner.value);
  switch (metadata.owner.domain) {
    case GeometryOwnerDomain::kDesignWire:
      result.wire = metadata.owner.value;
      result.net = design.routingStorage().wire(DesignWireId{entity}).net.packed();
      break;
    case GeometryOwnerDomain::kDesignNet:
    case GeometryOwnerDomain::kDesignGuide:
    case GeometryOwnerDomain::kDesignTrackAssignment:
      result.net = metadata.owner.value;
      break;
    case GeometryOwnerDomain::kDesignBlockPin: {
      const auto& pin = design.netlistStorage().blockPin(DesignBlockPinId{entity});
      const auto& term = design.netlistStorage().blockTerm(pin.term);
      const auto net = term.net ? term.net : term.special_net;
      if (net) result.net = net.packed();
      break;
    }
    default: break;
  }
  return result;
}

GeometryShapeRef refFor(const GeometryShapeRecord& record)
{
  return GeometryShapeRef{
      .id = record.id,
      .kind = record.kind,
      .owner_domain = record.owner_domain,
      .owner = record.owner,
      .net = record.net,
      .wire = record.wire,
      .layer = record.layer,
      .bounds = Rect{record.ll_x, record.ll_y, record.ur_x, record.ur_y},
      .flags = record.flags,
      .source_path = record.source_path,
  };
}

}  // namespace

GeometryShapeBatch GeometryQueryService::query(Rect box) const
{
  if (!_context) throw std::logic_error("GeometryQueryService has no database");
  const auto& design = _context->design();
  const auto& geometry = design.geometryStorage();
  GeometryShapeBatch result;
  result.geometry_epoch = geometry.geometryEpoch();
  const auto ids = design.geometryIndex().query(box.normalized());
  result.records.reserve(ids.size());
  for (const auto id : ids) {
    if (geometry.contains(id)) result.records.push_back(recordFor(design, id));
  }
  return result;
}

GeometryShapeRefBatch GeometryQueryService::queryRefs(Rect box, const GeometryQueryFilter& filter) const
{
  if (!_context) throw std::logic_error("GeometryQueryService has no database");
  const auto& design = _context->design();
  const auto& geometry = design.geometryStorage();
  GeometryShapeRefBatch result;
  result.geometry_epoch = geometry.geometryEpoch();
  const auto ids = design.geometryIndex().query(box.normalized(), filter);
  result.records.reserve(ids.size());
  for (const auto id : ids) {
    if (!matchesQueryRef(geometry, *_context, id, filter)) continue;
    result.records.push_back(refFor(recordFor(design, id)));
  }
  return result;
}

bool GeometryQueryService::hasShapes(Rect box, const GeometryQueryFilter& filter) const
{
  if (!_context) throw std::logic_error("GeometryQueryService has no database");
  const auto& design = _context->design();
  const auto& geometry = design.geometryStorage();
  return design.geometryIndex().any(box.normalized(), filter, [&](GeometryShapeId id) {
    return matchesQueryRef(geometry, *_context, id, filter);
  });
}

std::optional<Rect> GeometryQueryService::designBounds() const
{
  if (!_context) return std::nullopt;
  const auto& design = _context->design();
  if (design.globalStorage().hasDieArea()) {
    return design.globalStorage().dieBounds();
  }
  try {
    const auto bounds = design.floorplanStorage().coreBounds();
    if (bounds.isValid() && bounds.hasArea()) return bounds;
  } catch (const std::exception&) {
    // A partially loaded design may have neither rows nor a die. Fitting can
    // then fall back to an application-provided viewport without touching the
    // full geometry index.
  }
  return std::nullopt;
}

void GeometryQueryService::warmupIndex() const
{
  if (!indexReady()) _context->design().geometryIndex().rebuild();
}

bool GeometryQueryService::indexReady() const
{
  const auto& design = _context->design();
  return design.geometryIndex().isBuilt()
         && design.geometryIndex().indexedEpoch() == design.geometryStorage().geometryEpoch();
}

std::size_t GeometryQueryService::indexedShapeCount() const
{
  return _context->design().geometryIndex().indexedShapeCount();
}

GeometryOverviewBatch GeometryQueryService::queryOverview(Rect box, uint8_t level) const
{
  if (!_context) throw std::logic_error("GeometryQueryService has no database");
  const auto& design = _context->design();
  const auto& overview = design.geometryOverview();
  GeometryOverviewBatch result;
  result.level = level;
  result.base_tile_size = overview.baseTileSize();
  result.geometry_epoch = design.geometryStorage().geometryEpoch();
  const auto tiles = overview.tiles(box.normalized(), level);
  result.tiles.reserve(tiles.size());
  for (const auto& tile : tiles) {
    GeometryOverviewTileRecord record{
        .level = tile.key.level,
        .tile_x = tile.key.x,
        .tile_y = tile.key.y,
        .bounds = tile.bounds,
        .occupied = tile.occupied,
        .shape_count = tile.shape_count,
        .covered_area = tile.covered_area,
        .occupancy = tile.occupancy,
        .geometry_epoch = tile.geometry_epoch,
    };
    record.layers.reserve(tile.layers.size());
    for (const auto& layer : tile.layers) {
      record.layers.push_back(GeometryOverviewLayerRecord{
          .layer = layer.layer,
          .shape_count = layer.shape_count,
          .bounds = layer.bounds,
          .occupancy = layer.occupancy});
    }
    result.tiles.push_back(std::move(record));
  }
  return result;
}

bool GeometryQueryService::seedOverview(const GeometryOverviewBatch& batch) const
{
  if (!_context) return false;
  const auto& design = _context->design();
  const auto epoch = design.geometryStorage().geometryEpoch();
  if (batch.geometry_epoch == 0 || batch.geometry_epoch != epoch) return false;
  if (batch.base_tile_size != design.geometryOverview().baseTileSize()) return false;
  std::vector<DesignGeometryOverview::Tile> tiles;
  tiles.reserve(batch.tiles.size());
  for (const auto& record : batch.tiles) {
    DesignGeometryOverview::Tile tile{
        .key = {.level = record.level, .x = record.tile_x, .y = record.tile_y},
        .bounds = record.bounds,
        .occupied = record.occupied,
        .shape_count = record.shape_count,
        .covered_area = record.covered_area,
        .occupancy = record.occupancy,
        .geometry_epoch = record.geometry_epoch,
    };
    tile.layers.reserve(record.layers.size());
    for (const auto& layer : record.layers) {
      tile.layers.push_back(DesignGeometryOverview::LayerSummary{
          .layer = layer.layer,
          .shape_count = layer.shape_count,
          .bounds = layer.bounds,
          .occupancy = layer.occupancy,
      });
    }
    tiles.push_back(std::move(tile));
  }
  return design.geometryOverview().seed(tiles, epoch);
}

std::optional<uint64_t> GeometryQueryService::hitTest(Point point, int32_t tolerance,
                                                    const GeometryQueryFilter& filter) const
{
  if (tolerance < 0) throw std::invalid_argument("hit-test tolerance must be non-negative");
  const auto clamp = [](int64_t coordinate) {
    return static_cast<int32_t>(std::clamp(coordinate, int64_t{INT32_MIN}, int64_t{INT32_MAX}));
  };
  const Rect box{clamp(int64_t{point.x} - tolerance), clamp(int64_t{point.y} - tolerance),
                 clamp(int64_t{point.x} + tolerance), clamp(int64_t{point.y} + tolerance)};
  std::vector<uint64_t> layer_order;
  if (const auto* tech = _context->technology())
    for (const auto layer : tech->layerSequence()) layer_order.push_back(layer.packed());
  return hitTestGeometry(GeometryPayloadService{*_context}.queryGeometry(box, filter), point, tolerance, layer_order);
}

std::vector<uint64_t> GeometryQueryService::queryShapeIds(
    Rect box, const GeometryQueryFilter& filter) const
{
  if (!_context) throw std::logic_error("GeometryQueryService has no database");
  const auto& design = _context->design();
  const auto candidates = design.geometryIndex().query(box.normalized(), filter);
  std::vector<uint64_t> result;
  result.reserve(candidates.size());
  for (const auto id : candidates) {
    // This endpoint is deliberately the cheap R-tree broad phase used by
    // large interactive marquees.  The R-tree has already applied the
    // ordinary kind/owner/layer metadata filter.  Expanding every candidate
    // VIA through GeometryPayloadService here defeats that contract (a
    // 500k-shape marquee used to construct one temporary payload per VIA).
    // Exact layer expansion remains in queryRefs/queryGeometry; callers that
    // need exact primitive semantics should use intersectShapes instead.
    result.push_back(id.packed());
  }
  return result;
}

bool GeometryQueryService::contains(uint64_t id) const
{
  if (!_context) return false;
  return _context->design().geometryStorage().contains(toShapeId(id));
}

std::vector<uint64_t> GeometryQueryService::intersectShapes(Rect box, const GeometryQueryFilter& filter) const
{
  return intersectGeometry(GeometryPayloadService{*_context}.queryGeometry(box, filter), box);
}

uint64_t GeometryQueryService::geometryEpoch() const
{
  if (!_context) return 0;
  return _context->design().geometryStorage().geometryEpoch();
}

}  // namespace eccdb
