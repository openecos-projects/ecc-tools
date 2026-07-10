#include "GeometryStore.h"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <string>
#include <utility>

namespace ecc::geometry {
namespace {

OwnerIndexKey make_owner_key(OwnerType type, OwnerId owner_id)
{
  return OwnerIndexKey{type, owner_id};
}

OwnerShapeKey make_owner_shape_key(OwnerRef owner)
{
  return OwnerShapeKey{owner.type, owner.owner_id, owner.path0, owner.path1, owner.path2, owner.path3};
}

bool are_valid_name_records(const std::vector<GeometryNameRecord>& name_records, const std::vector<std::byte>& name_payloads)
{
  for (const GeometryNameRecord& record : name_records) {
    const uint64_t name_end = record.name_offset + record.name_size;
    if (name_end < record.name_offset || name_end > name_payloads.size()) {
      return false;
    }
  }

  return true;
}

bool is_same_rect(Rect32 lhs, Rect32 rhs)
{
  lhs = normalize(lhs);
  rhs = normalize(rhs);
  return lhs.lx == rhs.lx && lhs.ly == rhs.ly && lhs.hx == rhs.hx && lhs.hy == rhs.hy;
}

ShapeVersion next_version(ShapeVersion version)
{
  return version == std::numeric_limits<ShapeVersion>::max() ? version : version + 1;
}

}  // namespace

size_t OwnerShapeKeyHash::operator()(const OwnerShapeKey& key) const
{
  size_t seed = std::hash<uint8_t>{}(static_cast<uint8_t>(key.type));
  seed ^= std::hash<OwnerId>{}(key.owner_id) + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
  seed ^= std::hash<uint32_t>{}(key.path0) + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
  seed ^= std::hash<uint32_t>{}(key.path1) + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
  seed ^= std::hash<uint32_t>{}(key.path2) + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
  seed ^= std::hash<uint32_t>{}(key.path3) + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
  return seed;
}

void GeometryStore::clear()
{
  _shape_ids.reset();
  _shapes.clear();
  _owners.clear();
  _name_records.clear();
  _name_payloads.clear();
  _delta_events.clear();
  _next_delta_sequence = 1;
  _layer_index.clear();
  _spatial_index.clear();
  _lod_pyramid.clear();
  _owner_index.clear();
  _preserved_shapes_by_owner_path.clear();
  _name_index.clear();
}

void GeometryStore::clear_preserving_shape_ids()
{
  std::unordered_map<OwnerShapeKey, PreservedShapeState, OwnerShapeKeyHash> preserved_shapes;
  ShapeIdAllocator preserved_allocator;
  for (const ShapeRecord& record : _shapes.records()) {
    if (record.id == 0 || record.owner_index >= _owners.size()) {
      continue;
    }
    preserved_shapes.emplace(make_owner_shape_key(_owners[record.owner_index]),
                             PreservedShapeState{record.id, record.version, record.layer_id, record.kind, record.state,
                                                 record.flags, record.bbox});
    preserved_allocator.reserve_seen_id(record.id);
  }

  clear();
  _shape_ids = preserved_allocator;
  _preserved_shapes_by_owner_path = std::move(preserved_shapes);
}

bool GeometryStore::replace_snapshot(std::vector<ShapeRecord> records, std::vector<OwnerRef> owners, std::vector<std::byte> payloads)
{
  return replace_snapshot(std::move(records), std::move(owners), std::move(payloads), {}, {});
}

bool GeometryStore::replace_snapshot(std::vector<ShapeRecord> records, std::vector<OwnerRef> owners, std::vector<std::byte> payloads,
                                     std::vector<GeometryNameRecord> name_records, std::vector<std::byte> name_payloads)
{
  for (const ShapeRecord& record : records) {
    if (record.owner_index >= owners.size()) {
      return false;
    }
  }

  if (!are_valid_name_records(name_records, name_payloads)) {
    return false;
  }

  ShapeTable loaded_shapes;
  if (!loaded_shapes.replace_snapshot(std::move(records), std::move(payloads))) {
    return false;
  }

  clear();
  _owners = std::move(owners);
  _shapes = std::move(loaded_shapes);
  replace_names(std::move(name_records), std::move(name_payloads));

  for (const ShapeRecord& record : _shapes.records()) {
    _shape_ids.reserve_seen_id(record.id);
    index_shape(record, _owners[record.owner_index]);
  }
  _lod_pyramid.rebuild(_shapes.records());

  return true;
}

GeometryStore::AllocatedShapeIdentity GeometryStore::allocate_shape_identity(OwnerRef owner, LayerId layer_id, ShapeKind kind,
                                                                            uint16_t flags, Rect32 bbox)
{
  const OwnerShapeKey key = make_owner_shape_key(owner);
  const auto iter = _preserved_shapes_by_owner_path.find(key);
  if (iter == _preserved_shapes_by_owner_path.end()) {
    return AllocatedShapeIdentity{_shape_ids.allocate(), 1, false, 0, bbox};
  }

  const PreservedShapeState preserved = iter->second;
  _preserved_shapes_by_owner_path.erase(iter);
  _shape_ids.reserve_seen_id(preserved.id);

  const bool unchanged = preserved.state == ShapeState::kAlive && preserved.layer_id == layer_id && preserved.kind == kind
                         && preserved.flags == flags && is_same_rect(preserved.bbox, bbox);
  return AllocatedShapeIdentity{preserved.id, unchanged ? preserved.version : next_version(preserved.version), true,
                                preserved.version, preserved.bbox};
}

void GeometryStore::append_rebuild_delta(const AllocatedShapeIdentity& identity, const ShapeRecord& record)
{
  if (!identity.restored) {
    append_delta(GeometryDeltaOp::kInsert, record.id, 0, record.version, record.bbox, record.bbox);
  } else if (identity.old_version != record.version) {
    append_delta(GeometryDeltaOp::kUpdate, record.id, identity.old_version, record.version, identity.old_bbox, record.bbox);
  }
}

void GeometryStore::index_shape(const ShapeRecord& record, OwnerRef owner)
{
  _layer_index[record.layer_id].push_back(record.id);
  _spatial_index.insert(record);
  _owner_index[make_owner_key(owner.type, owner.owner_id)].push_back(record.id);
}

void GeometryStore::append_delta(GeometryDeltaOp op, ShapeId shape_id, ShapeVersion old_version, ShapeVersion new_version,
                                 Rect32 old_bbox, Rect32 new_bbox, uint64_t command_id)
{
  GeometryDeltaEvent event;
  event.sequence_id = _next_delta_sequence++;
  event.command_id = command_id;
  event.op = op;
  event.shape_id = shape_id;
  event.old_version = old_version;
  event.new_version = new_version;
  event.old_bbox = normalize(old_bbox);
  event.new_bbox = normalize(new_bbox);
  _delta_events.push_back(event);
}

ShapeId GeometryStore::add_rect(LayerId layer_id, Rect32 rect, OwnerRef owner, uint32_t flags)
{
  rect = normalize(rect);
  const uint16_t record_flags = static_cast<uint16_t>(flags);
  const AllocatedShapeIdentity identity =
      allocate_shape_identity(owner, layer_id, ShapeKind::kRect, record_flags, rect);
  const uint32_t owner_index = static_cast<uint32_t>(_owners.size());
  _owners.push_back(owner);

  ShapeRecord record;
  record.id = identity.id;
  record.version = identity.version;
  record.layer_id = layer_id;
  record.kind = ShapeKind::kRect;
  record.state = ShapeState::kAlive;
  record.flags = record_flags;
  record.owner_index = owner_index;
  record.bbox = rect;

  const RectPayload payload{rect};
  _shapes.insert(record, payload);
  index_shape(record, owner);
  _lod_pyramid.mark_dirty_tiles(layer_id, record.bbox, record.bbox);
  append_rebuild_delta(identity, record);
  return record.id;
}

ShapeId GeometryStore::add_point(LayerId layer_id, PointPayload point, OwnerRef owner, uint32_t flags)
{
  const Rect32 bbox{point.point.x, point.point.y, point.point.x, point.point.y};
  const uint16_t record_flags = static_cast<uint16_t>(flags);
  const AllocatedShapeIdentity identity =
      allocate_shape_identity(owner, layer_id, ShapeKind::kPoint, record_flags, bbox);
  const uint32_t owner_index = static_cast<uint32_t>(_owners.size());
  _owners.push_back(owner);

  ShapeRecord record;
  record.id = identity.id;
  record.version = identity.version;
  record.layer_id = layer_id;
  record.kind = ShapeKind::kPoint;
  record.state = ShapeState::kAlive;
  record.flags = record_flags;
  record.owner_index = owner_index;
  record.bbox = bbox;

  _shapes.insert(record, point);
  index_shape(record, owner);
  _lod_pyramid.mark_dirty_tiles(layer_id, record.bbox, record.bbox);
  append_rebuild_delta(identity, record);
  return record.id;
}

ShapeId GeometryStore::add_line(LayerId layer_id, LinePayload line, OwnerRef owner, uint32_t flags)
{
  const int32_t half_width = (std::abs(line.width) + 1) / 2;
  const int32_t lx = std::min(line.begin.x, line.end.x) - half_width;
  const int32_t ly = std::min(line.begin.y, line.end.y) - half_width;
  const int32_t hx = std::max(line.begin.x, line.end.x) + half_width;
  const int32_t hy = std::max(line.begin.y, line.end.y) + half_width;
  const Rect32 bbox{lx, ly, hx, hy};
  const uint16_t record_flags = static_cast<uint16_t>(flags);
  const AllocatedShapeIdentity identity =
      allocate_shape_identity(owner, layer_id, ShapeKind::kLine, record_flags, bbox);
  const uint32_t owner_index = static_cast<uint32_t>(_owners.size());
  _owners.push_back(owner);

  ShapeRecord record;
  record.id = identity.id;
  record.version = identity.version;
  record.layer_id = layer_id;
  record.kind = ShapeKind::kLine;
  record.state = ShapeState::kAlive;
  record.flags = record_flags;
  record.owner_index = owner_index;
  record.bbox = bbox;

  _shapes.insert(record, line);
  index_shape(record, owner);
  _lod_pyramid.mark_dirty_tiles(layer_id, record.bbox, record.bbox);
  append_rebuild_delta(identity, record);
  return record.id;
}

void GeometryStore::add_owner_name(OwnerType type, OwnerId owner_id, std::string_view name)
{
  if (name.empty()) {
    return;
  }

  GeometryNameRecord record;
  record.owner_type = type;
  record.owner_id = owner_id;
  record.name_offset = static_cast<uint64_t>(_name_payloads.size());
  record.name_size = static_cast<uint32_t>(name.size());

  const auto* name_begin = reinterpret_cast<const std::byte*>(name.data());
  _name_payloads.insert(_name_payloads.end(), name_begin, name_begin + name.size());
  _name_records.push_back(record);
  _name_index[std::string{name}].push_back(make_owner_key(type, owner_id));
}

bool GeometryStore::update_rect(ShapeId id, Rect32 rect, uint64_t command_id)
{
  const ShapeRecord* current = _shapes.find(id);
  if (current == nullptr || current->state != ShapeState::kAlive || current->kind != ShapeKind::kRect) {
    return false;
  }

  rect = normalize(rect);

  const ShapeRecord old_record = *current;
  ShapeRecord updated = old_record;
  updated.version = current->version + 1;
  updated.bbox = rect;

  const RectPayload payload{rect};
  if (!_shapes.update(id, updated, payload)) {
    return false;
  }

  _spatial_index.update(old_record, updated);
  _lod_pyramid.mark_dirty_tiles(updated.layer_id, old_record.bbox, updated.bbox);
  append_delta(GeometryDeltaOp::kUpdate, id, old_record.version, updated.version, old_record.bbox, updated.bbox, command_id);
  return true;
}

bool GeometryStore::delete_shape(ShapeId id)
{
  const ShapeRecord* record = _shapes.find(id);
  if (record == nullptr) {
    return false;
  }

  const ShapeRecord old_record = *record;
  _spatial_index.remove(old_record);
  if (!_shapes.mark_deleted(id)) {
    return false;
  }

  const ShapeRecord* deleted_record = _shapes.find(id);
  const ShapeVersion new_version = deleted_record == nullptr ? old_record.version + 1 : deleted_record->version;
  _lod_pyramid.mark_dirty_tiles(old_record.layer_id, old_record.bbox, old_record.bbox);
  append_delta(GeometryDeltaOp::kDelete, id, old_record.version, new_version, old_record.bbox, old_record.bbox);
  return true;
}

const ShapeRecord* GeometryStore::find_shape(ShapeId id) const
{
  return _shapes.find(id);
}

OwnerRef GeometryStore::owner_of(ShapeId id) const
{
  const ShapeRecord* record = find_shape(id);
  if (record == nullptr || record->owner_index >= _owners.size()) {
    return OwnerRef{};
  }

  return _owners[record->owner_index];
}

std::span<const ShapeRecord> GeometryStore::records() const
{
  return _shapes.records();
}

std::span<const OwnerRef> GeometryStore::owners() const
{
  return _owners;
}

std::span<const std::byte> GeometryStore::payloads() const
{
  return _shapes.payloads();
}

std::span<const GeometryNameRecord> GeometryStore::name_records() const
{
  return _name_records;
}

std::span<const std::byte> GeometryStore::name_payloads() const
{
  return _name_payloads;
}

std::span<const GeometryDeltaEvent> GeometryStore::delta_events() const
{
  return _delta_events;
}

void GeometryStore::clear_delta_events()
{
  _delta_events.clear();
}

void GeometryStore::rebuild_lod_tiles()
{
  _lod_pyramid.rebuild(_shapes.records());
}

void GeometryStore::rebuild_dirty_lod_tiles()
{
  _lod_pyramid.rebuild_dirty_tiles(_shapes.records());
}

size_t GeometryStore::dirty_lod_tile_count() const
{
  return _lod_pyramid.dirty_tile_count();
}

std::vector<GeometryTileSummary> GeometryStore::lod_summaries() const
{
  return _lod_pyramid.summaries();
}

std::vector<GeometryTileSummary> GeometryStore::query_lod_tiles(uint8_t lod_level, LayerId layer_id, Rect32 viewport) const
{
  return _lod_pyramid.query(lod_level, layer_id, viewport);
}

std::map<OwnerType, uint64_t> GeometryStore::count_alive_shapes_by_owner_type() const
{
  std::map<OwnerType, uint64_t> counts;
  for (const ShapeRecord& record : _shapes.records()) {
    if (record.state != ShapeState::kAlive || record.owner_index >= _owners.size()) {
      continue;
    }

    ++counts[_owners[record.owner_index].type];
  }

  return counts;
}

std::map<LayerId, uint64_t> GeometryStore::count_alive_shapes_by_layer() const
{
  std::map<LayerId, uint64_t> counts;
  for (const ShapeRecord& record : _shapes.records()) {
    if (record.state != ShapeState::kAlive) {
      continue;
    }

    ++counts[record.layer_id];
  }

  return counts;
}

std::vector<ShapeId> GeometryStore::query_intersect(LayerId layer_id, Rect32 bbox) const
{
  std::vector<ShapeId> result;
  bbox = normalize(bbox);

  for (const ShapeId id : _spatial_index.query(layer_id, bbox)) {
    const ShapeRecord* record = _shapes.find(id);
    if (record == nullptr || record->state != ShapeState::kAlive || record->layer_id != layer_id) {
      continue;
    }
    if (intersects(record->bbox, bbox)) {
      result.push_back(record->id);
    }
  }

  return result;
}

std::vector<ShapeId> GeometryStore::query_owner(OwnerType type, OwnerId owner_id) const
{
  std::vector<ShapeId> result;
  const auto owner_iter = _owner_index.find(make_owner_key(type, owner_id));
  if (owner_iter == _owner_index.end()) {
    return result;
  }

  for (const ShapeId id : owner_iter->second) {
    const ShapeRecord* record = _shapes.find(id);
    if (record == nullptr || record->state != ShapeState::kAlive || record->owner_index >= _owners.size()) {
      continue;
    }

    const OwnerRef& owner = _owners[record->owner_index];
    if (owner.type == type && owner.owner_id == owner_id) {
      result.push_back(record->id);
    }
  }

  return result;
}

std::vector<ShapeId> GeometryStore::query_owner_name(std::string_view name) const
{
  std::vector<ShapeId> result;
  const auto name_iter = _name_index.find(std::string{name});
  if (name_iter == _name_index.end()) {
    return result;
  }

  for (const OwnerIndexKey& owner_key : name_iter->second) {
    std::vector<ShapeId> owner_hits = query_owner(owner_key.type, owner_key.owner_id);
    result.insert(result.end(), owner_hits.begin(), owner_hits.end());
  }

  std::sort(result.begin(), result.end());
  result.erase(std::unique(result.begin(), result.end()), result.end());
  return result;
}

bool GeometryStore::replace_names(std::vector<GeometryNameRecord> name_records, std::vector<std::byte> name_payloads)
{
  if (!are_valid_name_records(name_records, name_payloads)) {
    return false;
  }

  _name_records = std::move(name_records);
  _name_payloads = std::move(name_payloads);
  rebuild_name_index();
  return true;
}

void GeometryStore::rebuild_name_index()
{
  _name_index.clear();

  for (const GeometryNameRecord& record : _name_records) {
    if (record.name_size == 0) {
      continue;
    }

    const uint64_t name_end = record.name_offset + record.name_size;
    if (name_end < record.name_offset || name_end > _name_payloads.size()) {
      continue;
    }

    const size_t offset = static_cast<size_t>(record.name_offset);
    const size_t size = static_cast<size_t>(record.name_size);
    const char* name_data = reinterpret_cast<const char*>(_name_payloads.data() + offset);
    _name_index[std::string{name_data, size}].push_back(make_owner_key(record.owner_type, record.owner_id));
  }
}

}  // namespace ecc::geometry
