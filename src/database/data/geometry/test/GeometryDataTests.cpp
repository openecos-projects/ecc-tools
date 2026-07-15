#include "GeometryEdit.h"
#include "GeometryDelta.h"
#include "GeometrySpatialIndex.h"
#include "GeometrySnapshotSchema.h"
#include "GeometryStore.h"
#include "GeometryTilePyramid.h"
#include "GeometryTypes.h"
#include "ShapeId.h"
#include "ShapeTable.h"

#include <cassert>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace {

using namespace ecc::geometry;

void test_rect_normalization_and_intersection()
{
  const Rect32 raw{10, 20, -5, 7};
  const Rect32 normalized = normalize(raw);

  assert(normalized.lx == -5);
  assert(normalized.ly == 7);
  assert(normalized.hx == 10);
  assert(normalized.hy == 20);
  assert(intersects(normalized, Rect32{0, 0, 20, 10}));
  assert(!intersects(normalized, Rect32{11, 0, 20, 10}));
}

void test_shape_id_allocator_is_monotonic()
{
  ShapeIdAllocator allocator;

  assert(allocator.allocate() == 1);
  assert(allocator.allocate() == 2);

  allocator.reserve_seen_id(50);

  assert(allocator.allocate() == 51);
  assert(allocator.next_id() == 52);

  allocator.reset();

  assert(allocator.allocate() == 1);
}

void test_shape_table_insert_update_and_delete()
{
  ShapeTable table;

  ShapeRecord record;
  record.id = 7;
  record.version = 1;
  record.layer_id = 3;
  record.kind = ShapeKind::kRect;
  record.state = ShapeState::kAlive;
  record.bbox = Rect32{0, 0, 100, 200};

  const RectPayload payload{record.bbox};
  const RecordIndex index = table.insert(record, payload);

  assert(index == 0);
  assert(table.size() == 1);
  assert(table.find(7) != nullptr);
  assert(table.find(7)->bbox.hx == 100);

  record.version = 2;
  record.bbox = Rect32{10, 20, 110, 220};
  const RectPayload updated_payload{record.bbox};

  assert(table.update(7, record, updated_payload));
  assert(table.find(7)->version == 2);
  assert(table.find(7)->bbox.lx == 10);

  assert(table.mark_deleted(7));
  assert(table.find(7)->state == ShapeState::kDeleted);

  table.clear();

  assert(table.size() == 0);
  assert(table.find(7) == nullptr);
  assert(table.records().empty());
  assert(table.payloads().empty());
}

void test_geometry_store_queries_by_shape_and_layer()
{
  GeometryStore store;

  const ShapeId first = store.add_rect(2, Rect32{0, 0, 100, 100}, OwnerRef{OwnerType::kDie});
  const ShapeId second = store.add_rect(2, Rect32{200, 200, 300, 300}, OwnerRef{OwnerType::kCore});
  const ShapeId third = store.add_rect(3, Rect32{0, 0, 50, 50}, OwnerRef{OwnerType::kRow});

  assert(first == 1);
  assert(second == 2);
  assert(third == 3);
  assert(store.find_shape(second) != nullptr);
  assert(store.owner_of(first).type == OwnerType::kDie);

  const std::vector<ShapeId> layer_hits = store.query_intersect(2, Rect32{50, 50, 250, 250});

  assert(layer_hits.size() == 2);
  assert(layer_hits[0] == first);
  assert(layer_hits[1] == second);

  assert(store.delete_shape(first));
  assert(store.find_shape(first)->state == ShapeState::kDeleted);

  const std::vector<ShapeId> after_delete = store.query_intersect(2, Rect32{0, 0, 100, 100});

  assert(after_delete.empty());
}

