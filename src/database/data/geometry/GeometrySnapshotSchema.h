#pragma once

#include "OwnerRef.h"
#include "ShapeRecord.h"

#include <cstdint>

namespace ecc::geometry {

constexpr uint32_t kGeometrySchemaVersion = 1;
constexpr uint64_t kGeometryFileMagic = 0x454347454f4d3031ULL;  // ECGEOM01

enum class GeometryFileKind : uint16_t
{
  kUnknown = 0,
  kMeta = 1,
  kShapes = 2,
  kPayload = 3,
  kOwners = 4,
  kNames = 5,
  kNameIndex = 6,
  kOwnerIndex = 7,
  kTiles = 8,
  kView = 9,
  kSidMap = 10,
  kDelta = 11,
};

struct GeometryFileHeader
{
  uint64_t magic = kGeometryFileMagic;
  uint32_t schema_version = kGeometrySchemaVersion;
  uint32_t header_size = sizeof(GeometryFileHeader);
  GeometryFileKind file_kind = GeometryFileKind::kUnknown;
  uint16_t flags = 0;
  uint32_t record_size = 0;
  uint64_t record_count = 0;
  uint64_t payload_size = 0;
  uint64_t reserved0 = 0;
  uint64_t reserved1 = 0;
};

struct GeometryMetaRecord
{
  uint64_t shape_count = 0;
  uint64_t owner_count = 0;
  uint64_t payload_size = 0;
  uint64_t name_record_count = 0;
  uint64_t name_payload_size = 0;
  ShapeId next_shape_id = 1;
  uint64_t reserved0 = 0;
  uint64_t reserved1 = 0;
};

struct GeometrySidMapRecord
{
  ShapeId shape_id = 0;
  OwnerRef owner;
};

struct GeometryViewTileRecord
{
  uint8_t lod_level = 0;
  uint8_t reserved0 = 0;
  LayerId layer_id = 0;
  int32_t tile_x = 0;
  int32_t tile_y = 0;
  uint32_t shape_count = 0;
  uint32_t reserved1 = 0;
  Rect32 bbox;
};

}  // namespace ecc::geometry
