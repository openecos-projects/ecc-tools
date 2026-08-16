#include "GeometryTilePyramid.h"

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

Rect32 union_rect(Rect32 lhs, Rect32 rhs)
{
  lhs = normalize(lhs);
  rhs = normalize(rhs);
  return Rect32{std::min(lhs.lx, rhs.lx), std::min(lhs.ly, rhs.ly), std::max(lhs.hx, rhs.hx), std::max(lhs.hy, rhs.hy)};
}

}  // namespace

size_t GeometryTileKeyHash::operator()(const GeometryTileKey& key) const
{
  size_t seed = std::hash<uint8_t>{}(key.lod_level);
  seed ^= std::hash<LayerId>{}(key.layer_id) + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
  seed ^= std::hash<int32_t>{}(key.tile_x) + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
  seed ^= std::hash<int32_t>{}(key.tile_y) + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
  return seed;
}

GeometryTilePyramid::GeometryTilePyramid(GeometryTilePyramidOptions options) : _options(options)
{
  if (_options.base_tile_size <= 0) {
    _options.base_tile_size = 4096;
  }
  if (_options.lod_level_count == 0) {
    _options.lod_level_count = 1;
  }
  if (_options.max_tile_refs_per_shape == 0) {
    _options.max_tile_refs_per_shape = 1;
  }
}

void GeometryTilePyramid::clear()
{
  _summaries.clear();
  _large_shape_summaries.clear();
  _tile_shape_ids.clear();
  _shape_tile_keys.clear();
  _records_by_id.clear();
  _dirty_tiles.clear();
  _dirty_shape_ids.clear();
  _last_dirty_rebuild_candidate_count = 0;
}

void GeometryTilePyramid::rebuild(std::span<const ShapeRecord> records)
{
  clear();

  for (const ShapeRecord& record : records) {
    if (record.id == 0 || record.state != ShapeState::kAlive) {
      continue;
    }

    _records_by_id[record.id] = record;
    for (uint8_t lod = 0; lod < _options.lod_level_count; ++lod) {
      add_record(lod, record);
    }
  }
}

void GeometryTilePyramid::mark_dirty_tiles(LayerId layer_id, Rect32 old_bbox, Rect32 new_bbox)
{
  for (uint8_t lod = 0; lod < _options.lod_level_count; ++lod) {
    add_dirty_keys(lod, layer_id, old_bbox);
    add_dirty_keys(lod, layer_id, new_bbox);
  }
}

void GeometryTilePyramid::mark_dirty_record_insert(const ShapeRecord& record)
{
  if (record.id == 0 || record.state != ShapeState::kAlive) {
    return;
  }

  _records_by_id[record.id] = record;
  _dirty_shape_ids.insert(record.id);
  mark_dirty_tiles(record.layer_id, record.bbox, record.bbox);
}

void GeometryTilePyramid::mark_dirty_record_update(const ShapeRecord& old_record, const ShapeRecord& new_record)
{
  if (old_record.id == 0) {
    return;
  }

  remove_record_membership(old_record.id);
  if (new_record.state == ShapeState::kAlive) {
    _records_by_id[new_record.id] = new_record;
  } else {
    _records_by_id.erase(old_record.id);
  }
  _dirty_shape_ids.insert(old_record.id);
  mark_dirty_tiles(old_record.layer_id, old_record.bbox, old_record.bbox);
  if (new_record.state == ShapeState::kAlive) {
    mark_dirty_tiles(new_record.layer_id, new_record.bbox, new_record.bbox);
  }
}

void GeometryTilePyramid::mark_dirty_record_delete(const ShapeRecord& old_record)
{
  if (old_record.id == 0) {
    return;
  }

  remove_record_membership(old_record.id);
  _records_by_id.erase(old_record.id);
  _dirty_shape_ids.insert(old_record.id);
  mark_dirty_tiles(old_record.layer_id, old_record.bbox, old_record.bbox);
}

void GeometryTilePyramid::rebuild_dirty_tiles()
{
  if (_dirty_tiles.empty()) {
    _last_dirty_rebuild_candidate_count = 0;
    return;
  }

  const std::unordered_set<GeometryTileKey, GeometryTileKeyHash> dirty_tiles = _dirty_tiles;
  std::unordered_set<ShapeId> candidate_ids = _dirty_shape_ids;
  for (const GeometryTileKey& key : dirty_tiles) {
    const auto iter = _tile_shape_ids.find(key);
    if (iter != _tile_shape_ids.end()) {
      candidate_ids.insert(iter->second.begin(), iter->second.end());
    }
  }
  _last_dirty_rebuild_candidate_count = candidate_ids.size();

  for (const GeometryTileKey& key : dirty_tiles) {
    _summaries.erase(key);
    _tile_shape_ids.erase(key);
  }

  for (ShapeId shape_id : candidate_ids) {
    const auto iter = _records_by_id.find(shape_id);
    if (iter == _records_by_id.end() || iter->second.state != ShapeState::kAlive) {
      continue;
    }
    add_record_for_dirty_tiles(iter->second, dirty_tiles, _dirty_shape_ids.contains(shape_id));
  }

  _dirty_tiles.clear();
  _dirty_shape_ids.clear();
}