void test_geometry_store_supports_point_and_line_shapes()
{
  GeometryStore store;

  PointPayload point;
  point.point = Point32{9, 11};
  const ShapeId point_id = store.add_point(5, point, OwnerRef{OwnerType::kPinPortShape});

  LinePayload line;
  line.begin = Point32{0, 0};
  line.end = Point32{10, 0};
  line.width = 4;
  const ShapeId line_id = store.add_line(6, line, OwnerRef{OwnerType::kNetWireSegment});

  const ShapeRecord* point_record = store.find_shape(point_id);
  const ShapeRecord* line_record = store.find_shape(line_id);

  assert(point_record != nullptr);
  assert(point_record->kind == ShapeKind::kPoint);
  assert(point_record->bbox.lx == 9);
  assert(point_record->bbox.hy == 11);
  assert(store.owner_of(point_id).type == OwnerType::kPinPortShape);

  assert(line_record != nullptr);
  assert(line_record->kind == ShapeKind::kLine);
  assert(line_record->bbox.lx == -2);
  assert(line_record->bbox.ly == -2);
  assert(line_record->bbox.hx == 12);
  assert(line_record->bbox.hy == 2);
  assert(store.owner_of(line_id).type == OwnerType::kNetWireSegment);

  const std::vector<ShapeId> line_hits = store.query_intersect(6, Rect32{11, -1, 11, 1});

  assert(line_hits.size() == 1);
  assert(line_hits[0] == line_id);
}

void test_geometry_store_queries_alive_shapes_by_owner()
{
  GeometryStore store;

  OwnerRef net_owner;
  net_owner.type = OwnerType::kNetWireSegment;
  net_owner.owner_id = 42;
  net_owner.path0 = 1;

  OwnerRef same_net_owner = net_owner;
  same_net_owner.path0 = 2;

  OwnerRef other_net_owner = net_owner;
  other_net_owner.owner_id = 43;

  const ShapeId first = store.add_rect(1, Rect32{0, 0, 10, 10}, net_owner);
  const ShapeId second = store.add_rect(2, Rect32{20, 20, 30, 30}, same_net_owner);
  const ShapeId other = store.add_rect(1, Rect32{40, 40, 50, 50}, other_net_owner);

  std::vector<ShapeId> hits = store.query_owner(OwnerType::kNetWireSegment, 42);

  assert(hits.size() == 2);
  assert(hits[0] == first);
  assert(hits[1] == second);

  assert(store.delete_shape(first));

  hits = store.query_owner(OwnerType::kNetWireSegment, 42);

  assert(hits.size() == 1);
  assert(hits[0] == second);

  const std::vector<ShapeId> other_hits = store.query_owner(OwnerType::kNetWireSegment, 43);

  assert(other_hits.size() == 1);
  assert(other_hits[0] == other);
}

void test_geometry_store_queries_alive_shapes_by_owner_name()
{
  GeometryStore store;

  OwnerRef net_owner;
  net_owner.type = OwnerType::kNetWireSegment;
  net_owner.owner_id = 42;

  const ShapeId first = store.add_rect(1, Rect32{0, 0, 10, 10}, net_owner);
  const ShapeId second = store.add_rect(2, Rect32{20, 20, 30, 30}, net_owner);

  OwnerRef other_owner;
  other_owner.type = OwnerType::kInstanceBBox;
  other_owner.owner_id = 88;
  const ShapeId other = store.add_rect(1, Rect32{40, 40, 50, 50}, other_owner);

  store.add_owner_name(OwnerType::kNetWireSegment, 42, "clk");
  store.add_owner_name(OwnerType::kInstanceBBox, 88, "u0");

  std::vector<ShapeId> net_hits = store.query_owner_name("clk");

  assert(net_hits.size() == 2);
  assert(net_hits[0] == first);
  assert(net_hits[1] == second);

  assert(store.name_records().size() == 2);
  assert(store.name_payloads().size() == 5);

  assert(store.delete_shape(first));

  net_hits = store.query_owner_name("clk");

  assert(net_hits.size() == 1);
  assert(net_hits[0] == second);

  const std::vector<ShapeId> inst_hits = store.query_owner_name("u0");

  assert(inst_hits.size() == 1);
  assert(inst_hits[0] == other);
  assert(store.query_owner_name("missing").empty());
}

