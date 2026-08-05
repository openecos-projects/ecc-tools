#include "GeometrySpatialIndex.h"

#include <algorithm>

namespace ecc::geometry {
namespace {

int32_t floor_div(int32_t value, int32_t divisor)
{
  int32_t quotient = value / divisor;
  const int32_t remainder = value % divisor;
  if (remainder != 0 && ((remainder < 0) != (divisor < 0))) {
    --quotient;
  }
  return quotient;
}

}  // namespace

size_t SpatialTileKeyHash::operator()(const SpatialTileKey& key) const
{
  const size_t layer_hash = std::hash<LayerId>{}(key.layer_id);
  const size_t x_hash = std::hash<int32_t>{}(key.tile_x);
  const size_t y_hash = std::hash<int32_t>{}(key.tile_y);
  size_t seed = layer_hash;
  seed ^= x_hash + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
  seed ^= y_hash + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
  return seed;
}

GeometrySpatialIndex::GeometrySpatialIndex(GeometrySpatialIndexOptions options) : _options(options)
{
  if (_options.tile_size <= 0) {
    _options.tile_size = 4096;
  }
  if (_options.max_tiles_per_shape == 0) {
    _options.max_tiles_per_shape = 1;
  }
}

void GeometrySpatialIndex::clear()
{
  _tiles.clear();
  _large_by_layer.clear();
}

void GeometrySpatialIndex::insert(const ShapeRecord& record)
{
  if (record.id == 0 || record.state != ShapeState::kAlive) {
    return;
  }

  const TileRange range = tile_range(record.bbox);
  if (should_use_large_bucket(range)) {
    _large_by_layer[record.layer_id].push_back(record.id);
    return;
  }

  for (int32_t x = range.min_x; x <= range.max_x; ++x) {
    for (int32_t y = range.min_y; y <= range.max_y; ++y) {
      _tiles[SpatialTileKey{record.layer_id, x, y}].push_back(record.id);
    }
  }
}

void GeometrySpatialIndex::remove(const ShapeRecord& record)
{
  if (record.id == 0) {
    return;
  }

  const TileRange range = tile_range(record.bbox);
  if (should_use_large_bucket(range)) {
    auto iter = _large_by_layer.find(record.layer_id);
    if (iter != _large_by_layer.end()) {
      remove_id(iter->second, record.id);
    }
    return;
  }

  for (int32_t x = range.min_x; x <= range.max_x; ++x) {
    for (int32_t y = range.min_y; y <= range.max_y; ++y) {
      auto iter = _tiles.find(SpatialTileKey{record.layer_id, x, y});
      if (iter != _tiles.end()) {
        remove_id(iter->second, record.id);
      }
    }
  }
}

void GeometrySpatialIndex::update(const ShapeRecord& old_record, const ShapeRecord& new_record)
{
  remove(old_record);
  insert(new_record);
}

std::vector<ShapeId> GeometrySpatialIndex::query(LayerId layer_id, Rect32 bbox) const
{
  std::vector<ShapeId> result;
  const TileRange range = tile_range(bbox);

  if (const auto large_iter = _large_by_layer.find(layer_id); large_iter != _large_by_layer.end()) {
    result.insert(result.end(), large_iter->second.begin(), large_iter->second.end());
  }

  for (int32_t x = range.min_x; x <= range.max_x; ++x) {
    for (int32_t y = range.min_y; y <= range.max_y; ++y) {
      const auto iter = _tiles.find(SpatialTileKey{layer_id, x, y});
      if (iter != _tiles.end()) {
        result.insert(result.end(), iter->second.begin(), iter->second.end());
      }
    }
  }

  std::sort(result.begin(), result.end());
  result.erase(std::unique(result.begin(), result.end()), result.end());
  return result;
}

GeometrySpatialIndex::TileRange GeometrySpatialIndex::tile_range(Rect32 bbox) const
{
  bbox = normalize(bbox);
  return TileRange{floor_div(bbox.lx, _options.tile_size), floor_div(bbox.ly, _options.tile_size),
                   floor_div(bbox.hx, _options.tile_size), floor_div(bbox.hy, _options.tile_size)};
}

bool GeometrySpatialIndex::should_use_large_bucket(TileRange range) const
{
  const uint64_t x_count = static_cast<uint64_t>(range.max_x - range.min_x + 1);
  const uint64_t y_count = static_cast<uint64_t>(range.max_y - range.min_y + 1);
  return x_count * y_count > _options.max_tiles_per_shape;
}

void GeometrySpatialIndex::remove_id(std::vector<ShapeId>& values, ShapeId id) const
{
  values.erase(std::remove(values.begin(), values.end(), id), values.end());
}

}  // namespace ecc::geometry
