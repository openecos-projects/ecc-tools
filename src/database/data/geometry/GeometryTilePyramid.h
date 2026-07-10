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
  void rebuild_dirty_tiles(std::span<const ShapeRecord> records);
  size_t dirty_tile_count() const;
  std::vector<GeometryTileSummary> summaries() const;
  std::vector<GeometryTileSummary> query(uint8_t lod_level, LayerId layer_id, Rect32 viewport) const;

 private:
  GeometryTileKey key_for(uint8_t lod_level, LayerId layer_id, Rect32 bbox) const;
  std::vector<GeometryTileKey> keys_for(uint8_t lod_level, LayerId layer_id, Rect32 bbox) const;
  int32_t tile_size(uint8_t lod_level) const;
  uint64_t tile_span_count(uint8_t lod_level, Rect32 bbox) const;
  void add_dirty_keys(uint8_t lod_level, LayerId layer_id, Rect32 bbox);
  void add_record(uint8_t lod_level, const ShapeRecord& record);
  void add_record_to_key(GeometryTileKey key, const ShapeRecord& record);
  void add_large_record_summary(uint8_t lod_level, const ShapeRecord& record);

  GeometryTilePyramidOptions _options;
  std::unordered_map<GeometryTileKey, GeometryTileSummary, GeometryTileKeyHash> _summaries;
  std::vector<GeometryTileSummary> _large_shape_summaries;
  std::unordered_set<GeometryTileKey, GeometryTileKeyHash> _dirty_tiles;
};

}  // namespace ecc::geometry