void test_geometry_store_local_names_do_not_pollute_owner_name_queries()
{
  GeometryStore store;

  const NameId local_name_id = store.add_local_name("via:VIA12");

  OwnerRef via_owner;
  via_owner.type = OwnerType::kVia;
  via_owner.owner_id = 77;
  via_owner.name_id = local_name_id;
  const ShapeId via_shape = store.add_rect(12, Rect32{0, 0, 4, 4}, via_owner);

  assert(local_name_id == 1);
  assert(store.name_records().size() == 1);
  assert(store.name_payloads().size() == 9);
  assert(store.owner_of(via_shape).name_id == local_name_id);
  assert(store.query_owner_name("via:VIA12").empty());
}

void test_geometry_store_counts_alive_shapes_by_owner_type_and_layer()
{
  GeometryStore store;

  const ShapeId die = store.add_rect(0, Rect32{0, 0, 100, 100}, OwnerRef{OwnerType::kDie});
  store.add_rect(1, Rect32{10, 10, 20, 20}, OwnerRef{OwnerType::kInstanceBBox});
  store.add_rect(1, Rect32{20, 20, 30, 30}, OwnerRef{OwnerType::kInstanceBBox});
  store.add_rect(2, Rect32{30, 30, 40, 40}, OwnerRef{OwnerType::kNetWireSegment});

  assert(store.delete_shape(die));

  const std::map<OwnerType, uint64_t> owner_counts = store.count_alive_shapes_by_owner_type();
  const std::map<LayerId, uint64_t> layer_counts = store.count_alive_shapes_by_layer();

  assert(!owner_counts.contains(OwnerType::kDie));
  assert(owner_counts.at(OwnerType::kInstanceBBox) == 2);
  assert(owner_counts.at(OwnerType::kNetWireSegment) == 1);
  assert(!layer_counts.contains(0));
  assert(layer_counts.at(1) == 2);
  assert(layer_counts.at(2) == 1);
}

void test_owner_type_label_includes_instance_halo()
{
  assert(owner_type_label(OwnerType::kInstanceHalo) == "instance_halo");
}

void test_geometry_spatial_index_returns_unique_layer_candidates()
{
  GeometrySpatialIndex index;

  ShapeRecord first;
  first.id = 10;
  first.layer_id = 2;
  first.kind = ShapeKind::kRect;
  first.state = ShapeState::kAlive;
  first.bbox = Rect32{0, 0, 10, 10};

  ShapeRecord spanning = first;
  spanning.id = 11;
  spanning.bbox = Rect32{4090, 0, 4105, 10};

  ShapeRecord other_layer = first;
  other_layer.id = 12;
  other_layer.layer_id = 3;
  other_layer.bbox = Rect32{0, 0, 10, 10};

  index.insert(first);
  index.insert(spanning);
  index.insert(other_layer);

  const std::vector<ShapeId> hits = index.query(2, Rect32{4096, 1, 4100, 2});

  assert(hits.size() == 1);
  assert(hits[0] == 11);
}

void test_geometry_spatial_index_updates_and_removes_shapes()
{
  GeometrySpatialIndex index;

  ShapeRecord record;
  record.id = 20;
  record.layer_id = 4;
  record.kind = ShapeKind::kRect;
  record.state = ShapeState::kAlive;
  record.bbox = Rect32{0, 0, 10, 10};

  index.insert(record);
  assert(index.query(4, Rect32{1, 1, 2, 2}).size() == 1);

  ShapeRecord updated = record;
  updated.version = 2;
  updated.bbox = Rect32{9000, 9000, 9010, 9010};
  index.update(record, updated);

  assert(index.query(4, Rect32{1, 1, 2, 2}).empty());

  const std::vector<ShapeId> updated_hits = index.query(4, Rect32{9001, 9001, 9002, 9002});
  assert(updated_hits.size() == 1);
  assert(updated_hits[0] == 20);

  index.remove(updated);
  assert(index.query(4, Rect32{9001, 9001, 9002, 9002}).empty());
}

