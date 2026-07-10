#pragma once

#include "GeometryDelta.h"
#include "GeometryName.h"
#include "GeometrySpatialIndex.h"
#include "OwnerRef.h"
#include "ShapeId.h"
#include "ShapeTable.h"

#include <cstddef>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ecc::geometry {

struct OwnerIndexKey
{
  OwnerType type = OwnerType::kNone;
  OwnerId owner_id = 0;

  bool operator==(const OwnerIndexKey& other) const { return type == other.type && owner_id == other.owner_id; }
};

struct OwnerIndexKeyHash
{
  size_t operator()(const OwnerIndexKey& key) const
  {
    const size_t type_hash = std::hash<uint8_t>{}(static_cast<uint8_t>(key.type));
    const size_t id_hash = std::hash<OwnerId>{}(key.owner_id);
    return type_hash ^ (id_hash + 0x9e3779b97f4a7c15ULL + (type_hash << 6U) + (type_hash >> 2U));
  }
};

struct OwnerShapeKey
{
  OwnerType type = OwnerType::kNone;
  OwnerId owner_id = 0;
  uint32_t path0 = 0;
  uint32_t path1 = 0;
  uint32_t path2 = 0;
  uint32_t path3 = 0;

  bool operator==(const OwnerShapeKey& other) const
  {
    return type == other.type && owner_id == other.owner_id && path0 == other.path0 && path1 == other.path1
           && path2 == other.path2 && path3 == other.path3;
  }
};

struct OwnerShapeKeyHash
{
  size_t operator()(const OwnerShapeKey& key) const;
};

class GeometryStore
{
 public:
  void clear();
  void clear_preserving_shape_ids();
  bool replace_snapshot(std::vector<ShapeRecord> records, std::vector<OwnerRef> owners, std::vector<std::byte> payloads);
  bool replace_snapshot(std::vector<ShapeRecord> records, std::vector<OwnerRef> owners, std::vector<std::byte> payloads,
                        std::vector<GeometryNameRecord> name_records, std::vector<std::byte> name_payloads);

  ShapeId add_rect(LayerId layer_id, Rect32 rect, OwnerRef owner, uint32_t flags = 0);
  ShapeId add_point(LayerId layer_id, PointPayload point, OwnerRef owner, uint32_t flags = 0);
  ShapeId add_line(LayerId layer_id, LinePayload line, OwnerRef owner, uint32_t flags = 0);
  void add_owner_name(OwnerType type, OwnerId owner_id, std::string_view name);
  bool update_rect(ShapeId id, Rect32 rect, uint64_t command_id = 0);
  bool delete_shape(ShapeId id);

  const ShapeRecord* find_shape(ShapeId id) const;
  OwnerRef owner_of(ShapeId id) const;

  std::span<const ShapeRecord> records() const;
  std::span<const OwnerRef> owners() const;
  std::span<const std::byte> payloads() const;
  std::span<const GeometryNameRecord> name_records() const;
  std::span<const std::byte> name_payloads() const;
  std::span<const GeometryDeltaEvent> delta_events() const;
  void clear_delta_events();
  std::map<OwnerType, uint64_t> count_alive_shapes_by_owner_type() const;
  std::map<LayerId, uint64_t> count_alive_shapes_by_layer() const;

  std::vector<ShapeId> query_intersect(LayerId layer_id, Rect32 bbox) const;
  std::vector<ShapeId> query_owner(OwnerType type, OwnerId owner_id) const;
  std::vector<ShapeId> query_owner_name(std::string_view name) const;

 private:
  ShapeId allocate_shape_id(OwnerRef owner);
  void index_shape(const ShapeRecord& record, OwnerRef owner);
  void append_delta(GeometryDeltaOp op, ShapeId shape_id, ShapeVersion old_version, ShapeVersion new_version, Rect32 old_bbox,
                    Rect32 new_bbox, uint64_t command_id = 0);
  bool replace_names(std::vector<GeometryNameRecord> name_records, std::vector<std::byte> name_payloads);
  void rebuild_name_index();

  ShapeIdAllocator _shape_ids;
  ShapeTable _shapes;
  std::vector<OwnerRef> _owners;
  std::vector<GeometryNameRecord> _name_records;
  std::vector<std::byte> _name_payloads;
  std::vector<GeometryDeltaEvent> _delta_events;
  uint64_t _next_delta_sequence = 1;
  std::unordered_map<LayerId, std::vector<ShapeId>> _layer_index;
  GeometrySpatialIndex _spatial_index;
  std::unordered_map<OwnerIndexKey, std::vector<ShapeId>, OwnerIndexKeyHash> _owner_index;
  std::unordered_map<OwnerShapeKey, ShapeId, OwnerShapeKeyHash> _shape_ids_by_owner_path;
  std::unordered_map<std::string, std::vector<OwnerIndexKey>> _name_index;
};

}  // namespace ecc::geometry
