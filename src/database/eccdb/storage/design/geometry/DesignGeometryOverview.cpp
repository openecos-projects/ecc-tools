#include "design/geometry/DesignGeometryOverview.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

#include "design/DesignStore.h"

namespace eccdb {

DesignGeometryOverview::DesignGeometryOverview(const DesignStore& design, int64_t base_tile_size) noexcept
    : _design(design), _base_tile_size(base_tile_size > 0 ? base_tile_size : 4096)
{
}

void DesignGeometryOverview::setBaseTileSize(int64_t value)
{
  if (value <= 0) throw std::invalid_argument("geometry overview tile size must be positive");
  if (_base_tile_size != value) {
    _base_tile_size = value;
    invalidate();
  }
}

void DesignGeometryOverview::invalidate() noexcept
{
  _tiles.clear();
  _cached_epoch = 0;
}

std::size_t DesignGeometryOverview::TileKeyHash::operator()(TileKey key) const noexcept
{
  // SplitMix-style mixing keeps adjacent world tiles from clustering in the
  // unordered map while remaining cheap in the renderer's hot path.
  uint64_t value = static_cast<uint64_t>(key.x);
  value ^= static_cast<uint64_t>(key.y) + 0x9e3779b97f4a7c15ULL + (value << 6u) + (value >> 2u);
  value ^= static_cast<uint64_t>(key.level) + 0x517cc1b727220a95ULL + (value << 6u) + (value >> 2u);
  value ^= value >> 30u;
  value *= 0xbf58476d1ce4e5b9ULL;
  value ^= value >> 27u;
  value *= 0x94d049bb133111ebULL;
  value ^= value >> 31u;
  return static_cast<std::size_t>(value);
}

int64_t DesignGeometryOverview::floorDivide(int64_t value, int64_t divisor) noexcept
{
  const auto quotient = value / divisor;
  const auto remainder = value % divisor;
  return remainder < 0 ? quotient - 1 : quotient;
}

int64_t DesignGeometryOverview::tileSize(uint8_t level) const noexcept
{
  constexpr auto max_value = std::numeric_limits<int64_t>::max();
  auto value = _base_tile_size;
  for (uint8_t index = 0; index < level && value <= max_value / 2; ++index) value *= 2;
  return value;
}

int32_t DesignGeometryOverview::clampCoordinate(int64_t value) noexcept
{
  return static_cast<int32_t>(std::clamp(value, static_cast<int64_t>(std::numeric_limits<int32_t>::min()),
                                         static_cast<int64_t>(std::numeric_limits<int32_t>::max())));
}

Rect DesignGeometryOverview::tileBounds(TileKey key) const noexcept
{
  const auto size = tileSize(key.level);
  const auto lower_x = key.x * size;
  const auto lower_y = key.y * size;
  return Rect{clampCoordinate(lower_x), clampCoordinate(lower_y), clampCoordinate(lower_x + size),
              clampCoordinate(lower_y + size)};
}

void DesignGeometryOverview::ensureEpoch() const
{
  const auto epoch = _design.geometryStorage().geometryEpoch();
  if (_cached_epoch != epoch) {
    // Keep the sparse key set. Individual tiles are rebuilt only when a GUI
    // view asks for them again, which avoids rebuilding an unrelated viewport
    // after a small edit. `invalidate()` remains available to bound the cache
    // at a transaction/frame boundary.
    _cached_epoch = epoch;
  }
}

void DesignGeometryOverview::addLayer(Tile& tile, uint64_t layer, Rect shape_bounds) noexcept
{
  for (auto& summary : tile.layers) {
    if (summary.layer == layer) {
      ++summary.shape_count;
      summary.bounds = summary.shape_count == 1 ? shape_bounds : summary.bounds.united(shape_bounds);
      return;
    }
  }
  tile.layers.push_back(LayerSummary{.layer = layer, .shape_count = 1, .bounds = shape_bounds});
}

void DesignGeometryOverview::markOccupancy(std::array<uint64_t, 4>& occupancy, Rect tile_bounds,
                                           Rect shape_bounds, bool thin) noexcept
{
  if (!shape_bounds.isValid() || !tile_bounds.isValid() || !shape_bounds.intersects(tile_bounds)) return;
  const auto clipped = Rect{std::max(shape_bounds.ll_x, tile_bounds.ll_x),
                            std::max(shape_bounds.ll_y, tile_bounds.ll_y),
                            std::min(shape_bounds.ur_x, tile_bounds.ur_x),
                            std::min(shape_bounds.ur_y, tile_bounds.ur_y)};
  const auto width = std::max<int64_t>(1, tile_bounds.width());
  const auto height = std::max<int64_t>(1, tile_bounds.height());
  const auto column = [&](int64_t coordinate) {
    return static_cast<uint32_t>(std::clamp(((coordinate - tile_bounds.ll_x) * 16) / width, int64_t{0}, int64_t{15}));
  };
  const auto row = [&](int64_t coordinate) {
    return static_cast<uint32_t>(std::clamp(((coordinate - tile_bounds.ll_y) * 16) / height, int64_t{0}, int64_t{15}));
  };
  const auto first_column = column(clipped.ll_x);
  const auto last_column = column(clipped.ur_x);
  const auto first_row = row(clipped.ll_y);
  const auto last_row = row(clipped.ur_y);
  // A routed segment is a swept line, not its whole BBox. Keep its overview
  // coverage thin when it spans many cells; otherwise one long wire fills a
  // solid rectangle and quickly destroys the thumbnail's spatial signal.
  if (thin && (last_column - first_column > 2u || last_row - first_row > 2u)) {
    const auto center_column = column((clipped.ll_x + clipped.ur_x) / 2);
    const auto center_row = row((clipped.ll_y + clipped.ur_y) / 2);
    if (last_column - first_column >= last_row - first_row) {
      for (uint32_t x = first_column; x <= last_column; ++x) {
        const auto bit = center_row * 16u + x;
        occupancy[bit / 64u] |= uint64_t{1} << (bit % 64u);
      }
    } else {
      for (uint32_t y = first_row; y <= last_row; ++y) {
        const auto bit = y * 16u + center_column;
        occupancy[bit / 64u] |= uint64_t{1} << (bit % 64u);
      }
    }
    return;
  }
  for (uint32_t y = first_row; y <= last_row; ++y) {
    for (uint32_t x = first_column; x <= last_column; ++x) {
      const auto bit = y * 16u + x;
      occupancy[bit / 64u] |= uint64_t{1} << (bit % 64u);
    }
  }
}

void DesignGeometryOverview::markOccupancy(Tile& tile, Rect shape_bounds, bool thin) noexcept
{
  markOccupancy(tile.occupancy, tile.bounds, shape_bounds, thin);
}

void DesignGeometryOverview::applyChange(Rect old_bounds, std::optional<Rect> new_bounds,
                                         uint64_t previous_epoch) const
{
  if (_cached_epoch != previous_epoch) return;
  _cached_epoch = _design.geometryStorage().geometryEpoch();
  for (auto iterator = _tiles.begin(); iterator != _tiles.end();) {
    if (iterator->second.bounds.intersects(old_bounds)
        || (new_bounds && iterator->second.bounds.intersects(*new_bounds))) {
      iterator = _tiles.erase(iterator);
    } else {
      iterator->second.geometry_epoch = _cached_epoch;
      ++iterator;
    }
  }
}

bool DesignGeometryOverview::seed(std::span<const Tile> tiles, uint64_t epoch) const
{
  if (epoch == 0 || epoch != _design.geometryStorage().geometryEpoch()) return false;
  if (tiles.empty()) {
    // An empty sidecar is still a valid snapshot of the current epoch. Do not
    // disturb a cache that may already contain incrementally maintained tiles.
    if (_cached_epoch == 0) _cached_epoch = epoch;
    return _cached_epoch == epoch;
  }
  for (const auto& value : tiles) {
    if (value.geometry_epoch != epoch || value.key.level > 63u) return false;
    if (value.bounds != tileBounds(value.key)) return false;
  }
  if (_cached_epoch != 0 && _cached_epoch != epoch) {
    _tiles.clear();
  }
  _cached_epoch = epoch;
  if (_tiles.size() + tiles.size() > 4096u) _tiles.clear();
  for (const auto& value : tiles) _tiles.insert_or_assign(value.key, value);
  return true;
}

DesignGeometryOverview::Tile DesignGeometryOverview::buildTile(TileKey key) const
{
  Tile result{.key = key, .bounds = tileBounds(key), .geometry_epoch = _cached_epoch};
  // The spatial index already stores the exact BBox used for every entry.
  // Consume those records directly for a summary tile; resolving every typed
  // Shape and recomputing bounds here made a broad zoom rebuild walk hundreds
  // of thousands of EnTT payloads on the GUI's native worker.
  const auto records = _design.geometryIndex().queryRecords(result.bounds);
  const auto& geometry = _design.geometryStorage();
  result.layers.reserve(4);
  bool has_occupied_bounds = false;
  const auto append = [&](uint64_t layer, Rect shape_bounds, bool thin, bool& source_counted) {
    if (!shape_bounds.isValid() || !shape_bounds.intersects(result.bounds)) {
      return;
    }
    const auto clipped = Rect{std::max(shape_bounds.ll_x, result.bounds.ll_x),
                              std::max(shape_bounds.ll_y, result.bounds.ll_y),
                              std::min(shape_bounds.ur_x, result.bounds.ur_x),
                              std::min(shape_bounds.ur_y, result.bounds.ur_y)};
    if (!source_counted) {
      ++result.shape_count;
      source_counted = true;
    }
    result.covered_area += static_cast<uint64_t>(std::max<int64_t>(0, clipped.area()));
    result.occupied = has_occupied_bounds ? result.occupied.united(clipped) : clipped;
    has_occupied_bounds = true;
    addLayer(result, layer, clipped);
    markOccupancy(result, clipped, thin);
    // Keep a second, tiny mask per layer. This is still only derived cache
    // metadata, but it prevents a visible layer from inheriting occupancy
    // cells contributed by a different routing layer.
    for (auto& summary : result.layers) {
      if (summary.layer == layer) {
        markOccupancy(summary.occupancy, result.bounds, clipped, thin);
        break;
      }
    }
  };

  bool via_source_counted = false;
  const DesignGeometryStorage::ViaLayerBoundsVisitor append_via =
      [&append, &via_source_counted](TechLayerId layer, Rect bounds) {
        append(layer.packed(), bounds, false, via_source_counted);
      };
  for (const auto& record : records) {
    if (!record.bounds.isValid()) {
      continue;
    }
    const bool via = record.kind == GeometryShapeKind::kVia || record.kind == GeometryShapeKind::kNetVia
                     || record.kind == GeometryShapeKind::kPinVia;
    if (via) {
      // Indexed VIA records retain their broad union BBox for candidate
      // lookup only. Overview aggregation resolves the placed physical
      // bottom/cut/top pieces, so an unlayered record never paints all VIA
      // metal into its route-entry layer.
      via_source_counted = false;
      static_cast<void>(geometry.forEachViaLayerBounds(record.id, append_via));
      continue;
    }
    bool source_counted = false;
    const auto thin = record.kind == GeometryShapeKind::kSegment || record.kind == GeometryShapeKind::kPatch;
    append(record.layer, record.bounds, thin, source_counted);
  }
  return result;
}

DesignGeometryOverview::Tile DesignGeometryOverview::tile(TileKey key) const
{
  ensureEpoch();
  if (const auto iterator = _tiles.find(key); iterator != _tiles.end()
      && iterator->second.geometry_epoch == _cached_epoch) {
    return iterator->second;
  }
  // Keep this derived cache bounded during long pan sessions.
  if (_tiles.size() >= 4096) _tiles.clear();
  auto [iterator, inserted] = _tiles.insert_or_assign(key, buildTile(key));
  static_cast<void>(inserted);
  return iterator->second;
}

std::vector<DesignGeometryOverview::Tile> DesignGeometryOverview::tiles(Rect viewport, uint8_t level) const
{
  if (!viewport.isValid()) return {};
  ensureEpoch();
  const auto size = tileSize(level);
  const auto first_x = floorDivide(viewport.ll_x, size);
  const auto last_x = floorDivide(viewport.ur_x, size);
  const auto first_y = floorDivide(viewport.ll_y, size);
  const auto last_y = floorDivide(viewport.ur_y, size);
  const auto count_x = static_cast<uint64_t>(last_x - first_x + 1);
  const auto count_y = static_cast<uint64_t>(last_y - first_y + 1);
  const auto count = count_x * count_y;
  if (count > 1'000'000ULL) throw std::invalid_argument("geometry overview viewport contains too many tiles");
  std::vector<Tile> result;
  result.reserve(static_cast<std::size_t>(count));
  for (auto y = first_y; y <= last_y; ++y) {
    for (auto x = first_x; x <= last_x; ++x) result.push_back(tile(TileKey{level, x, y}));
  }
  return result;
}

std::vector<DesignGeometryOverview::ShapeId> DesignGeometryOverview::exactShapes(Rect viewport) const
{
  ensureEpoch();
  return _design.geometryIndex().query(viewport);
}

}  // namespace eccdb