void test_geometry_tile_pyramid_builds_layer_lod_summaries()
{
  std::vector<ShapeRecord> records;

  ShapeRecord first;
  first.id = 101;
  first.layer_id = 2;
  first.kind = ShapeKind::kRect;
  first.state = ShapeState::kAlive;
  first.bbox = Rect32{0, 0, 10, 10};
  records.push_back(first);

  ShapeRecord second = first;
  second.id = 102;
  second.bbox = Rect32{20, 20, 30, 30};
  records.push_back(second);

  ShapeRecord other_layer = first;
  other_layer.id = 103;
  other_layer.layer_id = 3;
  other_layer.bbox = Rect32{0, 0, 10, 10};
  records.push_back(other_layer);

  ShapeRecord deleted = first;
  deleted.id = 104;
  deleted.state = ShapeState::kDeleted;
  records.push_back(deleted);

  GeometryTilePyramid pyramid(GeometryTilePyramidOptions{4096, 3});
  pyramid.rebuild(records);

  const std::vector<GeometryTileSummary> layer2_tiles = pyramid.query(0, 2, Rect32{0, 0, 100, 100});

  assert(layer2_tiles.size() == 1);
  assert(layer2_tiles[0].lod_level == 0);
  assert(layer2_tiles[0].layer_id == 2);
  assert(layer2_tiles[0].tile_x == 0);
  assert(layer2_tiles[0].tile_y == 0);
  assert(layer2_tiles[0].shape_count == 2);
  assert(layer2_tiles[0].bbox.lx == 0);
  assert(layer2_tiles[0].bbox.hy == 30);

  const std::vector<GeometryTileSummary> layer3_tiles = pyramid.query(0, 3, Rect32{0, 0, 100, 100});

  assert(layer3_tiles.size() == 1);
  assert(layer3_tiles[0].shape_count == 1);
}

void test_geometry_tile_pyramid_coarsens_parent_lod_tiles()
{
  std::vector<ShapeRecord> records;

  ShapeRecord left;
  left.id = 201;
  left.layer_id = 5;
  left.kind = ShapeKind::kRect;
  left.state = ShapeState::kAlive;
  left.bbox = Rect32{0, 0, 10, 10};
  records.push_back(left);

  ShapeRecord right = left;
  right.id = 202;
  right.bbox = Rect32{4096, 0, 4106, 10};
  records.push_back(right);

  GeometryTilePyramid pyramid(GeometryTilePyramidOptions{4096, 3});
  pyramid.rebuild(records);

  const std::vector<GeometryTileSummary> lod0_tiles = pyramid.query(0, 5, Rect32{0, 0, 5000, 100});
  const std::vector<GeometryTileSummary> lod1_tiles = pyramid.query(1, 5, Rect32{0, 0, 5000, 100});

  assert(lod0_tiles.size() == 2);
  assert(lod0_tiles[0].shape_count == 1);
  assert(lod0_tiles[1].shape_count == 1);

  assert(lod1_tiles.size() == 1);
  assert(lod1_tiles[0].lod_level == 1);
  assert(lod1_tiles[0].tile_x == 0);
  assert(lod1_tiles[0].tile_y == 0);
  assert(lod1_tiles[0].shape_count == 2);
  assert(lod1_tiles[0].bbox.lx == 0);
  assert(lod1_tiles[0].bbox.hx == 4106);

  pyramid.clear();
  assert(pyramid.query(0, 5, Rect32{0, 0, 5000, 100}).empty());
}

