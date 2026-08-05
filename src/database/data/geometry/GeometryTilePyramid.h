#pragma once

#include "ShapeRecord.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ecc::geometry {

struct GeometryTilePyramidOptions
{
  int32_t base_tile_size = 4096;
  uint8_t lod_level_count = 4;
  uint32_t max_tile_refs_per_shape = 64;
};

struct GeometryTileKey
{
  uint8_t lod_level = 0;
  LayerId layer_id = 0;
  int32_t tile_x = 0;
  int32_t tile_y = 0;

  bool operator==(const GeometryTileKey& other) const
  {
    return lod_level == other.lod_level && layer_id == other.layer_id && tile_x == other.tile_x && tile_y == other.tile_y;
  }
};

struct GeometryTileKeyHash
{
  size_t operator()(const GeometryTileKey& key) const;
};

struct GeometryTileSummary
{
  uint8_t lod_level = 0;
  LayerId layer_id = 0;
  int32_t tile_x = 0;
  int32_t tile_y = 0;
  uint32_t shape_count = 0;
  Rect32 bbox;
};

class GeometryTilePyramid
{
 public:
  explicit GeometryTilePyramid(GeometryTilePyramidOptions options = {});

  void clear();
  void rebuild(std::span<const ShapeRecord> records);
  void mark_dirty_tiles(LayerId layer_id, Rect32 old_bbox, Rect32 new_bbox);
  void mark_dirty_record_insert(const ShapeRecord& record);
  void mark_dirty_record_update(const ShapeRecord& old_record, const ShapeRecord& new_record);
  void mark_dirty_record_delete(const ShapeRecord& old_record);
  void rebuild_dirty_tiles();
  void rebuild_dirty_tiles(std::span<const ShapeRecord> records);
  size_t dirty_tile_count() const;
  size_t last_dirty_rebuild_candidate_count() const;
  std::vector<GeometryTileSummary> summaries() const;
  std::vector<GeometryTileSummary> query(uint8_t lod_level, LayerId layer_id, Rect32 viewport) const;

 private:
  struct LargeShapeSummary
  {
    ShapeId shape_id = 0;
    GeometryTileSummary summary;
  };

  GeometryTileKey key_for(uint8_t lod_level, LayerId layer_id, Rect32 bbox) const;
  std::vector<GeometryTileKey> keys_for(uint8_t lod_level, LayerId layer_id, Rect32 bbox) const;
  int32_t tile_size(uint8_t lod_level) const;
  uint64_t tile_span_count(uint8_t lod_level, Rect32 bbox) const;
  void add_dirty_keys(uint8_t lod_level, LayerId layer_id, Rect32 bbox);
  void add_record(uint8_t lod_level, const ShapeRecord& record);
  void add_record_for_dirty_tiles(const ShapeRecord& record, const std::unordered_set<GeometryTileKey, GeometryTileKeyHash>& dirty_tiles,
                                  bool rebuild_large_summary);
  void add_record_to_key(GeometryTileKey key, const ShapeRecord& record);
  void add_large_record_summary(uint8_t lod_level, const ShapeRecord& record);
  void remove_record_membership(ShapeId shape_id);
  void remove_large_record_summary(ShapeId shape_id);

  GeometryTilePyramidOptions _options;
  std::unordered_map<GeometryTileKey, GeometryTileSummary, GeometryTileKeyHash> _summaries;
  std::vector<LargeShapeSummary> _large_shape_summaries;
  std::unordered_map<GeometryTileKey, std::unordered_set<ShapeId>, GeometryTileKeyHash> _tile_shape_ids;
  std::unordered_map<ShapeId, std::vector<GeometryTileKey>> _shape_tile_keys;
  std::unordered_map<ShapeId, ShapeRecord> _records_by_id;
  std::unordered_set<GeometryTileKey, GeometryTileKeyHash> _dirty_tiles;
  std::unordered_set<ShapeId> _dirty_shape_ids;
  size_t _last_dirty_rebuild_candidate_count = 0;
};

}  // namespace ecc::geometry
