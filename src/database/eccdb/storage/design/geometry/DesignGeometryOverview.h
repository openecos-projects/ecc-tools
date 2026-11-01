// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#pragma once

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include <optional>
#include <span>

#include "common/GeometryTypes.h"
#include "geometry/GeometryEntity.h"

namespace eccdb {

class DesignStore;

// Runtime-only, KLayout-inspired overview of the editable geometry.
//
// GeometryShape remains the source of truth.  A tile stores only a cheap
// summary and a 16x16 occupancy mask; it never owns or duplicates a shape.
// The overview is therefore safe to discard after an edit, a binary save, or
// a viewport change.  Exact shape queries still go through the canonical
// geometry spatial index.
class DesignGeometryOverview
{
 public:
  using ShapeId = GeometryShapeId;

  struct TileKey
  {
    uint8_t level = 0;
    int64_t x = 0;
    int64_t y = 0;

    friend bool operator==(const TileKey&, const TileKey&) = default;
  };

  struct LayerSummary
  {
    uint64_t layer = 0;
    uint32_t shape_count = 0;
    Rect bounds{};
    // Per-layer occupancy is required by the renderer. A tile-wide mask
    // cannot be reused for every layer without painting M2/M3 occupancy in
    // an M1-only view.
    std::array<uint64_t, 4> occupancy{};

    [[nodiscard]] bool occupiedCell(uint32_t column, uint32_t row) const noexcept
    {
      if (column >= 16u || row >= 16u) return false;
      const auto bit = row * 16u + column;
      return (occupancy[bit / 64u] & (uint64_t{1} << (bit % 64u))) != 0;
    }
  };

  struct Tile
  {
    TileKey key{};
    Rect bounds{};
    Rect occupied{};
    uint32_t shape_count = 0;
    uint64_t covered_area = 0;
    // Bit (row * 16 + column) means that part of the tile is occupied.
    std::array<uint64_t, 4> occupancy{};
    std::vector<LayerSummary> layers;
    uint64_t geometry_epoch = 0;

    [[nodiscard]] bool occupiedCell(uint32_t column, uint32_t row) const noexcept
    {
      if (column >= 16u || row >= 16u) return false;
      const auto bit = row * 16u + column;
      return (occupancy[bit / 64u] & (uint64_t{1} << (bit % 64u))) != 0;
    }
  };

  explicit DesignGeometryOverview(const DesignStore& design, int64_t base_tile_size = 4096) noexcept;

  DesignGeometryOverview(const DesignGeometryOverview&) = delete;
  DesignGeometryOverview& operator=(const DesignGeometryOverview&) = delete;

  [[nodiscard]] int64_t baseTileSize() const noexcept { return _base_tile_size; }
  void setBaseTileSize(int64_t value);

  // Explicit invalidation is useful for a GUI transaction.  Normal edits are
  // also detected lazily through DesignGeometryStorage::geometryEpoch().
  void invalidate() noexcept;
  void applyChange(Rect old_bounds, std::optional<Rect> new_bounds, uint64_t previous_epoch) const;
  // Seed detached overview metadata (for example, a validated GUI sidecar)
  // into this runtime cache.  No canonical GeometryShape payload is copied.
  // The seed is accepted only for the design's current geometry epoch.
  [[nodiscard]] bool seed(std::span<const Tile> tiles, uint64_t epoch) const;
  [[nodiscard]] uint64_t cachedEpoch() const noexcept { return _cached_epoch; }
  [[nodiscard]] std::size_t cachedTileCount() const noexcept { return _tiles.size(); }

  [[nodiscard]] Tile tile(TileKey key) const;
  [[nodiscard]] std::vector<Tile> tiles(Rect viewport, uint8_t level = 0) const;

  // Exact IDs are intentionally not stored in a tile.  This method is the
  // high-zoom escape hatch for a renderer or editor.
  [[nodiscard]] std::vector<ShapeId> exactShapes(Rect viewport) const;

  [[nodiscard]] Rect tileBounds(TileKey key) const noexcept;
  [[nodiscard]] int64_t tileSize(uint8_t level) const noexcept;

 private:
  struct TileKeyHash
  {
    [[nodiscard]] std::size_t operator()(TileKey key) const noexcept;
  };

  void ensureEpoch() const;
  [[nodiscard]] Tile buildTile(TileKey key) const;
  static void markOccupancy(Tile& tile, Rect shape_bounds, bool thin = false) noexcept;
  static void markOccupancy(std::array<uint64_t, 4>& occupancy, Rect tile_bounds,
                            Rect shape_bounds, bool thin = false) noexcept;
  static void addLayer(Tile& tile, uint64_t layer, Rect shape_bounds) noexcept;
  [[nodiscard]] static int64_t floorDivide(int64_t value, int64_t divisor) noexcept;
  [[nodiscard]] static int32_t clampCoordinate(int64_t value) noexcept;

  const DesignStore& _design;
  int64_t _base_tile_size;
  mutable uint64_t _cached_epoch = 0;
  mutable std::unordered_map<TileKey, Tile, TileKeyHash> _tiles;
};

}  // namespace eccdb