void test_geometry_tile_pyramid_rebuilds_dirty_tiles_only()
{
  std::vector<ShapeRecord> records;

  ShapeRecord moving;
  moving.id = 301;
  moving.layer_id = 6;
  moving.kind = ShapeKind::kRect;
  moving.state = ShapeState::kAlive;
  moving.bbox = Rect32{0, 0, 10, 10};
  records.push_back(moving);

  ShapeRecord stable = moving;
  stable.id = 302;
  stable.bbox = Rect32{8192, 0, 8202, 10};
  records.push_back(stable);

  GeometryTilePyramid pyramid(GeometryTilePyramidOptions{4096, 3});
  pyramid.rebuild(records);

  const Rect32 old_bbox = records[0].bbox;
  records[0].bbox = Rect32{4096, 0, 4106, 10};
  pyramid.mark_dirty_tiles(6, old_bbox, records[0].bbox);

  assert(pyramid.dirty_tile_count() == 4);

  pyramid.rebuild_dirty_tiles(records);

  assert(pyramid.dirty_tile_count() == 0);

  const std::vector<GeometryTileSummary> old_lod0 = pyramid.query(0, 6, Rect32{0, 0, 100, 100});
  const std::vector<GeometryTileSummary> new_lod0 = pyramid.query(0, 6, Rect32{4096, 0, 4200, 100});
  const std::vector<GeometryTileSummary> stable_lod0 = pyramid.query(0, 6, Rect32{8192, 0, 8300, 100});
  const std::vector<GeometryTileSummary> parent_lod1 = pyramid.query(1, 6, Rect32{0, 0, 8200, 100});

  assert(old_lod0.empty());
  assert(new_lod0.size() == 1);
  assert(new_lod0[0].shape_count == 1);
  assert(new_lod0[0].bbox.lx == 4096);
  assert(stable_lod0.size() == 1);
  assert(stable_lod0[0].shape_count == 1);
  assert(parent_lod1.size() == 2);
  assert(parent_lod1[0].shape_count == 1);
  assert(parent_lod1[1].shape_count == 1);
}

void test_geometry_tile_pyramid_rebuilds_dirty_tiles_from_indexed_candidates()
{
  std::vector<ShapeRecord> records;

  ShapeRecord moving;
  moving.id = 350;
  moving.layer_id = 6;
  moving.kind = ShapeKind::kRect;
  moving.state = ShapeState::kAlive;
  moving.bbox = Rect32{0, 0, 10, 10};
  records.push_back(moving);

  for (int32_t i = 0; i < 128; ++i) {
    ShapeRecord stable = moving;
    stable.id = static_cast<ShapeId>(351 + i);
    stable.bbox = Rect32{100000 + i * 100, 100000, 100010 + i * 100, 100010};
    records.push_back(stable);
  }

  GeometryTilePyramid pyramid(GeometryTilePyramidOptions{4096, 3});
  pyramid.rebuild(records);

  ShapeRecord updated = moving;
  updated.bbox = Rect32{4096, 0, 4106, 10};
  pyramid.mark_dirty_record_update(moving, updated);
  pyramid.rebuild_dirty_tiles();

  assert(pyramid.last_dirty_rebuild_candidate_count() < records.size());
  assert(pyramid.query(0, 6, Rect32{0, 0, 100, 100}).empty());
  assert(pyramid.query(0, 6, Rect32{4096, 0, 4200, 100}).size() == 1);
}

void test_geometry_tile_pyramid_handles_large_shapes_without_tile_explosion()
{
  ShapeRecord large;
  large.id = 401;
  large.layer_id = 7;
  large.kind = ShapeKind::kRect;
  large.state = ShapeState::kAlive;
  large.bbox = Rect32{0, 0, 1'000'000, 1'000'000};

  GeometryTilePyramid pyramid(GeometryTilePyramidOptions{4096, 3});
  pyramid.rebuild(std::span<const ShapeRecord>{&large, 1});

  const std::vector<GeometryTileSummary> far_tiles = pyramid.query(0, 7, Rect32{900'000, 900'000, 901'000, 901'000});
  const std::vector<GeometryTileSummary> near_tiles = pyramid.query(0, 7, Rect32{0, 0, 100, 100});

  assert(far_tiles.size() == 1);
  assert(near_tiles.size() == 1);
  assert(far_tiles[0].shape_count == 1);
  assert(far_tiles[0].bbox.hx == 1'000'000);
}

