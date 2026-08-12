#pragma once

#include "ShapeRecord.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace ecc::geometry {

struct SpatialTileKey
{
  LayerId layer_id = 0;
  int32_t tile_x = 0;
  int32_t tile_y = 0;

  bool operator==(const SpatialTileKey& other) const
  {
    return layer_id == other.layer_id && tile_x == other.tile_x && tile_y == other.tile_y;
  }
};

struct SpatialTileKeyHash
{
  size_t operator()(const SpatialTileKey& key) const;
};

struct GeometrySpatialIndexOptions
{
  int32_t tile_size = 4096;
  uint32_t max_tiles_per_shape = 256;
};

class GeometrySpatialIndex
{
 public:
  explicit GeometrySpatialIndex(GeometrySpatialIndexOptions options = {});

  void clear();
  void insert(const ShapeRecord& record);
  void remove(const ShapeRecord& record);
  void update(const ShapeRecord& old_record, const ShapeRecord& new_record);
  std::vector<ShapeId> query(LayerId layer_id, Rect32 bbox) const;

 private:
  struct TileRange
  {
    int32_t min_x = 0;
    int32_t min_y = 0;
    int32_t max_x = 0;
    int32_t max_y = 0;
  };

  TileRange tile_range(Rect32 bbox) const;
  bool should_use_large_bucket(TileRange range) const;
  void remove_id(std::vector<ShapeId>& values, ShapeId id) const;

  GeometrySpatialIndexOptions _options;
  std::unordered_map<SpatialTileKey, std::vector<ShapeId>, SpatialTileKeyHash> _tiles;
  std::unordered_map<LayerId, std::vector<ShapeId>> _large_by_layer;
};

}  // namespace ecc::geometry