void GeometryTilePyramid::rebuild_dirty_tiles(std::span<const ShapeRecord> records)
{
  if (_dirty_tiles.empty()) {
    _last_dirty_rebuild_candidate_count = 0;
    return;
  }

  const std::unordered_set<GeometryTileKey, GeometryTileKeyHash> dirty_tiles = _dirty_tiles;
  for (const GeometryTileKey& key : dirty_tiles) {
    _summaries.erase(key);
    _tile_shape_ids.erase(key);
  }
  _large_shape_summaries.clear();
  _shape_tile_keys.clear();
  _records_by_id.clear();
  _last_dirty_rebuild_candidate_count = records.size();

  for (const ShapeRecord& record : records) {
    if (record.id == 0 || record.state != ShapeState::kAlive) {
      continue;
    }

    _records_by_id[record.id] = record;
    for (uint8_t lod = 0; lod < _options.lod_level_count; ++lod) {
      if (tile_span_count(lod, record.bbox) > _options.max_tile_refs_per_shape) {
        add_large_record_summary(lod, record);
        continue;
      }
      for (GeometryTileKey key : keys_for(lod, record.layer_id, record.bbox)) {
        if (dirty_tiles.contains(key)) {
          add_record_to_key(key, record);
        }
      }
    }
  }

  _dirty_tiles.clear();
  _dirty_shape_ids.clear();
}

size_t GeometryTilePyramid::dirty_tile_count() const
{
  return _dirty_tiles.size();
}

size_t GeometryTilePyramid::last_dirty_rebuild_candidate_count() const
{
  return _last_dirty_rebuild_candidate_count;
}

std::vector<GeometryTileSummary> GeometryTilePyramid::summaries() const
{
  std::vector<GeometryTileSummary> result;
  result.reserve(_summaries.size() + _large_shape_summaries.size());
  for (const auto& [key, summary] : _summaries) {
    result.push_back(summary);
  }
  for (const LargeShapeSummary& large_summary : _large_shape_summaries) {
    result.push_back(large_summary.summary);
  }
  std::sort(result.begin(), result.end(), [](const GeometryTileSummary& lhs, const GeometryTileSummary& rhs) {
    if (lhs.lod_level != rhs.lod_level) {
      return lhs.lod_level < rhs.lod_level;
    }
    if (lhs.layer_id != rhs.layer_id) {
      return lhs.layer_id < rhs.layer_id;
    }
    if (lhs.tile_x != rhs.tile_x) {
      return lhs.tile_x < rhs.tile_x;
    }
    return lhs.tile_y < rhs.tile_y;
  });
  return result;
}

std::vector<GeometryTileSummary> GeometryTilePyramid::query(uint8_t lod_level, LayerId layer_id, Rect32 viewport) const
{
  std::vector<GeometryTileSummary> result;
  if (lod_level >= _options.lod_level_count) {
    return result;
  }

  viewport = normalize(viewport);
  const int32_t size = tile_size(lod_level);
  const int32_t min_x = floor_div(viewport.lx, size);
  const int32_t min_y = floor_div(viewport.ly, size);
  const int32_t max_x = floor_div(viewport.hx, size);
  const int32_t max_y = floor_div(viewport.hy, size);

  for (int32_t x = min_x; x <= max_x; ++x) {
    for (int32_t y = min_y; y <= max_y; ++y) {
      const auto iter = _summaries.find(GeometryTileKey{lod_level, layer_id, x, y});
      if (iter != _summaries.end()) {
        result.push_back(iter->second);
      }
    }
  }
  for (const LargeShapeSummary& large_summary : _large_shape_summaries) {
    const GeometryTileSummary& summary = large_summary.summary;
    if (summary.lod_level == lod_level && summary.layer_id == layer_id && intersects(summary.bbox, viewport)) {
      result.push_back(summary);
    }
  }

  std::sort(result.begin(), result.end(), [](const GeometryTileSummary& lhs, const GeometryTileSummary& rhs) {
    if (lhs.tile_x != rhs.tile_x) {
      return lhs.tile_x < rhs.tile_x;
    }
    return lhs.tile_y < rhs.tile_y;
  });
  return result;
}

GeometryTileKey GeometryTilePyramid::key_for(uint8_t lod_level, LayerId layer_id, Rect32 bbox) const
{
  bbox = normalize(bbox);
  const int32_t size = tile_size(lod_level);
  return GeometryTileKey{lod_level, layer_id, floor_div(bbox.lx, size), floor_div(bbox.ly, size)};
}