void test_geometry_store_accepts_spatial_and_lod_options()
{
  GeometryStoreOptions options;
  options.spatial_index.tile_size = 128;
  options.spatial_index.max_tiles_per_shape = 4;
  options.lod_pyramid.base_tile_size = 128;
  options.lod_pyramid.lod_level_count = 1;
  options.lod_pyramid.max_tile_refs_per_shape = 4;
  GeometryStore store(options);

  store.add_rect(5, Rect32{0, 0, 10, 10}, OwnerRef{OwnerType::kDie});
  store.rebuild_lod_tiles();

  const std::vector<ShapeId> spatial_hits = store.query_intersect(5, Rect32{0, 0, 20, 20});
  const std::vector<GeometryTileSummary> lod0_tiles = store.query_lod_tiles(0, 5, Rect32{0, 0, 20, 20});
  const std::vector<GeometryTileSummary> lod1_tiles = store.query_lod_tiles(1, 5, Rect32{0, 0, 20, 20});

  assert(spatial_hits.size() == 1);
  assert(lod0_tiles.size() == 1);
  assert(lod1_tiles.empty());
}

void test_geometry_store_clear_resets_records_owners_payloads_and_shape_ids()
{
  GeometryStore store;

  const ShapeId first = store.add_rect(1, Rect32{0, 0, 10, 10}, OwnerRef{OwnerType::kDie});
  store.add_owner_name(OwnerType::kDie, 0, "die");
  assert(first == 1);
  assert(store.records().size() == 1);
  assert(store.owners().size() == 1);
  assert(!store.payloads().empty());
  assert(!store.name_records().empty());
  assert(!store.name_payloads().empty());

  store.clear();

  assert(store.records().empty());
  assert(store.owners().empty());
  assert(store.payloads().empty());
  assert(store.name_records().empty());
  assert(store.name_payloads().empty());
  assert(store.find_shape(first) == nullptr);

  const ShapeId after_clear = store.add_rect(1, Rect32{20, 20, 30, 30}, OwnerRef{OwnerType::kCore});
  assert(after_clear == 1);
}

void test_geometry_store_preserves_shape_ids_by_owner_path_across_rebuild()
{
  GeometryStore store;

  OwnerRef first_owner;
  first_owner.type = OwnerType::kInstanceBBox;
  first_owner.owner_id = 10;
  first_owner.path0 = 7;

  OwnerRef second_owner;
  second_owner.type = OwnerType::kBlockage;
  second_owner.owner_id = 2;
  second_owner.path0 = 1;

  const ShapeId first_id = store.add_rect(1, Rect32{0, 0, 10, 10}, first_owner);
  const ShapeId second_id = store.add_rect(1, Rect32{20, 20, 30, 30}, second_owner);

  store.clear_preserving_shape_ids();

  OwnerRef new_owner;
  new_owner.type = OwnerType::kRow;
  new_owner.owner_id = 99;
  const ShapeId new_id = store.add_rect(1, Rect32{40, 40, 50, 50}, new_owner);
  const ShapeId restored_second_id = store.add_rect(1, Rect32{22, 22, 32, 32}, second_owner);
  const ShapeId restored_first_id = store.add_rect(1, Rect32{2, 2, 12, 12}, first_owner);

  assert(first_id == 1);
  assert(second_id == 2);
  assert(new_id == 3);
  assert(restored_second_id == second_id);
  assert(restored_first_id == first_id);
}

void test_geometry_store_preserves_versions_for_unchanged_rebuilt_shapes()
{
  GeometryStore store;
  OwnerRef owner;
  owner.type = OwnerType::kInstanceBBox;
  owner.owner_id = 41;
  owner.path0 = 3;

  const ShapeId original_id = store.add_rect(2, Rect32{0, 0, 10, 10}, owner);
  assert(store.update_rect(original_id, Rect32{20, 20, 30, 30}, 901));
  assert(store.find_shape(original_id)->version == 2);

  store.clear_preserving_shape_ids();
  const ShapeId unchanged_id = store.add_rect(2, Rect32{20, 20, 30, 30}, owner);

  assert(unchanged_id == original_id);
  assert(store.find_shape(unchanged_id)->version == 2);
  assert(store.delta_events().empty());

  store.clear_preserving_shape_ids();
  const ShapeId changed_id = store.add_rect(2, Rect32{40, 40, 50, 50}, owner);

  assert(changed_id == original_id);
  assert(store.find_shape(changed_id)->version == 3);
  assert(store.delta_events().size() == 1);
  assert(store.delta_events()[0].op == GeometryDeltaOp::kUpdate);
  assert(store.delta_events()[0].old_version == 2);
  assert(store.delta_events()[0].new_version == 3);
}

void test_geometry_store_marks_and_rebuilds_dirty_lod_tiles()
{
  GeometryStore store;
  const ShapeId shape_id =
      store.add_rect(5, Rect32{0, 0, 100, 100}, OwnerRef{OwnerType::kNetWireSegment});

  assert(store.dirty_lod_tile_count() > 0);
  store.rebuild_dirty_lod_tiles();
  assert(store.dirty_lod_tile_count() == 0);
  assert(store.query_lod_tiles(0, 5, Rect32{0, 0, 100, 100}).size() == 1);

  assert(store.update_rect(shape_id, Rect32{9000, 9000, 9100, 9100}));
  assert(store.dirty_lod_tile_count() > 0);
  store.rebuild_dirty_lod_tiles();

  assert(store.query_lod_tiles(0, 5, Rect32{0, 0, 100, 100}).empty());
  assert(store.query_lod_tiles(0, 5, Rect32{9000, 9000, 9100, 9100}).size() == 1);
}

void test_geometry_store_records_delta_events_for_insert_update_and_delete()
{
  GeometryStore store;

  const ShapeId id = store.add_rect(1, Rect32{0, 0, 10, 10}, OwnerRef{OwnerType::kDie});
  assert(store.delta_events().size() == 1);
  assert(store.delta_events()[0].op == GeometryDeltaOp::kInsert);
  assert(store.delta_events()[0].shape_id == id);
  assert(store.delta_events()[0].old_version == 0);
  assert(store.delta_events()[0].new_version == 1);
  assert(store.delta_events()[0].new_bbox.hx == 10);

  assert(store.update_rect(id, Rect32{20, 30, 40, 50}, 1234));

  assert(store.delta_events().size() == 2);
  assert(store.delta_events()[1].op == GeometryDeltaOp::kUpdate);
  assert(store.delta_events()[1].command_id == 1234);
  assert(store.delta_events()[1].shape_id == id);
  assert(store.delta_events()[1].old_version == 1);
  assert(store.delta_events()[1].new_version == 2);
  assert(store.delta_events()[1].old_bbox.lx == 0);
  assert(store.delta_events()[1].new_bbox.lx == 20);

  assert(store.delete_shape(id));

  assert(store.delta_events().size() == 3);
  assert(store.delta_events()[2].op == GeometryDeltaOp::kDelete);
  assert(store.delta_events()[2].shape_id == id);
  assert(store.delta_events()[2].old_version == 2);
  assert(store.delta_events()[2].new_version == 3);
  assert(store.delta_events()[2].old_bbox.hx == 40);
  assert(store.delta_events()[2].new_bbox.hx == 40);

  store.clear_delta_events();
  assert(store.delta_events().empty());
}

void test_geometry_edit_command_carries_expected_version()
{
  GeometryEditCommand command;
  command.command_id = 99;
  command.shape_id = 7;
  command.expected_version = 3;
  command.op = GeometryEditOp::kResizeRect;
  command.requested_bbox = Rect32{0, 0, 10, 20};

  GeometryEditResult result;
  result.command_id = command.command_id;
  result.shape_id = command.shape_id;
  result.new_version = command.expected_version + 1;
  result.status = GeometryEditStatus::kAdjustedAccepted;
  result.committed_bbox = Rect32{0, 0, 12, 20};

  assert(result.command_id == 99);
  assert(result.shape_id == 7);
  assert(result.new_version == 4);
  assert(result.status == GeometryEditStatus::kAdjustedAccepted);
  assert(result.committed_bbox.hx == 12);
}