std::vector<GeometryTileKey> GeometryTilePyramid::keys_for(uint8_t lod_level, LayerId layer_id, Rect32 bbox) const
{
  std::vector<GeometryTileKey> keys;
  bbox = normalize(bbox);
  const int32_t size = tile_size(lod_level);
  const int32_t min_x = floor_div(bbox.lx, size);
  const int32_t min_y = floor_div(bbox.ly, size);
  const int32_t max_x = floor_div(bbox.hx, size);
  const int32_t max_y = floor_div(bbox.hy, size);

  for (int32_t x = min_x; x <= max_x; ++x) {
    for (int32_t y = min_y; y <= max_y; ++y) {
      keys.push_back(GeometryTileKey{lod_level, layer_id, x, y});
    }
  }

  return keys;
}

int32_t GeometryTilePyramid::tile_size(uint8_t lod_level) const
{
  return _options.base_tile_size << lod_level;
}

uint64_t GeometryTilePyramid::tile_span_count(uint8_t lod_level, Rect32 bbox) const
{
  bbox = normalize(bbox);
  const int32_t size = tile_size(lod_level);
  const int32_t min_x = floor_div(bbox.lx, size);
  const int32_t min_y = floor_div(bbox.ly, size);
  const int32_t max_x = floor_div(bbox.hx, size);
  const int32_t max_y = floor_div(bbox.hy, size);
  return static_cast<uint64_t>(max_x - min_x + 1) * static_cast<uint64_t>(max_y - min_y + 1);
}

void GeometryTilePyramid::add_dirty_keys(uint8_t lod_level, LayerId layer_id, Rect32 bbox)
{
  if (tile_span_count(lod_level, bbox) > _options.max_tile_refs_per_shape) {
    _dirty_tiles.insert(key_for(lod_level, layer_id, bbox));
    return;
  }

  for (GeometryTileKey key : keys_for(lod_level, layer_id, bbox)) {
    _dirty_tiles.insert(key);
  }
}

void GeometryTilePyramid::add_record(uint8_t lod_level, const ShapeRecord& record)
{
  if (tile_span_count(lod_level, record.bbox) > _options.max_tile_refs_per_shape) {
    add_large_record_summary(lod_level, record);
    return;
  }

  for (GeometryTileKey key : keys_for(lod_level, record.layer_id, record.bbox)) {
    add_record_to_key(key, record);
  }
}

void GeometryTilePyramid::add_record_for_dirty_tiles(const ShapeRecord& record,
                                                     const std::unordered_set<GeometryTileKey, GeometryTileKeyHash>& dirty_tiles,
                                                     bool rebuild_large_summary)
{
  for (uint8_t lod = 0; lod < _options.lod_level_count; ++lod) {
    if (tile_span_count(lod, record.bbox) > _options.max_tile_refs_per_shape) {
      if (rebuild_large_summary) {
        remove_large_record_summary(record.id);
        add_large_record_summary(lod, record);
      }
      continue;
    }
    for (GeometryTileKey key : keys_for(lod, record.layer_id, record.bbox)) {
      if (dirty_tiles.contains(key)) {
        add_record_to_key(key, record);
      }
    }
  }
}

void GeometryTilePyramid::add_record_to_key(GeometryTileKey key, const ShapeRecord& record)
{
  auto [iter, inserted] =
      _summaries.emplace(key, GeometryTileSummary{key.lod_level, key.layer_id, key.tile_x, key.tile_y, 0, record.bbox});
  GeometryTileSummary& summary = iter->second;
  if (!inserted) {
    summary.bbox = union_rect(summary.bbox, record.bbox);
  }
  ++summary.shape_count;
  _tile_shape_ids[key].insert(record.id);
  _shape_tile_keys[record.id].push_back(key);
}

void GeometryTilePyramid::add_large_record_summary(uint8_t lod_level, const ShapeRecord& record)
{
  const GeometryTileKey key = key_for(lod_level, record.layer_id, record.bbox);
  _large_shape_summaries.push_back(
      LargeShapeSummary{record.id, GeometryTileSummary{key.lod_level, key.layer_id, key.tile_x, key.tile_y, 1, normalize(record.bbox)}});
  _tile_shape_ids[key].insert(record.id);
  _shape_tile_keys[record.id].push_back(key);
}

void GeometryTilePyramid::remove_record_membership(ShapeId shape_id)
{
  const auto key_iter = _shape_tile_keys.find(shape_id);
  if (key_iter != _shape_tile_keys.end()) {
    for (const GeometryTileKey& key : key_iter->second) {
      auto tile_iter = _tile_shape_ids.find(key);
      if (tile_iter != _tile_shape_ids.end()) {
        tile_iter->second.erase(shape_id);
        if (tile_iter->second.empty()) {
          _tile_shape_ids.erase(tile_iter);
        }
      }
    }
    _shape_tile_keys.erase(key_iter);
  }
  remove_large_record_summary(shape_id);
}

void GeometryTilePyramid::remove_large_record_summary(ShapeId shape_id)
{
  _large_shape_summaries.erase(std::remove_if(_large_shape_summaries.begin(), _large_shape_summaries.end(),
                                              [shape_id](const LargeShapeSummary& summary) { return summary.shape_id == shape_id; }),
                               _large_shape_summaries.end());
}

}  // namespace ecc::geometry