void test_geometry_edit_diagnostic_flags_round_trip()
{
  GeometryEditResult result;
  result.flags = set_geometry_edit_diagnostic(result.flags, GeometryEditDiagnostic::kUnsupportedOwner);

  assert(geometry_edit_diagnostic(result) == GeometryEditDiagnostic::kUnsupportedOwner);
  assert(geometry_edit_diagnostic_message(geometry_edit_diagnostic(result))
         == std::string("owner type is read-only for this edit path"));

  result.flags = set_geometry_edit_diagnostic(result.flags, GeometryEditDiagnostic::kUnsupportedTransform);

  assert(geometry_edit_diagnostic(result) == GeometryEditDiagnostic::kUnsupportedTransform);
  assert(geometry_edit_diagnostic_message(geometry_edit_diagnostic(result))
         == std::string("shape uses an orientation or transform unsupported by this edit path"));

  result.flags = set_geometry_edit_diagnostic(result.flags, GeometryEditDiagnostic::kNone);

  assert(geometry_edit_diagnostic(result) == GeometryEditDiagnostic::kNone);
  assert(geometry_edit_diagnostic_message(geometry_edit_diagnostic(result))[0] == '\0');
}

void test_snapshot_header_has_stable_schema_identity()
{
  GeometryFileHeader header;
  header.magic = kGeometryFileMagic;
  header.schema_version = kGeometrySchemaVersion;
  header.header_size = sizeof(GeometryFileHeader);
  header.record_size = sizeof(ShapeRecord);
  header.record_count = 123;

  assert(header.magic == kGeometryFileMagic);
  assert(header.schema_version == 1);
  assert(header.header_size == sizeof(GeometryFileHeader));
  assert(header.record_size == sizeof(ShapeRecord));
  assert(header.record_count == 123);
}

}  // namespace

int main()
{
  test_rect_normalization_and_intersection();
  test_shape_id_allocator_is_monotonic();
  test_shape_table_insert_update_and_delete();
  test_geometry_store_queries_by_shape_and_layer();
  test_geometry_store_supports_point_and_line_shapes();
  test_geometry_store_queries_alive_shapes_by_owner();
  test_geometry_store_queries_alive_shapes_by_owner_name();
  test_geometry_store_local_names_do_not_pollute_owner_name_queries();
  test_geometry_store_counts_alive_shapes_by_owner_type_and_layer();
  test_owner_type_label_includes_instance_halo();
  test_geometry_spatial_index_returns_unique_layer_candidates();
  test_geometry_spatial_index_updates_and_removes_shapes();
  test_geometry_tile_pyramid_builds_layer_lod_summaries();
  test_geometry_tile_pyramid_coarsens_parent_lod_tiles();
  test_geometry_tile_pyramid_rebuilds_dirty_tiles_only();
  test_geometry_tile_pyramid_rebuilds_dirty_tiles_from_indexed_candidates();
  test_geometry_tile_pyramid_handles_large_shapes_without_tile_explosion();
  test_geometry_store_accepts_spatial_and_lod_options();
  test_geometry_store_clear_resets_records_owners_payloads_and_shape_ids();
  test_geometry_store_preserves_shape_ids_by_owner_path_across_rebuild();
  test_geometry_store_preserves_versions_for_unchanged_rebuilt_shapes();
  test_geometry_store_marks_and_rebuilds_dirty_lod_tiles();
  test_geometry_store_records_delta_events_for_insert_update_and_delete();
  test_geometry_edit_command_carries_expected_version();
  test_geometry_edit_diagnostic_flags_round_trip();
  test_snapshot_header_has_stable_schema_identity();
  return 0;
}
