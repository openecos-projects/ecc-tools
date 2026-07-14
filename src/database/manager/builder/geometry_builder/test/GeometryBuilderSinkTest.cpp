#include "GeometrySink.h"
#include "GeometryEditApplier.h"
#include "GeometryEditJson.h"
#include "GeometryBuilder.h"
#include "GeometryNameQuery.h"
#include "GeometrySnapshotReader.h"
#include "GeometrySnapshotExporter.h"
#include "GeometrySnapshotWorkflow.h"
#include "GeometrySnapshotWriter.h"
#include "StoreGeometrySink.h"

#include "GeometryStore.h"
#include "GeometrySnapshotSchema.h"
#include "IdbDesign.h"
#include "IdbInstance.h"
#include "IdbCellMaster.h"
#include "IdbLayer.h"
#include "IdbObs.h"
#include "IdbViaMaster.h"
#include "IdbVias.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace ecc::geometry;

GeometryFileHeader read_header(const std::filesystem::path& path)
{
  std::ifstream file(path, std::ios::binary);
  GeometryFileHeader header;
  file.read(reinterpret_cast<char*>(&header), sizeof(header));
  return header;
}

std::string local_name_by_id(const GeometryStore& store, NameId name_id)
{
  if (name_id == 0 || name_id > store.name_records().size()) {
    return {};
  }

  const GeometryNameRecord& record = store.name_records()[name_id - 1];
  const size_t begin = static_cast<size_t>(record.name_offset);
  const size_t end = begin + static_cast<size_t>(record.name_size);
  if (end > store.name_payloads().size()) {
    return {};
  }

  const auto* begin_ptr = reinterpret_cast<const char*>(store.name_payloads().data() + begin);
  return std::string(begin_ptr, record.name_size);
}

std::string read_text_file(const std::filesystem::path& path)
{
  std::ifstream file(path);
  return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

std::string manifest_value(const std::filesystem::path& path, const std::string& key)
{
  std::ifstream file(path);
  std::string line;
  const std::string prefix = key + "=";
  while (std::getline(file, line)) {
    if (line.rfind(prefix, 0) == 0) {
      return line.substr(prefix.size());
    }
  }
  return {};
}

void test_geometry_edit_json_parses_resize_rect_op()
{
  const GeometryEditCommand command = parse_geometry_edit_command_json(R"json({
    "command_id": 17,
    "shape_id": 42,
    "expected_version": 3,
    "op": "resize_rect",
    "requested_bbox": {
      "lx": 44,
      "ly": 22,
      "hx": 12,
      "hy": 66
    }
  })json");

  assert(command.command_id == 17);
  assert(command.shape_id == 42);
  assert(command.expected_version == 3);
  assert(command.op == GeometryEditOp::kResizeRect);
  assert(command.requested_bbox.lx == 12);
  assert(command.requested_bbox.ly == 22);
  assert(command.requested_bbox.hx == 44);
  assert(command.requested_bbox.hy == 66);
}

void test_geometry_snapshot_workflow_skips_rebuild_for_restored_apply_edit_snapshot()
{
  const GeometrySnapshotPreparation apply_edit_with_snapshot = plan_geometry_snapshot_preparation(
      GeometrySnapshotRunMode::kApplyEdit, true, true);
  assert(apply_edit_with_snapshot.ok);
  assert(!apply_edit_with_snapshot.rebuild_from_design);

  const GeometrySnapshotPreparation apply_edit_without_snapshot = plan_geometry_snapshot_preparation(
      GeometrySnapshotRunMode::kApplyEdit, false, false);
  assert(!apply_edit_without_snapshot.ok);
  assert(!apply_edit_without_snapshot.rebuild_from_design);

  const GeometrySnapshotPreparation snapshot_with_snapshot = plan_geometry_snapshot_preparation(
      GeometrySnapshotRunMode::kSnapshot, true, true);
  assert(snapshot_with_snapshot.ok);
  assert(snapshot_with_snapshot.rebuild_from_design);
}

ShapeId find_shape_by_owner_path(const GeometryStore& store, OwnerType type, OwnerId owner_id, uint32_t path0, uint32_t path1)
{
  for (const ShapeId shape_id : store.query_owner(type, owner_id)) {
    const OwnerRef owner = store.owner_of(shape_id);
    if (owner.path0 == path0 && owner.path1 == path1) {
      return shape_id;
    }
  }

  return 0;
}

ShapeId find_shape_by_owner_path(const GeometryStore& store, OwnerType type, OwnerId owner_id, uint32_t path0, uint32_t path1,
                                 uint32_t path2)
{
  for (const ShapeId shape_id : store.query_owner(type, owner_id)) {
    const OwnerRef owner = store.owner_of(shape_id);
    if (owner.path0 == path0 && owner.path1 == path1 && owner.path2 == path2) {
      return shape_id;
    }
  }

  return 0;
}

void test_geometry_edit_applier_moves_instance_bbox_back_to_idb()
{
  idb::IdbCellMaster master;
  master.set_name("unit_master");
  master.set_width(100);
  master.set_height(40);

  idb::IdbInstance instance;
  instance.set_id(42);
  instance.set_name("u0");
  instance.set_cell_master(&master);
  instance.set_coodinate(10, 20);

  GeometryStore store;
  OwnerRef owner;
  owner.type = OwnerType::kInstanceBBox;
  owner.owner_id = instance.get_id();
  const ShapeId shape_id = store.add_rect(0, Rect32{10, 20, 110, 60}, owner);

  GeometryEditCommand command;
  command.command_id = 700;
  command.shape_id = shape_id;
  command.expected_version = 1;
  command.op = GeometryEditOp::kMoveShape;
  command.requested_bbox = Rect32{30, 50, 130, 90};

  GeometryEditApplier applier;
  const GeometryEditResult result = applier.apply_instance_bbox_edit(command, instance, store);

  assert(result.command_id == 700);
  assert(result.shape_id == shape_id);
  assert(result.status == GeometryEditStatus::kAccepted);
  assert(result.new_version == 2);
  assert(result.committed_bbox.lx == 30);
  assert(result.committed_bbox.hy == 90);

  assert(instance.get_coordinate()->get_x() == 30);
  assert(instance.get_coordinate()->get_y() == 50);
  assert(instance.get_bounding_box()->get_low_x() == 30);
  assert(instance.get_bounding_box()->get_high_y() == 90);

  const ShapeRecord* record = store.find_shape(shape_id);
  assert(record != nullptr);
  assert(record->version == 2);
  assert(record->bbox.lx == 30);
  assert(record->bbox.hy == 90);
}

void test_geometry_edit_applier_resolves_instance_from_design_owner_id()
{
  idb::IdbCellMaster master;
  master.set_name("unit_master");
  master.set_width(20);
  master.set_height(10);

  idb::IdbDesign design;
  idb::IdbInstance* instance = design.get_instance_list()->add_instance("u_design");
  instance->set_id(77);
  instance->set_cell_master(&master);
  instance->set_coodinate(5, 6);

  GeometryStore store;
  OwnerRef owner;
  owner.type = OwnerType::kInstanceBBox;
  owner.owner_id = 77;
  const ShapeId shape_id = store.add_rect(0, Rect32{5, 6, 25, 16}, owner);

  GeometryEditCommand command;
  command.command_id = 701;
  command.shape_id = shape_id;
  command.expected_version = 1;
  command.op = GeometryEditOp::kMoveShape;
  command.requested_bbox = Rect32{100, 200, 120, 210};

  GeometryEditApplier applier;
  const GeometryEditResult result = applier.apply_instance_bbox_edit(command, design, store);

  assert(result.status == GeometryEditStatus::kAccepted);
  assert(result.new_version == 2);
  assert(instance->get_coordinate()->get_x() == 100);
  assert(instance->get_coordinate()->get_y() == 200);
  assert(instance->get_bounding_box()->get_high_x() == 120);
  assert(instance->get_bounding_box()->get_high_y() == 210);
  assert(store.find_shape(shape_id)->bbox.lx == 100);
}

void test_geometry_edit_applier_reports_adjusted_instance_bbox_when_master_size_is_preserved()
{
  idb::IdbCellMaster master;
  master.set_name("unit_master");
  master.set_width(100);
  master.set_height(40);

  idb::IdbInstance instance;
  instance.set_id(43);
  instance.set_name("u_adjust");
  instance.set_cell_master(&master);
  instance.set_coodinate(10, 20);

  GeometryStore store;
  OwnerRef owner;
  owner.type = OwnerType::kInstanceBBox;
  owner.owner_id = instance.get_id();
  const ShapeId shape_id = store.add_rect(0, Rect32{10, 20, 110, 60}, owner);

  GeometryEditCommand command;
  command.command_id = 702;
  command.shape_id = shape_id;
  command.expected_version = 1;
  command.op = GeometryEditOp::kMoveShape;
  command.requested_bbox = Rect32{30, 50, 999, 999};

  GeometryEditApplier applier;
  const GeometryEditResult result = applier.apply_instance_bbox_edit(command, instance, store);

  assert(result.status == GeometryEditStatus::kAdjustedAccepted);
  assert(result.new_version == 2);
  assert(result.committed_bbox.lx == 30);
  assert(result.committed_bbox.ly == 50);
  assert(result.committed_bbox.hx == 130);
  assert(result.committed_bbox.hy == 90);
  assert(instance.get_bounding_box()->get_high_x() == 130);
  assert(store.find_shape(shape_id)->bbox.hy == 90);
}

void test_geometry_edit_applier_reports_conflict_when_shape_version_changed()
{
  idb::IdbCellMaster master;
  master.set_width(20);
  master.set_height(10);

  idb::IdbInstance instance;
  instance.set_id(44);
  instance.set_cell_master(&master);
  instance.set_coodinate(5, 6);

  GeometryStore store;
  OwnerRef owner;
  owner.type = OwnerType::kInstanceBBox;
  owner.owner_id = 44;
  const ShapeId shape_id = store.add_rect(0, Rect32{5, 6, 25, 16}, owner);
  assert(store.update_rect(shape_id, Rect32{7, 8, 27, 18}));

  GeometryEditCommand command;
  command.command_id = 703;
  command.shape_id = shape_id;
  command.expected_version = 1;
  command.op = GeometryEditOp::kMoveShape;
  command.requested_bbox = Rect32{100, 200, 120, 210};

  GeometryEditApplier applier;
  const GeometryEditResult result = applier.apply_instance_bbox_edit(command, instance, store);

  assert(result.status == GeometryEditStatus::kConflict);
  assert(result.new_version == 2);
  assert(result.committed_bbox.lx == 7);
  assert(result.committed_bbox.hy == 18);
}

void test_store_geometry_sink_emits_all_shape_kinds()
{
  GeometryStore store;
  StoreGeometrySink sink(store);

  GeometryEmitOptions rect_options;
  rect_options.flags = 7;

  const ShapeId rect_id = sink.emit_rect(4, Rect32{10, 20, 0, 5}, OwnerRef{OwnerType::kInstanceBBox}, rect_options);

  LinePayload line;
  line.begin = Point32{0, 0};
  line.end = Point32{10, 0};
  line.width = 2;
  const ShapeId line_id = sink.emit_line(5, line, OwnerRef{OwnerType::kNetWireSegment}, GeometryEmitOptions{});

  PointPayload point;
  point.point = Point32{3, 9};
  const ShapeId point_id = sink.emit_point(6, point, OwnerRef{OwnerType::kPinPortShape}, GeometryEmitOptions{});

  assert(rect_id == 1);
  assert(line_id == 2);
  assert(point_id == 3);

  const ShapeRecord* rect_record = store.find_shape(rect_id);
  const ShapeRecord* line_record = store.find_shape(line_id);
  const ShapeRecord* point_record = store.find_shape(point_id);

  assert(rect_record != nullptr);
  assert(rect_record->kind == ShapeKind::kRect);
  assert(rect_record->bbox.lx == 0);
  assert(rect_record->bbox.hy == 20);
  assert(rect_record->flags == 7);
  assert(store.owner_of(rect_id).type == OwnerType::kInstanceBBox);

  assert(line_record != nullptr);
  assert(line_record->kind == ShapeKind::kLine);
  assert(store.owner_of(line_id).type == OwnerType::kNetWireSegment);

  assert(point_record != nullptr);
  assert(point_record->kind == ShapeKind::kPoint);
  assert(store.owner_of(point_id).type == OwnerType::kPinPortShape);

  const std::vector<ShapeId> rect_hits = store.query_intersect(4, Rect32{0, 0, 1, 6});

  assert(rect_hits.size() == 1);
  assert(rect_hits[0] == rect_id);
}

void test_geometry_builder_rebuilds_basic_layout_and_instance_shapes()
{
  idb::IdbLayout layout;
  layout.initDie(0, 0, 1000, 800);

  idb::IdbSite* site = layout.get_sites()->add_site_list("core_site");
  site->set_type_core();
  site->set_width(10);
  site->set_height(20);
  layout.get_sites()->set_core_site(site);
  layout.createRow("row0", "core_site", 10, 20, idb::IdbOrient::kN_R0, 5, 1, 10, 20);

  idb::IdbDesign design(&layout);

  idb::IdbCellMaster master;
  master.set_name("unit_master");
  master.set_width(50);
  master.set_height(30);
  master.set_site(site);

  idb::IdbInstance* instance = design.get_instance_list()->add_instance("u_rebuild");
  instance->set_id(88);
  instance->set_cell_master(&master);
  instance->set_coodinate(100, 200);
  instance->set_status_placed();

  GeometryStore store;
  GeometryBuilder builder;
  const GeometryBuildResult result = builder.rebuild_from_design(design, layout, store);

  assert(result.shape_count == 4);
  assert(result.die_shape_count == 1);
  assert(result.core_shape_count == 1);
  assert(result.row_shape_count == 1);
  assert(result.instance_shape_count == 1);
  assert(store.delta_events().empty());

  const std::vector<ShapeId> die_shapes = store.query_owner(OwnerType::kDie, 0);
  const std::vector<ShapeId> core_shapes = store.query_owner(OwnerType::kCore, 0);
  const std::vector<ShapeId> row_shapes = store.query_owner(OwnerType::kRow, 0);
  const std::vector<ShapeId> inst_shapes = store.query_owner(OwnerType::kInstanceBBox, 88);

  assert(die_shapes.size() == 1);
  assert(core_shapes.size() == 1);
  assert(row_shapes.size() == 1);
  assert(inst_shapes.size() == 1);

  assert(store.find_shape(die_shapes[0])->bbox.hx == 1000);
  assert(store.find_shape(core_shapes[0])->bbox.lx == 10);
  assert(store.find_shape(row_shapes[0])->bbox.hy == 40);
  assert(store.find_shape(inst_shapes[0])->bbox.lx == 100);
  assert(store.find_shape(inst_shapes[0])->bbox.hy == 230);
  assert(local_name_by_id(store, store.owner_of(inst_shapes[0]).name_id) == "master:unit_master site:core_site");

  GeometryNameQuery name_query;
  const std::vector<ShapeId> queried_inst_shapes = name_query.query_instance_name(design, store, "u_rebuild");
  assert(queried_inst_shapes.size() == 1);
  assert(queried_inst_shapes[0] == inst_shapes[0]);

  const std::vector<ShapeId> offline_inst_shapes = store.query_owner_name("u_rebuild");
  assert(offline_inst_shapes.size() == 1);
  assert(offline_inst_shapes[0] == inst_shapes[0]);
}

void test_geometry_builder_rebuilds_track_and_gcell_grid_lines()
{
  idb::IdbLayout layout;
  layout.initDie(0, 0, 1000, 800);

  idb::IdbLayerRouting routing_layer;
  routing_layer.set_name("M1");
  routing_layer.set_id(11);
  routing_layer.set_order(1);

  idb::IdbTrackGrid* track_grid = layout.get_track_grid_list()->add_track_grid();
  idb::IdbTrack* track = track_grid->get_track();
  track->set_direction(idb::IdbTrackDirection::kDirectionX);
  track->set_start(100);
  track->set_pitch(200);
  track->set_width(2);
  track_grid->set_track_number(3);
  track_grid->add_layer_list(&routing_layer);

  idb::IdbGCellGrid* gcell_grid = layout.get_gcell_grid_list()->add_gcell_grid();
  gcell_grid->set_direction(idb::IdbTrackDirection::kDirectionY);
  gcell_grid->set_start(50);
  gcell_grid->set_space(250);
  gcell_grid->set_num(2);

  idb::IdbDesign design(&layout);
  GeometryStore store;
  GeometryBuilder builder;
  const GeometryBuildResult result = builder.rebuild_from_design(design, layout, store);

  const std::map<OwnerType, uint64_t> owner_counts = store.count_alive_shapes_by_owner_type();
  assert(owner_counts.contains(OwnerType::kTrackGrid));
  assert(owner_counts.at(OwnerType::kTrackGrid) == 3);
  assert(owner_counts.contains(OwnerType::kGCellGrid));
  assert(owner_counts.at(OwnerType::kGCellGrid) == 2);
  assert(result.track_grid_shape_count == 3);
  assert(result.gcell_grid_shape_count == 2);
  assert(result.shape_count == 6);

  const std::vector<ShapeId> track_shapes = store.query_owner(OwnerType::kTrackGrid, 0);
  const std::vector<ShapeId> gcell_shapes = store.query_owner(OwnerType::kGCellGrid, 0);
  assert(track_shapes.size() == 3);
  assert(gcell_shapes.size() == 2);

  const ShapeRecord* first_track = store.find_shape(track_shapes[0]);
  assert(first_track != nullptr);
  assert(first_track->kind == ShapeKind::kLine);
  assert(first_track->layer_id == 11);
  assert(first_track->bbox.lx == 99);
  assert(first_track->bbox.ly == -1);
  assert(first_track->bbox.hx == 101);
  assert(first_track->bbox.hy == 801);

  const OwnerRef first_track_owner = store.owner_of(track_shapes[0]);
  assert(first_track_owner.path0 == 0);
  assert(first_track_owner.path1 == 0);
  assert(first_track_owner.path2 == static_cast<uint32_t>(idb::IdbTrackDirection::kDirectionX));

  const ShapeRecord* first_gcell = store.find_shape(gcell_shapes[0]);
  assert(first_gcell != nullptr);
  assert(first_gcell->kind == ShapeKind::kLine);
  assert(first_gcell->layer_id == 0);
  assert(first_gcell->bbox.lx == -1);
  assert(first_gcell->bbox.ly == 49);
  assert(first_gcell->bbox.hx == 1001);
  assert(first_gcell->bbox.hy == 51);

  const OwnerRef first_gcell_owner = store.owner_of(gcell_shapes[0]);
  assert(first_gcell_owner.path0 == 0);
  assert(first_gcell_owner.path1 == static_cast<uint32_t>(idb::IdbTrackDirection::kDirectionY));
}

void test_geometry_builder_rebuilds_instance_halo_shapes()
{
  idb::IdbLayout layout;
  idb::IdbDesign design(&layout);

  idb::IdbCellMaster master;
  master.set_name("macro_master");
  master.set_width(100);
  master.set_height(40);
  master.set_type(idb::CellMasterType::kBlock);

  idb::IdbInstance* instance = design.get_instance_list()->add_instance("u_macro");
  instance->set_id(188);
  instance->set_cell_master(&master);
  instance->set_coodinate(10, 20);
  instance->set_status_placed();

  idb::IdbHalo* halo = instance->set_halo();
  halo->set_extend_lef(3);
  halo->set_extend_right(7);
  halo->set_extend_bottom(5);
  halo->set_extend_top(11);
  instance->set_halo_coodinate();

  GeometryStore store;
  GeometryBuilder builder;
  const GeometryBuildResult result = builder.rebuild_from_design(design, layout, store);

  const std::vector<ShapeId> halo_shapes = store.query_owner(OwnerType::kInstanceHalo, 188);
  assert(result.instance_shape_count == 1);
  assert(result.instance_halo_shape_count == 1);
  assert(halo_shapes.size() == 1);

  const ShapeRecord* halo_record = store.find_shape(halo_shapes[0]);
  assert(halo_record != nullptr);
  assert(halo_record->bbox.lx == 7);
  assert(halo_record->bbox.ly == 15);
  assert(halo_record->bbox.hx == 117);
  assert(halo_record->bbox.hy == 71);

  GeometryNameQuery name_query;
  const std::vector<ShapeId> queried_inst_shapes = name_query.query_instance_name(design, store, "u_macro");
  const std::vector<ShapeId> offline_inst_shapes = store.query_owner_name("u_macro");

  assert(queried_inst_shapes.size() == 2);
  assert(offline_inst_shapes.size() == 2);
}

void test_geometry_builder_rebuild_replaces_previous_store_contents()
{
  idb::IdbLayout layout;
  layout.initDie(0, 0, 100, 100);
  idb::IdbDesign design(&layout);

  GeometryStore store;
  GeometryBuilder builder;

  const GeometryBuildResult first = builder.rebuild_from_design(design, layout, store);
  const GeometryBuildResult second = builder.rebuild_from_design(design, layout, store);

  assert(first.shape_count == 1);
  assert(second.shape_count == 1);
  assert(store.records().size() == 1);
  assert(store.records()[0].id == 1);
}

void test_geometry_builder_rebuild_preserves_shape_ids_when_new_earlier_shapes_appear()
{
  idb::IdbLayout layout;
  idb::IdbDesign design(&layout);

  idb::IdbCellMaster master;
  master.set_name("unit_master");
  master.set_width(50);
  master.set_height(30);

  idb::IdbInstance* instance = design.get_instance_list()->add_instance("u_stable");
  instance->set_id(89);
  instance->set_cell_master(&master);
  instance->set_coodinate(100, 200);
  instance->set_status_placed();

  GeometryStore store;
  GeometryBuilder builder;
  const GeometryBuildResult first = builder.rebuild_from_design(design, layout, store);

  const std::vector<ShapeId> first_inst_shapes = store.query_owner(OwnerType::kInstanceBBox, 89);
  assert(first.shape_count == 1);
  assert(first_inst_shapes.size() == 1);
  const ShapeId stable_instance_shape = first_inst_shapes[0];

  layout.initDie(0, 0, 1000, 800);
  idb::IdbSite* site = layout.get_sites()->add_site_list("core_site");
  site->set_type_core();
  site->set_width(10);
  site->set_height(20);
  layout.get_sites()->set_core_site(site);
  layout.createRow("row0", "core_site", 10, 20, idb::IdbOrient::kN_R0, 5, 1, 10, 20);

  const GeometryBuildResult second = builder.rebuild_from_design(design, layout, store);

  const std::vector<ShapeId> second_inst_shapes = store.query_owner(OwnerType::kInstanceBBox, 89);
  const std::vector<ShapeId> die_shapes = store.query_owner(OwnerType::kDie, 0);
  assert(second.shape_count == 4);
  assert(second_inst_shapes.size() == 1);
  assert(second_inst_shapes[0] == stable_instance_shape);
  assert(die_shapes.size() == 1);
  assert(die_shapes[0] != stable_instance_shape);
}

void test_geometry_builder_uses_instance_index_owner_when_id_is_missing()
{
  idb::IdbLayout layout;
  idb::IdbDesign design(&layout);

  idb::IdbCellMaster master;
  master.set_name("unit_master");
  master.set_width(10);
  master.set_height(10);

  idb::IdbInstance* first = design.get_instance_list()->add_instance("u_missing_id_0");
  first->set_cell_master(&master);
  first->set_coodinate(0, 0);
  first->set_status_placed();

  idb::IdbInstance* second = design.get_instance_list()->add_instance("u_missing_id_1");
  second->set_cell_master(&master);
  second->set_coodinate(100, 200);
  second->set_status_placed();

  GeometryStore store;
  GeometryBuilder builder;
  builder.rebuild_from_design(design, layout, store);

  const std::vector<ShapeId> first_name_hits = store.query_owner_name("u_missing_id_0");
  const std::vector<ShapeId> second_name_hits = store.query_owner_name("u_missing_id_1");

  assert(first_name_hits.size() == 1);
  assert(second_name_hits.size() == 1);
  assert(first_name_hits[0] != second_name_hits[0]);
  assert(store.owner_of(first_name_hits[0]).owner_id == 0);
  assert(store.owner_of(second_name_hits[0]).owner_id == 1);
  assert(store.owner_of(second_name_hits[0]).path0 == 1);
}

void test_geometry_builder_skips_unplaced_instance_geometry()
{
  idb::IdbLayout layout;
  idb::IdbDesign design(&layout);

  idb::IdbCellMaster master;
  master.set_name("unit_master");
  master.set_width(10);
  master.set_height(10);

  idb::IdbInstance* instance = design.get_instance_list()->add_instance("u_unplaced");
  instance->set_cell_master(&master);
  instance->set_coodinate(100, 200);
  instance->set_status_unplaced();

  GeometryStore store;
  GeometryBuilder builder;
  const GeometryBuildResult result = builder.rebuild_from_design(design, layout, store);

  assert(result.instance_shape_count == 0);
  assert(store.query_owner_name("u_unplaced").empty());
}

void test_geometry_builder_syncs_moved_instance_without_full_rebuild()
{
  idb::IdbLayout layout;
  idb::IdbDesign design(&layout);

  idb::IdbCellMaster master;
  master.set_name("unit_master");
  master.set_width(50);
  master.set_height(30);

  idb::IdbInstance* instance = design.get_instance_list()->add_instance("u_sync");
  instance->set_id(90);
  instance->set_cell_master(&master);
  instance->set_coodinate(100, 200);
  instance->set_status_placed();

  GeometryStore store;
  GeometryBuilder builder;
  builder.rebuild_from_design(design, layout, store);

  const std::vector<ShapeId> inst_shapes = store.query_owner(OwnerType::kInstanceBBox, 90);
  assert(inst_shapes.size() == 1);
  const ShapeId shape_id = inst_shapes[0];
  assert(store.find_shape(shape_id)->version == 1);

  store.clear_delta_events();
  instance->set_coodinate(300, 400);

  const GeometrySyncResult sync = builder.sync_instance(*instance, store);

  assert(sync.ok);
  assert(sync.updated_shape_count == 1);
  assert(sync.missing_shape_count == 0);

  const std::vector<ShapeId> updated_inst_shapes = store.query_owner(OwnerType::kInstanceBBox, 90);
  assert(updated_inst_shapes.size() == 1);
  assert(updated_inst_shapes[0] == shape_id);

  const ShapeRecord* updated_record = store.find_shape(shape_id);
  assert(updated_record != nullptr);
  assert(updated_record->version == 2);
  assert(updated_record->bbox.lx == 300);
  assert(updated_record->bbox.ly == 400);
  assert(updated_record->bbox.hx == 350);
  assert(updated_record->bbox.hy == 430);

  assert(store.delta_events().size() == 1);
  assert(store.delta_events()[0].op == GeometryDeltaOp::kUpdate);
  assert(store.delta_events()[0].shape_id == shape_id);
  assert(store.delta_events()[0].old_bbox.lx == 100);
  assert(store.delta_events()[0].new_bbox.lx == 300);
}

void test_geometry_builder_syncs_moved_instance_halo_without_full_rebuild()
{
  idb::IdbLayout layout;
  idb::IdbDesign design(&layout);

  idb::IdbCellMaster master;
  master.set_name("macro_master");
  master.set_width(100);
  master.set_height(40);
  master.set_type(idb::CellMasterType::kBlock);

  idb::IdbInstance* instance = design.get_instance_list()->add_instance("u_sync_halo");
  instance->set_id(92);
  instance->set_cell_master(&master);
  instance->set_coodinate(10, 20);
  instance->set_status_placed();

  idb::IdbHalo* halo = instance->set_halo();
  halo->set_extend_lef(3);
  halo->set_extend_right(7);
  halo->set_extend_bottom(5);
  halo->set_extend_top(11);
  instance->set_halo_coodinate();

  GeometryStore store;
  GeometryBuilder builder;
  builder.rebuild_from_design(design, layout, store);

  const std::vector<ShapeId> inst_shapes = store.query_owner(OwnerType::kInstanceBBox, 92);
  const std::vector<ShapeId> halo_shapes = store.query_owner(OwnerType::kInstanceHalo, 92);
  assert(inst_shapes.size() == 1);
  assert(halo_shapes.size() == 1);

  const ShapeId inst_shape_id = inst_shapes[0];
  const ShapeId halo_shape_id = halo_shapes[0];

  store.clear_delta_events();
  instance->set_coodinate(30, 50);

  const GeometrySyncResult sync = builder.sync_instance(*instance, store);

  assert(sync.ok);
  assert(sync.updated_shape_count == 2);
  assert(sync.missing_shape_count == 0);
  assert(store.find_shape(inst_shape_id)->bbox.lx == 30);
  assert(store.find_shape(inst_shape_id)->bbox.hy == 90);
  assert(store.find_shape(halo_shape_id)->bbox.lx == 27);
  assert(store.find_shape(halo_shape_id)->bbox.ly == 45);
  assert(store.find_shape(halo_shape_id)->bbox.hx == 137);
  assert(store.find_shape(halo_shape_id)->bbox.hy == 101);
  assert(store.delta_events().size() == 2);
}

void test_geometry_builder_syncs_moved_instance_obs_without_full_rebuild()
{
  idb::IdbLayout layout;
  idb::IdbDesign design(&layout);

  idb::IdbLayerRouting routing_layer;
  routing_layer.set_name("M2");
  routing_layer.set_id(13);
  routing_layer.set_order(2);

  idb::IdbCellMaster master;
  master.set_name("obs_sync_master");
  master.set_width(40);
  master.set_height(20);
  idb::IdbObs* obs = master.add_obs();
  idb::IdbObsLayer* obs_layer = obs->add_obs_layer();
  obs_layer->get_shape()->set_layer(&routing_layer);
  obs_layer->get_shape()->add_rect(1, 2, 9, 12);

  idb::IdbInstance* instance = design.get_instance_list()->add_instance("u_sync_obs");
  instance->set_id(93);
  instance->set_cell_master(&master);
  instance->set_coodinate(100, 200);
  instance->set_status_placed();

  GeometryStore store;
  GeometryBuilder builder;
  builder.rebuild_from_design(design, layout, store);

  const std::vector<ShapeId> inst_shapes = store.query_owner(OwnerType::kInstanceBBox, 93);
  const std::vector<ShapeId> obs_shapes = store.query_owner(OwnerType::kObs, 93);
  assert(inst_shapes.size() == 1);
  assert(obs_shapes.size() == 1);
  assert(local_name_by_id(store, store.owner_of(obs_shapes[0]).name_id) == "master:obs_sync_master");

  const ShapeId inst_shape_id = inst_shapes[0];
  const ShapeId obs_shape_id = obs_shapes[0];

  store.clear_delta_events();
  instance->set_coodinate(300, 400);

  const GeometrySyncResult sync = builder.sync_instance(*instance, store);

  assert(sync.ok);
  assert(sync.updated_shape_count == 2);
  assert(sync.missing_shape_count == 0);
  assert(store.find_shape(inst_shape_id)->bbox.lx == 300);
  assert(store.find_shape(inst_shape_id)->bbox.hy == 420);
  assert(store.find_shape(obs_shape_id)->version == 2);
  assert(store.find_shape(obs_shape_id)->layer_id == 13);
  assert(store.find_shape(obs_shape_id)->bbox.lx == 301);
  assert(store.find_shape(obs_shape_id)->bbox.ly == 402);
  assert(store.find_shape(obs_shape_id)->bbox.hx == 309);
  assert(store.find_shape(obs_shape_id)->bbox.hy == 412);
  assert(store.delta_events().size() == 2);
}

void test_geometry_builder_syncs_moved_instance_pin_ports_without_full_rebuild()
{
  idb::IdbLayout layout;
  idb::IdbDesign design(&layout);

  idb::IdbLayerRouting routing_layer;
  routing_layer.set_name("M1");
  routing_layer.set_id(11);
  routing_layer.set_order(1);

  idb::IdbCellMaster master;
  master.set_name("pin_sync_master");
  master.set_width(40);
  master.set_height(20);
  idb::IdbTerm* term = master.add_term();
  term->set_name("A");
  idb::IdbPort* port = term->add_port();
  idb::IdbLayerShape* layer_shape = port->add_layer_shape();
  layer_shape->set_layer(&routing_layer);
  layer_shape->add_rect(1, 2, 9, 12);

  idb::IdbInstance* instance = design.get_instance_list()->add_instance("u_sync_pin");
  instance->set_id(94);
  instance->set_cell_master(&master);
  instance->set_coodinate(100, 200);
  instance->set_status_placed();

  GeometryStore store;
  GeometryBuilder builder;
  builder.rebuild_from_design(design, layout, store);

  const OwnerId pin_owner_id = static_cast<OwnerId>(0);
  const std::vector<ShapeId> pin_shapes = store.query_owner(OwnerType::kPinPortShape, pin_owner_id);
  assert(pin_shapes.size() == 1);
  const ShapeId pin_shape_id = pin_shapes[0];
  assert(store.find_shape(pin_shape_id)->bbox.lx == 101);
  assert(store.find_shape(pin_shape_id)->bbox.hy == 212);
  assert(local_name_by_id(store, store.owner_of(pin_shape_id).name_id) == "master:pin_sync_master");

  store.clear_delta_events();
  instance->set_coodinate(300, 400);

  const GeometrySyncResult sync = builder.sync_instance(*instance, store);

  assert(sync.ok);
  assert(sync.updated_shape_count == 2);
  assert(sync.missing_shape_count == 0);
  assert(store.find_shape(pin_shape_id)->version == 2);
  assert(store.find_shape(pin_shape_id)->layer_id == 11);
  assert(store.find_shape(pin_shape_id)->bbox.lx == 301);
  assert(store.find_shape(pin_shape_id)->bbox.ly == 402);
  assert(store.find_shape(pin_shape_id)->bbox.hx == 309);
  assert(store.find_shape(pin_shape_id)->bbox.hy == 412);
}

void test_geometry_builder_syncs_io_pin_ports_without_full_rebuild()
{
  idb::IdbLayout layout;
  idb::IdbDesign design(&layout);

  idb::IdbLayerRouting routing_layer;
  routing_layer.set_name("M1");
  routing_layer.set_id(11);
  routing_layer.set_order(1);

  idb::IdbPin* pin = design.get_io_pin_list()->add_pin_list("io0");
  pin->set_id(201);
  pin->set_as_io();
  pin->set_location(30, 40);
  idb::IdbTerm* term = pin->set_term();
  term->set_name("io0");
  term->set_placement_status_fix();
  term->set_bounding_box(0, 0, 5, 5);
  idb::IdbPort* port = term->add_port();
  idb::IdbLayerShape* layer_shape = port->add_layer_shape();
  layer_shape->set_layer(&routing_layer);
  layer_shape->add_rect(0, 0, 5, 5);
  pin->set_bounding_box();

  GeometryStore store;
  GeometryBuilder builder;
  const GeometryBuildResult rebuild = builder.rebuild_from_design(design, layout, store);

  const std::vector<ShapeId> pin_shapes = store.query_owner(OwnerType::kPinPortShape, 201);
  assert(rebuild.pin_shape_count == 1);
  assert(pin_shapes.size() == 1);
  const ShapeId pin_shape_id = pin_shapes[0];
  assert(store.find_shape(pin_shape_id)->bbox.lx == 30);
  assert(store.find_shape(pin_shape_id)->bbox.hy == 45);

  store.clear_delta_events();
  layer_shape->get_rect_list()[0]->set_rect(10, 20, 30, 40);
  pin->set_bounding_box();

  const GeometrySyncResult sync = builder.sync_io_pin(design, *pin, store);

  assert(sync.ok);
  assert(sync.updated_shape_count == 1);
  assert(sync.missing_shape_count == 0);
  assert(store.find_shape(pin_shape_id)->version == 2);
  assert(store.find_shape(pin_shape_id)->bbox.lx == 40);
  assert(store.find_shape(pin_shape_id)->bbox.ly == 60);
  assert(store.find_shape(pin_shape_id)->bbox.hx == 60);
  assert(store.find_shape(pin_shape_id)->bbox.hy == 80);
  assert(store.delta_events().size() == 1);
}

void test_geometry_builder_sync_instance_reports_missing_owner_shape()
{
  idb::IdbCellMaster master;
  master.set_name("unit_master");
  master.set_width(50);
  master.set_height(30);

  idb::IdbInstance instance;
  instance.set_id(91);
  instance.set_cell_master(&master);
  instance.set_coodinate(100, 200);

  GeometryStore store;
  GeometryBuilder builder;
  const GeometrySyncResult sync = builder.sync_instance(instance, store);

  assert(!sync.ok);
  assert(sync.updated_shape_count == 0);
  assert(sync.missing_shape_count == 1);
  assert(store.delta_events().empty());
}

void test_geometry_builder_syncs_regular_net_updates_and_adds_segments()
{
  idb::IdbLayout layout;
  idb::IdbDesign design(&layout);

  idb::IdbLayerRouting routing_layer;
  routing_layer.set_name("M1");
  routing_layer.set_id(11);
  routing_layer.set_width(4);
  routing_layer.set_direction(idb::IdbLayerDirection::kHorizontal);

  idb::IdbNet* net = design.get_net_list()->add_net("n_sync");
  net->set_id(102);
  idb::IdbRegularWire* wire = net->get_wire_list()->add_wire();
  idb::IdbRegularWireSegment* segment = wire->add_segment();
  segment->set_layer(&routing_layer);
  segment->set_layer_name("M1");
  segment->add_point(0, 50);
  segment->add_point(20, 50);

  GeometryStore store;
  GeometryBuilder builder;
  builder.rebuild_from_design(design, layout, store);

  const ShapeId original_shape = find_shape_by_owner_path(store, OwnerType::kNetWireSegment, 102, 0, 0);
  assert(original_shape != 0);

  store.clear_delta_events();
  segment->get_point_start()->set_xy(10, 70);
  segment->get_point_second()->set_xy(30, 70);

  idb::IdbRegularWireSegment* added_segment = wire->add_segment();
  added_segment->set_layer(&routing_layer);
  added_segment->set_layer_name("M1");
  added_segment->add_point(100, 10);
  added_segment->add_point(100, 40);

  const GeometrySyncResult sync = builder.sync_net(design, *net, store);

  assert(sync.ok);
  assert(sync.updated_shape_count == 1);
  assert(sync.added_shape_count == 1);
  assert(sync.deleted_shape_count == 0);
  assert(sync.missing_shape_count == 0);

  const ShapeRecord* updated_record = store.find_shape(original_shape);
  assert(updated_record != nullptr);
  assert(updated_record->version == 2);
  assert(updated_record->bbox.lx == 8);
  assert(updated_record->bbox.ly == 68);
  assert(updated_record->bbox.hx == 32);
  assert(updated_record->bbox.hy == 72);

  const ShapeId added_shape = find_shape_by_owner_path(store, OwnerType::kNetWireSegment, 102, 0, 1);
  assert(added_shape != 0);
  assert(added_shape != original_shape);
  assert(store.find_shape(added_shape)->bbox.lx == 98);
  assert(store.find_shape(added_shape)->bbox.hy == 42);

  assert(store.delta_events().size() == 2);
  assert(store.delta_events()[0].op == GeometryDeltaOp::kUpdate);
  assert(store.delta_events()[0].shape_id == original_shape);
  assert(store.delta_events()[1].op == GeometryDeltaOp::kInsert);
  assert(store.delta_events()[1].shape_id == added_shape);
}

void test_geometry_builder_syncs_regular_net_deletes_removed_segments()
{
  idb::IdbLayout layout;
  idb::IdbDesign design(&layout);

  idb::IdbLayerRouting routing_layer;
  routing_layer.set_name("M1");
  routing_layer.set_id(11);
  routing_layer.set_width(4);
  routing_layer.set_direction(idb::IdbLayerDirection::kHorizontal);

  idb::IdbNet* net = design.get_net_list()->add_net("n_delete");
  net->set_id(103);
  idb::IdbRegularWire* wire = net->get_wire_list()->add_wire();
  idb::IdbRegularWireSegment* kept_segment = wire->add_segment();
  kept_segment->set_layer(&routing_layer);
  kept_segment->set_layer_name("M1");
  kept_segment->add_point(0, 50);
  kept_segment->add_point(20, 50);

  idb::IdbRegularWireSegment* removed_segment = wire->add_segment();
  removed_segment->set_layer(&routing_layer);
  removed_segment->set_layer_name("M1");
  removed_segment->add_point(100, 10);
  removed_segment->add_point(100, 40);

  GeometryStore store;
  GeometryBuilder builder;
  builder.rebuild_from_design(design, layout, store);

  const ShapeId removed_shape = find_shape_by_owner_path(store, OwnerType::kNetWireSegment, 103, 0, 1);
  assert(removed_shape != 0);

  store.clear_delta_events();
  assert(wire->delete_seg(removed_segment));

  const GeometrySyncResult sync = builder.sync_net(design, *net, store);

  assert(sync.ok);
  assert(sync.updated_shape_count == 0);
  assert(sync.added_shape_count == 0);
  assert(sync.deleted_shape_count == 1);
  assert(sync.missing_shape_count == 0);
  assert(store.query_owner(OwnerType::kNetWireSegment, 103).size() == 1);
  assert(store.find_shape(removed_shape)->state == ShapeState::kDeleted);
  assert(store.delta_events().size() == 1);
  assert(store.delta_events()[0].op == GeometryDeltaOp::kDelete);
  assert(store.delta_events()[0].shape_id == removed_shape);
}

void test_geometry_builder_syncs_regular_net_vias_incrementally()
{
  idb::IdbLayout layout;
  idb::IdbDesign design(&layout);

  idb::IdbLayerRouting bottom_layer;
  bottom_layer.set_name("M1");
  bottom_layer.set_id(11);
  bottom_layer.set_order(1);
  bottom_layer.set_width(4);
  bottom_layer.set_direction(idb::IdbLayerDirection::kHorizontal);

  idb::IdbLayerCut cut_layer;
  cut_layer.set_name("VIA12");
  cut_layer.set_id(12);
  cut_layer.set_order(2);

  idb::IdbLayerRouting top_layer;
  top_layer.set_name("M2");
  top_layer.set_id(13);
  top_layer.set_order(3);
  top_layer.set_width(6);
  top_layer.set_direction(idb::IdbLayerDirection::kVertical);

  idb::IdbViaMaster via_master;
  via_master.set_name("VIA12");
  via_master.set_type_fixed();
  idb::IdbViaMasterFixed* bottom_fixed = via_master.add_fixed("M1");
  bottom_fixed->set_layer(&bottom_layer);
  bottom_fixed->add_rect(-5, -5, 5, 5);
  idb::IdbViaMasterFixed* cut_fixed = via_master.add_fixed("VIA12");
  cut_fixed->set_layer(&cut_layer);
  cut_fixed->add_rect(-2, -2, 2, 2);
  idb::IdbViaMasterFixed* top_fixed = via_master.add_fixed("M2");
  top_fixed->set_layer(&top_layer);
  top_fixed->add_rect(-6, -6, 6, 6);
  via_master.set_via_shape();

  idb::IdbNet* net = design.get_net_list()->add_net("n_sync_via");
  net->set_id(104);
  idb::IdbRegularWire* wire = net->get_wire_list()->add_wire();
  idb::IdbRegularWireSegment* moved_segment = wire->add_segment();
  moved_segment->set_layer(&bottom_layer);
  moved_segment->set_layer_name("M1");
  moved_segment->set_is_via(true);
  idb::IdbVia* moved_via = new idb::IdbVia();
  moved_via->set_instance_reference(&via_master);
  moved_via->set_coordinate(100, 200);
  moved_segment->set_via(moved_via);

  GeometryStore store;
  GeometryBuilder builder;
  builder.rebuild_from_design(design, layout, store);

  const std::vector<ShapeId> initial_via_shapes = store.query_owner_name("n_sync_via");
  assert(initial_via_shapes.size() == 1);
  const OwnerRef via_owner = store.owner_of(initial_via_shapes[0]);
  assert(via_owner.type == OwnerType::kVia);

  const ShapeId moved_shape = find_shape_by_owner_path(store, OwnerType::kVia, via_owner.owner_id, 0, 0, 0);
  assert(moved_shape != 0);

  store.clear_delta_events();
  moved_via->set_coordinate(120, 240);

  idb::IdbRegularWireSegment* added_segment = wire->add_segment();
  added_segment->set_layer(&bottom_layer);
  added_segment->set_layer_name("M1");
  added_segment->set_is_via(true);
  idb::IdbVia* added_via = new idb::IdbVia();
  added_via->set_instance_reference(&via_master);
  added_via->set_coordinate(500, 600);
  added_segment->set_via(added_via);

  const GeometrySyncResult sync = builder.sync_net(design, *net, store);

  assert(sync.ok);
  assert(sync.updated_shape_count == 1);
  assert(sync.added_shape_count == 1);
  assert(sync.deleted_shape_count == 0);
  assert(sync.missing_shape_count == 0);

  const ShapeRecord* moved_record = store.find_shape(moved_shape);
  assert(moved_record != nullptr);
  assert(moved_record->version == 2);
  assert(moved_record->layer_id == 12);
  assert(moved_record->bbox.lx == 118);
  assert(moved_record->bbox.hy == 242);

  const ShapeId added_shape = find_shape_by_owner_path(store, OwnerType::kVia, via_owner.owner_id, 0, 1, 0);
  assert(added_shape != 0);
  assert(added_shape != moved_shape);
  assert(store.find_shape(added_shape)->bbox.lx == 498);
  assert(store.find_shape(added_shape)->bbox.hy == 602);
}

void test_geometry_builder_syncs_special_net_updates_and_adds_segments()
{
  idb::IdbLayout layout;
  idb::IdbDesign design(&layout);

  idb::IdbLayerRouting routing_layer;
  routing_layer.set_name("M1");
  routing_layer.set_id(11);
  routing_layer.set_width(4);
  routing_layer.set_direction(idb::IdbLayerDirection::kVertical);

  idb::IdbSpecialNet* special_net = design.get_special_net_list()->add_net("VSS");
  idb::IdbSpecialWire* wire = special_net->get_wire_list()->add_wire();
  idb::IdbSpecialWireSegment* segment = wire->add_segment();
  segment->set_layer(&routing_layer);
  segment->set_route_width(6);
  segment->set_shape_type(idb::IdbWireShapeType::kStripe);
  segment->add_point(100, 10);
  segment->add_point(100, 40);
  segment->set_bounding_box();

  GeometryStore store;
  GeometryBuilder builder;
  builder.rebuild_from_design(design, layout, store);

  const ShapeId original_shape = find_shape_by_owner_path(store, OwnerType::kSpecialWireSegment, 0, 0, 0);
  assert(original_shape != 0);

  store.clear_delta_events();
  segment->get_point_start()->set_xy(110, 20);
  segment->get_point_second()->set_xy(110, 50);

  idb::IdbSpecialWireSegment* added_segment = wire->add_segment();
  added_segment->set_layer(&routing_layer);
  added_segment->set_route_width(6);
  added_segment->set_shape_type(idb::IdbWireShapeType::kStripe);
  added_segment->add_point(200, 10);
  added_segment->add_point(200, 30);

  const GeometrySyncResult sync = builder.sync_special_net(design, *special_net, store);

  assert(sync.ok);
  assert(sync.updated_shape_count == 1);
  assert(sync.added_shape_count == 1);
  assert(sync.deleted_shape_count == 0);
  assert(sync.missing_shape_count == 0);

  const ShapeRecord* updated_record = store.find_shape(original_shape);
  assert(updated_record != nullptr);
  assert(updated_record->version == 2);
  assert(updated_record->bbox.lx == 107);
  assert(updated_record->bbox.ly == 20);
  assert(updated_record->bbox.hx == 113);
  assert(updated_record->bbox.hy == 50);

  const ShapeId added_shape = find_shape_by_owner_path(store, OwnerType::kSpecialWireSegment, 0, 0, 1);
  assert(added_shape != 0);
  assert(store.find_shape(added_shape)->bbox.lx == 197);
  assert(store.find_shape(added_shape)->bbox.hy == 30);
}

void test_geometry_builder_syncs_special_net_vias_incrementally()
{
  idb::IdbLayout layout;
  idb::IdbDesign design(&layout);

  idb::IdbLayerRouting bottom_layer;
  bottom_layer.set_name("M1");
  bottom_layer.set_id(11);
  bottom_layer.set_order(1);
  bottom_layer.set_width(4);
  bottom_layer.set_direction(idb::IdbLayerDirection::kHorizontal);

  idb::IdbLayerCut cut_layer;
  cut_layer.set_name("VIA12");
  cut_layer.set_id(12);
  cut_layer.set_order(2);

  idb::IdbLayerRouting top_layer;
  top_layer.set_name("M2");
  top_layer.set_id(13);
  top_layer.set_order(3);
  top_layer.set_width(6);
  top_layer.set_direction(idb::IdbLayerDirection::kVertical);

  idb::IdbViaMaster via_master;
  via_master.set_name("VIA12");
  via_master.set_type_fixed();
  idb::IdbViaMasterFixed* bottom_fixed = via_master.add_fixed("M1");
  bottom_fixed->set_layer(&bottom_layer);
  bottom_fixed->add_rect(-5, -5, 5, 5);
  idb::IdbViaMasterFixed* cut_fixed = via_master.add_fixed("VIA12");
  cut_fixed->set_layer(&cut_layer);
  cut_fixed->add_rect(-2, -2, 2, 2);
  idb::IdbViaMasterFixed* top_fixed = via_master.add_fixed("M2");
  top_fixed->set_layer(&top_layer);
  top_fixed->add_rect(-6, -6, 6, 6);
  via_master.set_via_shape();

  idb::IdbSpecialNet* special_net = design.get_special_net_list()->add_net("VSS_SYNC_VIA");
  idb::IdbSpecialWire* wire = special_net->get_wire_list()->add_wire();
  idb::IdbSpecialWireSegment* moved_segment = wire->add_segment();
  moved_segment->set_layer(&bottom_layer);
  moved_segment->set_is_via(true);
  idb::IdbVia* moved_via = new idb::IdbVia();
  moved_via->set_instance_reference(&via_master);
  moved_via->set_coordinate(100, 200);
  moved_segment->set_via(moved_via);
  moved_segment->set_bounding_box();

  GeometryStore store;
  GeometryBuilder builder;
  builder.rebuild_from_design(design, layout, store);

  const std::vector<ShapeId> initial_via_shapes = store.query_owner_name("VSS_SYNC_VIA");
  assert(initial_via_shapes.size() == 1);
  const OwnerRef via_owner = store.owner_of(initial_via_shapes[0]);
  assert(via_owner.type == OwnerType::kVia);

  const ShapeId moved_shape = find_shape_by_owner_path(store, OwnerType::kVia, via_owner.owner_id, 0, 0, 0);
  assert(moved_shape != 0);

  store.clear_delta_events();
  moved_via->set_coordinate(120, 240);
  moved_segment->set_bounding_box();

  idb::IdbSpecialWireSegment* added_segment = wire->add_segment();
  added_segment->set_layer(&bottom_layer);
  added_segment->set_is_via(true);
  idb::IdbVia* added_via = new idb::IdbVia();
  added_via->set_instance_reference(&via_master);
  added_via->set_coordinate(500, 600);
  added_segment->set_via(added_via);
  added_segment->set_bounding_box();

  const GeometrySyncResult sync = builder.sync_special_net(design, *special_net, store);

  assert(sync.ok);
  assert(sync.updated_shape_count == 1);
  assert(sync.added_shape_count == 1);
  assert(sync.deleted_shape_count == 0);
  assert(sync.missing_shape_count == 0);
  assert(store.query_owner(OwnerType::kSpecialWireSegment, 0).empty());

  const ShapeRecord* moved_record = store.find_shape(moved_shape);
  assert(moved_record != nullptr);
  assert(moved_record->version == 2);
  assert(moved_record->layer_id == 12);
  assert(moved_record->bbox.lx == 118);
  assert(moved_record->bbox.hy == 242);

  const ShapeId added_shape = find_shape_by_owner_path(store, OwnerType::kVia, via_owner.owner_id, 0, 1, 0);
  assert(added_shape != 0);
  assert(added_shape != moved_shape);
  assert(store.find_shape(added_shape)->bbox.lx == 498);
  assert(store.find_shape(added_shape)->bbox.hy == 602);
}

void test_geometry_builder_syncs_blockage_rects_incrementally()
{
  idb::IdbLayout layout;
  idb::IdbDesign design(&layout);

  idb::IdbLayerRouting routing_layer;
  routing_layer.set_name("M1");
  routing_layer.set_id(11);

  idb::IdbRoutingBlockage* blockage = design.get_blockage_list()->add_blockage_routing("M1");
  blockage->set_layer(&routing_layer);
  blockage->add_rect(10, 20, 30, 40);

  GeometryStore store;
  GeometryBuilder builder;
  builder.rebuild_from_design(design, layout, store);

  const ShapeId original_shape = find_shape_by_owner_path(store, OwnerType::kBlockage, 0, 0, 0);
  assert(original_shape != 0);

  std::vector<idb::IdbRect*> rects = blockage->get_rect_list();
  assert(rects.size() == 1);

  store.clear_delta_events();
  rects[0]->set_rect(12, 22, 34, 44);
  blockage->add_rect(100, 110, 130, 140);

  const GeometrySyncResult update_sync = builder.sync_blockage(design, *blockage, store);

  assert(update_sync.ok);
  assert(update_sync.updated_shape_count == 1);
  assert(update_sync.added_shape_count == 1);
  assert(update_sync.deleted_shape_count == 0);
  assert(update_sync.missing_shape_count == 0);

  const ShapeRecord* updated_record = store.find_shape(original_shape);
  assert(updated_record != nullptr);
  assert(updated_record->version == 2);
  assert(updated_record->layer_id == 11);
  assert(updated_record->bbox.lx == 12);
  assert(updated_record->bbox.ly == 22);
  assert(updated_record->bbox.hx == 34);
  assert(updated_record->bbox.hy == 44);

  const ShapeId added_shape = find_shape_by_owner_path(store, OwnerType::kBlockage, 0, 1, 0);
  assert(added_shape != 0);
  assert(added_shape != original_shape);
  assert(store.find_shape(added_shape)->bbox.lx == 100);
  assert(store.find_shape(added_shape)->bbox.hy == 140);
  assert(store.delta_events().size() == 2);
  assert(store.delta_events()[0].op == GeometryDeltaOp::kUpdate);
  assert(store.delta_events()[0].shape_id == original_shape);
  assert(store.delta_events()[1].op == GeometryDeltaOp::kInsert);
  assert(store.delta_events()[1].shape_id == added_shape);

  store.clear_delta_events();
  rects = blockage->get_rect_list();
  assert(rects.size() == 2);
  rects.pop_back();
  blockage->set_rect_list(rects);

  const GeometrySyncResult delete_sync = builder.sync_blockage(design, *blockage, store);

  assert(delete_sync.ok);
  assert(delete_sync.updated_shape_count == 0);
  assert(delete_sync.added_shape_count == 0);
  assert(delete_sync.deleted_shape_count == 1);
  assert(delete_sync.missing_shape_count == 0);
  assert(store.find_shape(original_shape)->state == ShapeState::kAlive);
  assert(store.find_shape(added_shape)->state == ShapeState::kDeleted);
  assert(store.delta_events().size() == 1);
  assert(store.delta_events()[0].op == GeometryDeltaOp::kDelete);
  assert(store.delta_events()[0].shape_id == added_shape);
}

void test_geometry_builder_syncs_region_boundary_rects_incrementally()
{
  idb::IdbLayout layout;
  idb::IdbDesign design(&layout);

  idb::IdbRegion* region = design.get_region_list()->add_region("region0");
  region->add_boundary(10, 20, 30, 40);

  GeometryStore store;
  GeometryBuilder builder;
  builder.rebuild_from_design(design, layout, store);

  const ShapeId original_shape = find_shape_by_owner_path(store, OwnerType::kRegion, 0, 0, 0);
  assert(original_shape != 0);

  store.clear_delta_events();
  region->get_boundary()[0]->set_rect(12, 22, 34, 44);
  region->add_boundary(100, 110, 130, 140);

  const GeometrySyncResult update_sync = builder.sync_region(design, *region, store);

  assert(update_sync.ok);
  assert(update_sync.updated_shape_count == 1);
  assert(update_sync.added_shape_count == 1);
  assert(update_sync.deleted_shape_count == 0);
  assert(update_sync.missing_shape_count == 0);

  const ShapeRecord* updated_record = store.find_shape(original_shape);
  assert(updated_record != nullptr);
  assert(updated_record->version == 2);
  assert(updated_record->layer_id == 0);
  assert(updated_record->bbox.lx == 12);
  assert(updated_record->bbox.ly == 22);
  assert(updated_record->bbox.hx == 34);
  assert(updated_record->bbox.hy == 44);

  const ShapeId added_shape = find_shape_by_owner_path(store, OwnerType::kRegion, 0, 1, 0);
  assert(added_shape != 0);
  assert(added_shape != original_shape);
  assert(store.find_shape(added_shape)->bbox.lx == 100);
  assert(store.find_shape(added_shape)->bbox.hy == 140);
  assert(store.delta_events().size() == 2);
  assert(store.delta_events()[0].op == GeometryDeltaOp::kUpdate);
  assert(store.delta_events()[0].shape_id == original_shape);
  assert(store.delta_events()[1].op == GeometryDeltaOp::kInsert);
  assert(store.delta_events()[1].shape_id == added_shape);

  store.clear_delta_events();
  region->get_boundary().pop_back();

  const GeometrySyncResult delete_sync = builder.sync_region(design, *region, store);

  assert(delete_sync.ok);
  assert(delete_sync.updated_shape_count == 0);
  assert(delete_sync.added_shape_count == 0);
  assert(delete_sync.deleted_shape_count == 1);
  assert(delete_sync.missing_shape_count == 0);
  assert(store.find_shape(original_shape)->state == ShapeState::kAlive);
  assert(store.find_shape(added_shape)->state == ShapeState::kDeleted);
  assert(store.delta_events().size() == 1);
  assert(store.delta_events()[0].op == GeometryDeltaOp::kDelete);
  assert(store.delta_events()[0].shape_id == added_shape);
}

void test_geometry_builder_syncs_slot_rects_incrementally()
{
  idb::IdbLayout layout;
  idb::IdbDesign design(&layout);

  idb::IdbLayerRouting routing_layer;
  routing_layer.set_name("M1");
  routing_layer.set_id(11);

  idb::IdbSlot* slot = design.get_slot_list()->add_slot();
  slot->set_layer(&routing_layer);
  slot->add_rect(10, 20, 30, 40);

  GeometryStore store;
  GeometryBuilder builder;
  builder.rebuild_from_design(design, layout, store);

  const ShapeId original_shape = find_shape_by_owner_path(store, OwnerType::kSlot, 0, 0, 0);
  assert(original_shape != 0);

  store.clear_delta_events();
  slot->get_rect_list()[0]->set_rect(12, 22, 34, 44);
  slot->add_rect(100, 110, 130, 140);

  const GeometrySyncResult update_sync = builder.sync_slot(design, *slot, store);

  assert(update_sync.ok);
  assert(update_sync.updated_shape_count == 1);
  assert(update_sync.added_shape_count == 1);
  assert(update_sync.deleted_shape_count == 0);
  assert(update_sync.missing_shape_count == 0);

  const ShapeRecord* updated_record = store.find_shape(original_shape);
  assert(updated_record != nullptr);
  assert(updated_record->version == 2);
  assert(updated_record->layer_id == 11);
  assert(updated_record->bbox.lx == 12);
  assert(updated_record->bbox.ly == 22);
  assert(updated_record->bbox.hx == 34);
  assert(updated_record->bbox.hy == 44);

  const ShapeId added_shape = find_shape_by_owner_path(store, OwnerType::kSlot, 0, 1, 0);
  assert(added_shape != 0);
  assert(added_shape != original_shape);
  assert(store.find_shape(added_shape)->bbox.lx == 100);
  assert(store.find_shape(added_shape)->bbox.hy == 140);
  assert(store.delta_events().size() == 2);
  assert(store.delta_events()[0].op == GeometryDeltaOp::kUpdate);
  assert(store.delta_events()[0].shape_id == original_shape);
  assert(store.delta_events()[1].op == GeometryDeltaOp::kInsert);
  assert(store.delta_events()[1].shape_id == added_shape);

  store.clear_delta_events();
  slot->get_rect_list().pop_back();

  const GeometrySyncResult delete_sync = builder.sync_slot(design, *slot, store);

  assert(delete_sync.ok);
  assert(delete_sync.updated_shape_count == 0);
  assert(delete_sync.added_shape_count == 0);
  assert(delete_sync.deleted_shape_count == 1);
  assert(delete_sync.missing_shape_count == 0);
  assert(store.find_shape(original_shape)->state == ShapeState::kAlive);
  assert(store.find_shape(added_shape)->state == ShapeState::kDeleted);
  assert(store.delta_events().size() == 1);
  assert(store.delta_events()[0].op == GeometryDeltaOp::kDelete);
  assert(store.delta_events()[0].shape_id == added_shape);
}

void test_geometry_builder_syncs_layer_fill_rects_incrementally()
{
  idb::IdbLayout layout;
  idb::IdbDesign design(&layout);

  idb::IdbLayerRouting routing_layer;
  routing_layer.set_name("M1");
  routing_layer.set_id(11);

  idb::IdbFillLayer* fill_layer = design.get_fill_list()->add_fill_layer(&routing_layer);
  fill_layer->add_rect(10, 20, 30, 40);

  GeometryStore store;
  GeometryBuilder builder;
  builder.rebuild_from_design(design, layout, store);

  const ShapeId original_shape = find_shape_by_owner_path(store, OwnerType::kFill, 0, 0, 0);
  assert(original_shape != 0);

  store.clear_delta_events();
  fill_layer->get_rect_list()[0]->set_rect(12, 22, 34, 44);
  fill_layer->add_rect(100, 110, 130, 140);

  idb::IdbFill* fill = design.get_fill_list()->get_fill_list()[0];
  const GeometrySyncResult update_sync = builder.sync_fill(design, *fill, store);

  assert(update_sync.ok);
  assert(update_sync.updated_shape_count == 1);
  assert(update_sync.added_shape_count == 1);
  assert(update_sync.deleted_shape_count == 0);
  assert(update_sync.missing_shape_count == 0);

  const ShapeRecord* updated_record = store.find_shape(original_shape);
  assert(updated_record != nullptr);
  assert(updated_record->version == 2);
  assert(updated_record->layer_id == 11);
  assert(updated_record->bbox.lx == 12);
  assert(updated_record->bbox.ly == 22);
  assert(updated_record->bbox.hx == 34);
  assert(updated_record->bbox.hy == 44);

  const ShapeId added_shape = find_shape_by_owner_path(store, OwnerType::kFill, 0, 1, 0);
  assert(added_shape != 0);
  assert(added_shape != original_shape);
  assert(store.find_shape(added_shape)->bbox.lx == 100);
  assert(store.find_shape(added_shape)->bbox.hy == 140);
  assert(store.delta_events().size() == 2);
  assert(store.delta_events()[0].op == GeometryDeltaOp::kUpdate);
  assert(store.delta_events()[0].shape_id == original_shape);
  assert(store.delta_events()[1].op == GeometryDeltaOp::kInsert);
  assert(store.delta_events()[1].shape_id == added_shape);

  store.clear_delta_events();
  fill_layer->get_rect_list().pop_back();

  const GeometrySyncResult delete_sync = builder.sync_fill(design, *fill, store);

  assert(delete_sync.ok);
  assert(delete_sync.updated_shape_count == 0);
  assert(delete_sync.added_shape_count == 0);
  assert(delete_sync.deleted_shape_count == 1);
  assert(delete_sync.missing_shape_count == 0);
  assert(store.find_shape(original_shape)->state == ShapeState::kAlive);
  assert(store.find_shape(added_shape)->state == ShapeState::kDeleted);
  assert(store.delta_events().size() == 1);
  assert(store.delta_events()[0].op == GeometryDeltaOp::kDelete);
  assert(store.delta_events()[0].shape_id == added_shape);
}

void test_geometry_builder_rebuilds_def_rect_and_wire_shapes()
{
  idb::IdbLayout layout;
  idb::IdbDesign design(&layout);

  idb::IdbLayerRouting routing_layer;
  routing_layer.set_name("M1");
  routing_layer.set_id(11);
  routing_layer.set_width(4);
  routing_layer.set_direction(idb::IdbLayerDirection::kHorizontal);

  idb::IdbNet* net = design.get_net_list()->add_net("n0");
  net->set_id(101);
  idb::IdbRegularWireSegment* regular_segment = net->get_wire_list()->add_wire()->add_segment();
  regular_segment->set_layer(&routing_layer);
  regular_segment->set_layer_name("M1");
  regular_segment->add_point(0, 50);
  regular_segment->add_point(20, 50);

  idb::IdbSpecialNet* special_net = design.get_special_net_list()->add_net("VDD");
  idb::IdbSpecialWireSegment* special_segment = special_net->get_wire_list()->add_wire()->add_segment();
  special_segment->set_layer(&routing_layer);
  special_segment->set_route_width(6);
  special_segment->set_shape_type(idb::IdbWireShapeType::kStripe);
  special_segment->add_point(100, 10);
  special_segment->add_point(100, 40);
  special_segment->set_bounding_box();

  idb::IdbRoutingBlockage* blockage = design.get_blockage_list()->add_blockage_routing("M1");
  blockage->set_layer(&routing_layer);
  blockage->add_rect(1, 2, 3, 4);

  idb::IdbFillLayer* fill_layer = design.get_fill_list()->add_fill_layer(&routing_layer);
  fill_layer->add_rect(5, 6, 7, 8);

  idb::IdbSlot* slot = design.get_slot_list()->add_slot();
  slot->set_layer(&routing_layer);
  slot->add_rect(9, 10, 11, 12);

  idb::IdbRegion* region = design.get_region_list()->add_region("region0");
  region->add_boundary(13, 14, 15, 16);

  idb::IdbPin* pin = design.get_io_pin_list()->add_pin_list("io0");
  pin->set_id(201);
  pin->set_as_io();
  pin->set_location(30, 40);
  idb::IdbTerm* term = pin->set_term();
  term->set_name("io0");
  term->set_placement_status_fix();
  term->set_bounding_box(0, 0, 5, 5);
  idb::IdbPort* port = term->add_port();
  idb::IdbLayerShape* pin_shape = port->add_layer_shape();
  pin_shape->set_layer(&routing_layer);
  pin_shape->add_rect(0, 0, 5, 5);
  pin->set_bounding_box();

  GeometryStore store;
  GeometryBuilder builder;
  const GeometryBuildResult result = builder.rebuild_from_design(design, layout, store);

  assert(result.net_wire_shape_count == 1);
  assert(result.special_net_wire_shape_count == 1);
  assert(result.blockage_shape_count == 1);
  assert(result.fill_shape_count == 1);
  assert(result.slot_shape_count == 1);
  assert(result.region_shape_count == 1);
  assert(result.pin_shape_count == 1);

  const std::vector<ShapeId> net_shapes = store.query_owner(OwnerType::kNetWireSegment, 101);
  const std::vector<ShapeId> special_shapes = store.query_owner(OwnerType::kSpecialWireSegment, 0);
  const std::vector<ShapeId> blockage_shapes = store.query_owner(OwnerType::kBlockage, 0);
  const std::vector<ShapeId> fill_shapes = store.query_owner(OwnerType::kFill, 0);
  const std::vector<ShapeId> slot_shapes = store.query_owner(OwnerType::kSlot, 0);
  const std::vector<ShapeId> region_shapes = store.query_owner(OwnerType::kRegion, 0);
  const std::vector<ShapeId> pin_shapes = store.query_owner(OwnerType::kPinPortShape, 201);

  assert(net_shapes.size() == 1);
  assert(special_shapes.size() == 1);
  assert(blockage_shapes.size() == 1);
  assert(fill_shapes.size() == 1);
  assert(slot_shapes.size() == 1);
  assert(region_shapes.size() == 1);
  assert(pin_shapes.size() == 1);

  assert(store.find_shape(net_shapes[0])->layer_id == 11);
  assert(store.find_shape(net_shapes[0])->bbox.lx == -2);
  assert(store.find_shape(net_shapes[0])->bbox.hy == 52);
  assert(store.owner_of(net_shapes[0]).path0 == 0);
  assert(store.owner_of(net_shapes[0]).path1 == 0);

  assert(store.find_shape(special_shapes[0])->layer_id == 11);
  assert(store.find_shape(special_shapes[0])->bbox.lx == 97);
  assert(store.find_shape(special_shapes[0])->bbox.hy == 40);
  assert(store.owner_of(special_shapes[0]).path0 == 0);
  assert(store.owner_of(special_shapes[0]).path1 == 0);

  assert(store.find_shape(blockage_shapes[0])->bbox.ly == 2);
  assert(store.find_shape(fill_shapes[0])->bbox.hx == 7);
  assert(store.find_shape(slot_shapes[0])->bbox.lx == 9);
  assert(store.find_shape(region_shapes[0])->layer_id == 0);
  assert(store.find_shape(pin_shapes[0])->layer_id == 11);
  assert(store.find_shape(pin_shapes[0])->bbox.lx == 30);
  assert(store.find_shape(pin_shapes[0])->bbox.hy == 45);

  GeometryNameQuery name_query;
  const std::vector<ShapeId> queried_net_shapes = name_query.query_net_name(design, store, "n0");
  const std::vector<ShapeId> queried_special_shapes = name_query.query_net_name(design, store, "VDD");
  const std::vector<ShapeId> offline_net_shapes = store.query_owner_name("n0");
  const std::vector<ShapeId> offline_special_shapes = store.query_owner_name("VDD");
  const std::vector<ShapeId> offline_pin_shapes = store.query_owner_name("io0");
  const std::vector<ShapeId> offline_region_shapes = store.query_owner_name("region0");

  assert(queried_net_shapes.size() == 1);
  assert(queried_net_shapes[0] == net_shapes[0]);
  assert(queried_special_shapes.size() == 1);
  assert(queried_special_shapes[0] == special_shapes[0]);
  assert(offline_net_shapes.size() == 1);
  assert(offline_net_shapes[0] == net_shapes[0]);
  assert(offline_special_shapes.size() == 1);
  assert(offline_special_shapes[0] == special_shapes[0]);
  assert(offline_pin_shapes.size() == 1);
  assert(offline_pin_shapes[0] == pin_shapes[0]);
  assert(offline_region_shapes.size() == 1);
  assert(offline_region_shapes[0] == region_shapes[0]);
}

void test_geometry_builder_rebuilds_wire_vias_and_instance_obs_shapes()
{
  idb::IdbLayout layout;
  idb::IdbDesign design(&layout);

  idb::IdbLayerRouting bottom_layer;
  bottom_layer.set_name("M1");
  bottom_layer.set_id(11);
  bottom_layer.set_order(1);
  bottom_layer.set_width(4);
  bottom_layer.set_direction(idb::IdbLayerDirection::kHorizontal);

  idb::IdbLayerCut cut_layer;
  cut_layer.set_name("VIA12");
  cut_layer.set_id(12);
  cut_layer.set_order(2);

  idb::IdbLayerRouting top_layer;
  top_layer.set_name("M2");
  top_layer.set_id(13);
  top_layer.set_order(3);
  top_layer.set_width(6);
  top_layer.set_direction(idb::IdbLayerDirection::kVertical);

  idb::IdbViaMaster via_master;
  via_master.set_name("VIA12");
  via_master.set_type_fixed();
  idb::IdbViaMasterFixed* bottom_fixed = via_master.add_fixed("M1");
  bottom_fixed->set_layer(&bottom_layer);
  bottom_fixed->add_rect(-5, -5, 5, 5);
  idb::IdbViaMasterFixed* cut_fixed = via_master.add_fixed("VIA12");
  cut_fixed->set_layer(&cut_layer);
  cut_fixed->add_rect(-2, -2, 2, 2);
  idb::IdbViaMasterFixed* top_fixed = via_master.add_fixed("M2");
  top_fixed->set_layer(&top_layer);
  top_fixed->add_rect(-6, -6, 6, 6);
  via_master.set_via_shape();

  idb::IdbNet* net = design.get_net_list()->add_net("n_via");
  net->set_id(301);
  idb::IdbRegularWireSegment* regular_via_segment = net->get_wire_list()->add_wire()->add_segment();
  regular_via_segment->set_layer(&bottom_layer);
  regular_via_segment->set_layer_name("M1");
  regular_via_segment->set_is_via(true);
  idb::IdbVia* regular_via = new idb::IdbVia();
  regular_via->set_instance_reference(&via_master);
  regular_via->set_coordinate(100, 200);
  regular_via_segment->set_via(regular_via);

  idb::IdbSpecialNet* special_net = design.get_special_net_list()->add_net("VSS");
  idb::IdbSpecialWireSegment* special_via_segment = special_net->get_wire_list()->add_wire()->add_segment();
  special_via_segment->set_layer(&bottom_layer);
  special_via_segment->set_is_via(true);
  idb::IdbVia* special_via = new idb::IdbVia();
  special_via->set_instance_reference(&via_master);
  special_via->set_coordinate(300, 400);
  special_via_segment->set_via(special_via);
  special_via_segment->set_bounding_box();

  idb::IdbVia* fill_via_template = new idb::IdbVia();
  fill_via_template->set_instance_reference(&via_master);
  idb::IdbFillVia* fill_via = design.get_fill_list()->add_fill_via(fill_via_template);
  fill_via->add_coordinate(500, 600);

  idb::IdbPin* pin = design.get_io_pin_list()->add_pin_list("io_via");
  pin->set_id(501);
  idb::IdbVia* pin_via = new idb::IdbVia();
  pin_via->set_instance_reference(&via_master);
  pin_via->set_coordinate(700, 800);
  pin->get_via_list().push_back(pin_via);

  idb::IdbCellMaster master;
  master.set_name("obs_master");
  master.set_width(40);
  master.set_height(20);
  idb::IdbObs* obs = master.add_obs();
  idb::IdbObsLayer* obs_layer = obs->add_obs_layer();
  obs_layer->get_shape()->set_layer(&top_layer);
  obs_layer->get_shape()->add_rect(1, 2, 9, 12);

  idb::IdbInstance* instance = design.get_instance_list()->add_instance("u_obs");
  instance->set_id(401);
  instance->set_cell_master(&master);
  instance->set_status_placed();
  instance->set_coodinate(1000, 2000);

  GeometryStore store;
  GeometryBuilder builder;
  const GeometryBuildResult result = builder.rebuild_from_design(design, layout, store);

  const std::map<OwnerType, uint64_t> owner_counts = store.count_alive_shapes_by_owner_type();
  assert(owner_counts.contains(OwnerType::kVia));
  assert(owner_counts.at(OwnerType::kVia) == 3);
  assert(owner_counts.contains(OwnerType::kObs));
  assert(owner_counts.at(OwnerType::kObs) == 1);
  assert(owner_counts.contains(OwnerType::kFill));
  assert(owner_counts.at(OwnerType::kFill) == 1);
  assert(result.via_shape_count == 3);
  assert(result.fill_shape_count == 1);
  assert(result.shape_count >= 6);

  const std::vector<ShapeId> regular_via_shapes = store.query_owner_name("n_via");
  const std::vector<ShapeId> special_via_shapes = store.query_owner_name("VSS");
  const std::vector<ShapeId> pin_via_shapes = store.query_owner_name("io_via");
  const std::vector<ShapeId> instance_shapes = store.query_owner_name("u_obs");

  assert(regular_via_shapes.size() == 1);
  assert(special_via_shapes.size() == 1);
  assert(pin_via_shapes.size() == 1);
  assert(instance_shapes.size() == 2);

  const ShapeRecord* regular_via_record = store.find_shape(regular_via_shapes[0]);
  const ShapeRecord* special_via_record = store.find_shape(special_via_shapes[0]);
  const ShapeRecord* pin_via_record = store.find_shape(pin_via_shapes[0]);
  assert(regular_via_record != nullptr);
  assert(special_via_record != nullptr);
  assert(pin_via_record != nullptr);
  assert(regular_via_record->layer_id == 12);
  assert(regular_via_record->bbox.lx == 98);
  assert(regular_via_record->bbox.hy == 202);
  assert(store.owner_of(regular_via_shapes[0]).type == OwnerType::kVia);
  assert(store.owner_of(regular_via_shapes[0]).name_id != 0);
  assert(special_via_record->layer_id == 12);
  assert(special_via_record->bbox.lx == 298);
  assert(special_via_record->bbox.hy == 402);
  assert(store.owner_of(special_via_shapes[0]).name_id != 0);
  assert(pin_via_record->layer_id == 12);
  assert(pin_via_record->bbox.lx == 698);
  assert(pin_via_record->bbox.hy == 802);
  assert(store.owner_of(pin_via_shapes[0]).type == OwnerType::kVia);
  assert(store.owner_of(pin_via_shapes[0]).name_id != 0);

  const std::vector<ShapeId> fill_shapes = store.query_owner(OwnerType::kFill, 0);
  assert(fill_shapes.size() == 1);
  const ShapeRecord* fill_via_record = store.find_shape(fill_shapes[0]);
  assert(fill_via_record != nullptr);
  assert(fill_via_record->layer_id == 12);
  assert(fill_via_record->bbox.lx == 498);
  assert(fill_via_record->bbox.hy == 602);
  assert(store.owner_of(fill_shapes[0]).type == OwnerType::kFill);
  assert(store.owner_of(fill_shapes[0]).path0 == 0);
  assert(store.owner_of(fill_shapes[0]).name_id != 0);

  const std::vector<ShapeId> obs_shapes = store.query_owner(OwnerType::kObs, 401);
  assert(obs_shapes.size() == 1);
  assert(store.find_shape(obs_shapes[0])->layer_id == 13);
  assert(store.find_shape(obs_shapes[0])->bbox.lx == 1001);
  assert(store.find_shape(obs_shapes[0])->bbox.ly == 2002);
  assert(store.find_shape(obs_shapes[0])->bbox.hx == 1009);
  assert(store.find_shape(obs_shapes[0])->bbox.hy == 2012);
}

void test_geometry_edit_applier_moves_regular_net_wire_segment_back_to_idb()
{
  idb::IdbLayout layout;
  idb::IdbDesign design(&layout);

  idb::IdbLayerRouting routing_layer;
  routing_layer.set_name("M1");
  routing_layer.set_id(11);
  routing_layer.set_width(4);
  routing_layer.set_direction(idb::IdbLayerDirection::kHorizontal);

  idb::IdbNet* net = design.get_net_list()->add_net("n0");
  net->set_id(101);
  idb::IdbRegularWireSegment* segment = net->get_wire_list()->add_wire()->add_segment();
  segment->set_layer(&routing_layer);
  segment->set_layer_name("M1");
  segment->add_point(0, 50);
  segment->add_point(20, 50);

  GeometryStore store;
  GeometryBuilder builder;
  builder.rebuild_from_design(design, layout, store);

  const std::vector<ShapeId> net_shapes = store.query_owner(OwnerType::kNetWireSegment, 101);
  assert(net_shapes.size() == 1);

  GeometryEditCommand command;
  command.command_id = 801;
  command.shape_id = net_shapes[0];
  command.expected_version = 1;
  command.op = GeometryEditOp::kMoveShape;
  command.requested_bbox = Rect32{8, 58, 32, 62};

  GeometryEditApplier applier;
  const GeometryEditResult result = applier.apply_edit(command, design, store);

  assert(result.command_id == 801);
  assert(result.shape_id == net_shapes[0]);
  assert(result.status == GeometryEditStatus::kAccepted);
  assert(result.new_version == 2);
  assert(result.committed_bbox.lx == 8);
  assert(result.committed_bbox.ly == 58);
  assert(result.committed_bbox.hx == 32);
  assert(result.committed_bbox.hy == 62);

  assert(segment->get_point_start()->get_x() == 10);
  assert(segment->get_point_start()->get_y() == 60);
  assert(segment->get_point_second()->get_x() == 30);
  assert(segment->get_point_second()->get_y() == 60);

  idb::IdbRect segment_rect = segment->get_segment_rect();
  assert(segment_rect.get_low_x() == 8);
  assert(segment_rect.get_low_y() == 58);
  assert(segment_rect.get_high_x() == 32);
  assert(segment_rect.get_high_y() == 62);
  assert(store.find_shape(net_shapes[0])->bbox.lx == 8);
  assert(store.find_shape(net_shapes[0])->bbox.hy == 62);
  assert(store.delta_events().back().command_id == 801);
}

void test_geometry_edit_applier_resizes_regular_net_wire_segment_back_to_idb()
{
  idb::IdbLayout layout;
  idb::IdbDesign design(&layout);

  idb::IdbLayerRouting routing_layer;
  routing_layer.set_name("M1");
  routing_layer.set_id(11);
  routing_layer.set_width(4);
  routing_layer.set_direction(idb::IdbLayerDirection::kHorizontal);

  idb::IdbNet* net = design.get_net_list()->add_net("n0");
  net->set_id(101);
  idb::IdbRegularWireSegment* segment = net->get_wire_list()->add_wire()->add_segment();
  segment->set_layer(&routing_layer);
  segment->set_layer_name("M1");
  segment->add_point(0, 50);
  segment->add_point(20, 50);

  GeometryStore store;
  GeometryBuilder builder;
  builder.rebuild_from_design(design, layout, store);

  const std::vector<ShapeId> net_shapes = store.query_owner(OwnerType::kNetWireSegment, 101);
  assert(net_shapes.size() == 1);

  GeometryEditCommand command;
  command.command_id = 804;
  command.shape_id = net_shapes[0];
  command.expected_version = 1;
  command.op = GeometryEditOp::kResizeRect;
  command.requested_bbox = Rect32{-2, 48, 42, 52};

  GeometryEditApplier applier;
  const GeometryEditResult result = applier.apply_edit(command, design, store);

  assert(result.command_id == 804);
  assert(result.shape_id == net_shapes[0]);
  assert(result.status == GeometryEditStatus::kAccepted);
  assert(result.new_version == 2);
  assert(result.committed_bbox.lx == -2);
  assert(result.committed_bbox.ly == 48);
  assert(result.committed_bbox.hx == 42);
  assert(result.committed_bbox.hy == 52);

  assert(segment->get_point_start()->get_x() == 0);
  assert(segment->get_point_start()->get_y() == 50);
  assert(segment->get_point_second()->get_x() == 40);
  assert(segment->get_point_second()->get_y() == 50);

  idb::IdbRect segment_rect = segment->get_segment_rect();
  assert(segment_rect.get_low_x() == -2);
  assert(segment_rect.get_low_y() == 48);
  assert(segment_rect.get_high_x() == 42);
  assert(segment_rect.get_high_y() == 52);
  assert(store.find_shape(net_shapes[0])->bbox.hx == 42);
  assert(store.delta_events().back().command_id == 804);
}

void test_geometry_edit_applier_moves_special_net_wire_segment_back_to_idb()
{
  idb::IdbLayout layout;
  idb::IdbDesign design(&layout);

  idb::IdbLayerRouting routing_layer;
  routing_layer.set_name("M1");
  routing_layer.set_id(11);
  routing_layer.set_width(4);
  routing_layer.set_direction(idb::IdbLayerDirection::kVertical);

  idb::IdbSpecialNet* special_net = design.get_special_net_list()->add_net("VDD");
  idb::IdbSpecialWireSegment* segment = special_net->get_wire_list()->add_wire()->add_segment();
  segment->set_layer(&routing_layer);
  segment->set_route_width(6);
  segment->set_shape_type(idb::IdbWireShapeType::kStripe);
  segment->add_point(100, 10);
  segment->add_point(100, 40);
  segment->set_bounding_box();

  GeometryStore store;
  GeometryBuilder builder;
  builder.rebuild_from_design(design, layout, store);

  const std::vector<ShapeId> net_shapes = store.query_owner(OwnerType::kSpecialWireSegment, 0);
  assert(net_shapes.size() == 1);

  GeometryEditCommand command;
  command.command_id = 802;
  command.shape_id = net_shapes[0];
  command.expected_version = 1;
  command.op = GeometryEditOp::kMoveShape;
  command.requested_bbox = Rect32{107, 25, 113, 55};

  GeometryEditApplier applier;
  const GeometryEditResult result = applier.apply_edit(command, design, store);

  assert(result.command_id == 802);
  assert(result.shape_id == net_shapes[0]);
  assert(result.status == GeometryEditStatus::kAccepted);
  assert(result.new_version == 2);
  assert(result.committed_bbox.lx == 107);
  assert(result.committed_bbox.ly == 25);
  assert(result.committed_bbox.hx == 113);
  assert(result.committed_bbox.hy == 55);

  assert(segment->get_point_start()->get_x() == 110);
  assert(segment->get_point_start()->get_y() == 25);
  assert(segment->get_point_second()->get_x() == 110);
  assert(segment->get_point_second()->get_y() == 55);
  assert(segment->get_bounding_box()->get_low_x() == 107);
  assert(segment->get_bounding_box()->get_high_y() == 55);
  assert(store.find_shape(net_shapes[0])->bbox.lx == 107);
  assert(store.find_shape(net_shapes[0])->bbox.hy == 55);
  assert(store.delta_events().back().command_id == 802);
}

void test_geometry_edit_applier_resizes_special_net_wire_segment_back_to_idb()
{
  idb::IdbLayout layout;
  idb::IdbDesign design(&layout);

  idb::IdbLayerRouting routing_layer;
  routing_layer.set_name("M1");
  routing_layer.set_id(11);
  routing_layer.set_width(4);
  routing_layer.set_direction(idb::IdbLayerDirection::kVertical);

  idb::IdbSpecialNet* special_net = design.get_special_net_list()->add_net("VDD");
  idb::IdbSpecialWireSegment* segment = special_net->get_wire_list()->add_wire()->add_segment();
  segment->set_layer(&routing_layer);
  segment->set_route_width(6);
  segment->set_shape_type(idb::IdbWireShapeType::kStripe);
  segment->add_point(100, 10);
  segment->add_point(100, 40);
  segment->set_bounding_box();

  GeometryStore store;
  GeometryBuilder builder;
  builder.rebuild_from_design(design, layout, store);

  const std::vector<ShapeId> net_shapes = store.query_owner(OwnerType::kSpecialWireSegment, 0);
  assert(net_shapes.size() == 1);

  GeometryEditCommand command;
  command.command_id = 805;
  command.shape_id = net_shapes[0];
  command.expected_version = 1;
  command.op = GeometryEditOp::kResizeRect;
  command.requested_bbox = Rect32{97, 5, 103, 70};

  GeometryEditApplier applier;
  const GeometryEditResult result = applier.apply_edit(command, design, store);

  assert(result.command_id == 805);
  assert(result.shape_id == net_shapes[0]);
  assert(result.status == GeometryEditStatus::kAccepted);
  assert(result.new_version == 2);
  assert(result.committed_bbox.lx == 97);
  assert(result.committed_bbox.ly == 5);
  assert(result.committed_bbox.hx == 103);
  assert(result.committed_bbox.hy == 70);

  assert(segment->get_point_start()->get_x() == 100);
  assert(segment->get_point_start()->get_y() == 5);
  assert(segment->get_point_second()->get_x() == 100);
  assert(segment->get_point_second()->get_y() == 70);
  assert(segment->get_bounding_box()->get_low_y() == 5);
  assert(segment->get_bounding_box()->get_high_y() == 70);
  assert(store.find_shape(net_shapes[0])->bbox.hy == 70);
  assert(store.delta_events().back().command_id == 805);
}

void test_geometry_edit_applier_resizes_short_horizontal_special_wire_by_points()
{
  idb::IdbLayout layout;
  idb::IdbDesign design(&layout);

  idb::IdbLayerRouting routing_layer;
  routing_layer.set_name("M1");
  routing_layer.set_id(11);
  routing_layer.set_width(4);
  routing_layer.set_direction(idb::IdbLayerDirection::kHorizontal);

  idb::IdbSpecialNet* special_net = design.get_special_net_list()->add_net("VDD_SHORT");
  idb::IdbSpecialWireSegment* segment = special_net->get_wire_list()->add_wire()->add_segment();
  segment->set_layer(&routing_layer);
  segment->set_route_width(6);
  segment->set_shape_type(idb::IdbWireShapeType::kStripe);
  segment->add_point(100, 50);
  segment->add_point(102, 50);
  segment->set_bounding_box();

  GeometryStore store;
  GeometryBuilder builder;
  builder.rebuild_from_design(design, layout, store);

  const std::vector<ShapeId> net_shapes = store.query_owner(OwnerType::kSpecialWireSegment, 0);
  assert(net_shapes.size() == 1);
  assert(store.find_shape(net_shapes[0])->bbox.lx == 100);
  assert(store.find_shape(net_shapes[0])->bbox.ly == 47);
  assert(store.find_shape(net_shapes[0])->bbox.hx == 102);
  assert(store.find_shape(net_shapes[0])->bbox.hy == 53);

  GeometryEditCommand command;
  command.command_id = 806;
  command.shape_id = net_shapes[0];
  command.expected_version = 1;
  command.op = GeometryEditOp::kResizeRect;
  command.requested_bbox = Rect32{90, 47, 110, 53};

  GeometryEditApplier applier;
  const GeometryEditResult result = applier.apply_edit(command, design, store);

  assert(result.command_id == 806);
  assert(result.status == GeometryEditStatus::kAccepted);
  assert(result.committed_bbox.lx == 90);
  assert(result.committed_bbox.ly == 47);
  assert(result.committed_bbox.hx == 110);
  assert(result.committed_bbox.hy == 53);

  assert(segment->get_point_start()->get_x() == 90);
  assert(segment->get_point_start()->get_y() == 50);
  assert(segment->get_point_second()->get_x() == 110);
  assert(segment->get_point_second()->get_y() == 50);
  assert(segment->get_bounding_box()->get_low_x() == 90);
  assert(segment->get_bounding_box()->get_high_x() == 110);
  assert(store.find_shape(net_shapes[0])->bbox.hx == 110);
  assert(store.delta_events().back().command_id == 806);
}

void test_geometry_edit_applier_updates_blockage_rect_back_to_idb()
{
  idb::IdbLayout layout;
  idb::IdbDesign design(&layout);

  idb::IdbLayerRouting routing_layer;
  routing_layer.set_name("M1");
  routing_layer.set_id(11);

  idb::IdbRoutingBlockage* blockage = design.get_blockage_list()->add_blockage_routing("M1");
  blockage->set_layer(&routing_layer);
  blockage->add_rect(10, 20, 30, 40);

  GeometryStore store;
  GeometryBuilder builder;
  builder.rebuild_from_design(design, layout, store);

  const std::vector<ShapeId> blockage_shapes = store.query_owner(OwnerType::kBlockage, 0);
  assert(blockage_shapes.size() == 1);

  GeometryEditCommand command;
  command.command_id = 803;
  command.shape_id = blockage_shapes[0];
  command.expected_version = 1;
  command.op = GeometryEditOp::kResizeRect;
  command.requested_bbox = Rect32{12, 22, 44, 66};

  GeometryEditApplier applier;
  const GeometryEditResult result = applier.apply_edit(command, design, store);

  assert(result.command_id == 803);
  assert(result.shape_id == blockage_shapes[0]);
  assert(result.status == GeometryEditStatus::kAccepted);
  assert(result.new_version == 2);
  assert(result.committed_bbox.lx == 12);
  assert(result.committed_bbox.ly == 22);
  assert(result.committed_bbox.hx == 44);
  assert(result.committed_bbox.hy == 66);

  const std::vector<idb::IdbRect*> rects = blockage->get_rect_list();
  assert(rects.size() == 1);
  assert(rects[0]->get_low_x() == 12);
  assert(rects[0]->get_low_y() == 22);
  assert(rects[0]->get_high_x() == 44);
  assert(rects[0]->get_high_y() == 66);
  assert(store.find_shape(blockage_shapes[0])->bbox.lx == 12);
  assert(store.find_shape(blockage_shapes[0])->bbox.hy == 66);
  assert(store.delta_events().back().command_id == 803);
}

void test_geometry_edit_applier_updates_fill_rect_back_to_idb()
{
  idb::IdbLayout layout;
  idb::IdbDesign design(&layout);

  idb::IdbLayerRouting routing_layer;
  routing_layer.set_name("M1");
  routing_layer.set_id(11);

  idb::IdbFillLayer* fill_layer = design.get_fill_list()->add_fill_layer(&routing_layer);
  fill_layer->add_rect(10, 20, 30, 40);

  GeometryStore store;
  GeometryBuilder builder;
  builder.rebuild_from_design(design, layout, store);

  const std::vector<ShapeId> fill_shapes = store.query_owner(OwnerType::kFill, 0);
  assert(fill_shapes.size() == 1);

  GeometryEditCommand command;
  command.command_id = 807;
  command.shape_id = fill_shapes[0];
  command.expected_version = 1;
  command.op = GeometryEditOp::kResizeRect;
  command.requested_bbox = Rect32{12, 22, 44, 66};

  GeometryEditApplier applier;
  const GeometryEditResult result = applier.apply_edit(command, design, store);

  assert(result.command_id == 807);
  assert(result.shape_id == fill_shapes[0]);
  assert(result.status == GeometryEditStatus::kAccepted);
  assert(result.new_version == 2);
  assert(result.committed_bbox.lx == 12);
  assert(result.committed_bbox.ly == 22);
  assert(result.committed_bbox.hx == 44);
  assert(result.committed_bbox.hy == 66);

  const std::vector<idb::IdbRect*> rects = fill_layer->get_rect_list();
  assert(rects.size() == 1);
  assert(rects[0]->get_low_x() == 12);
  assert(rects[0]->get_low_y() == 22);
  assert(rects[0]->get_high_x() == 44);
  assert(rects[0]->get_high_y() == 66);
  assert(store.find_shape(fill_shapes[0])->bbox.lx == 12);
  assert(store.find_shape(fill_shapes[0])->bbox.hy == 66);
  assert(store.delta_events().back().command_id == 807);
}

void test_geometry_edit_applier_updates_region_boundary_rect_back_to_idb()
{
  idb::IdbLayout layout;
  idb::IdbDesign design(&layout);

  idb::IdbRegion* region = design.get_region_list()->add_region("region0");
  region->add_boundary(10, 20, 30, 40);

  GeometryStore store;
  GeometryBuilder builder;
  builder.rebuild_from_design(design, layout, store);

  const std::vector<ShapeId> region_shapes = store.query_owner(OwnerType::kRegion, 0);
  assert(region_shapes.size() == 1);

  GeometryEditCommand command;
  command.command_id = 808;
  command.shape_id = region_shapes[0];
  command.expected_version = 1;
  command.op = GeometryEditOp::kResizeRect;
  command.requested_bbox = Rect32{12, 22, 44, 66};

  GeometryEditApplier applier;
  const GeometryEditResult result = applier.apply_edit(command, design, store);

  assert(result.command_id == 808);
  assert(result.shape_id == region_shapes[0]);
  assert(result.status == GeometryEditStatus::kAccepted);
  assert(result.new_version == 2);
  assert(result.committed_bbox.lx == 12);
  assert(result.committed_bbox.ly == 22);
  assert(result.committed_bbox.hx == 44);
  assert(result.committed_bbox.hy == 66);

  const std::vector<idb::IdbRect*> boundaries = region->get_boundary();
  assert(boundaries.size() == 1);
  assert(boundaries[0]->get_low_x() == 12);
  assert(boundaries[0]->get_low_y() == 22);
  assert(boundaries[0]->get_high_x() == 44);
  assert(boundaries[0]->get_high_y() == 66);
  assert(store.find_shape(region_shapes[0])->bbox.lx == 12);
  assert(store.find_shape(region_shapes[0])->bbox.hy == 66);
  assert(store.delta_events().back().command_id == 808);
}

void test_geometry_edit_applier_updates_slot_rect_back_to_idb()
{
  idb::IdbLayout layout;
  idb::IdbDesign design(&layout);

  idb::IdbLayerRouting routing_layer;
  routing_layer.set_name("M1");
  routing_layer.set_id(11);

  idb::IdbSlot* slot = design.get_slot_list()->add_slot();
  slot->set_layer(&routing_layer);
  slot->add_rect(10, 20, 30, 40);

  GeometryStore store;
  GeometryBuilder builder;
  builder.rebuild_from_design(design, layout, store);

  const std::vector<ShapeId> slot_shapes = store.query_owner(OwnerType::kSlot, 0);
  assert(slot_shapes.size() == 1);

  GeometryEditCommand command;
  command.command_id = 809;
  command.shape_id = slot_shapes[0];
  command.expected_version = 1;
  command.op = GeometryEditOp::kResizeRect;
  command.requested_bbox = Rect32{12, 22, 44, 66};

  GeometryEditApplier applier;
  const GeometryEditResult result = applier.apply_edit(command, design, store);

  assert(result.command_id == 809);
  assert(result.shape_id == slot_shapes[0]);
  assert(result.status == GeometryEditStatus::kAccepted);
  assert(result.new_version == 2);
  assert(result.committed_bbox.lx == 12);
  assert(result.committed_bbox.ly == 22);
  assert(result.committed_bbox.hx == 44);
  assert(result.committed_bbox.hy == 66);

  const std::vector<idb::IdbRect*> rects = slot->get_rect_list();
  assert(rects.size() == 1);
  assert(rects[0]->get_low_x() == 12);
  assert(rects[0]->get_low_y() == 22);
  assert(rects[0]->get_high_x() == 44);
  assert(rects[0]->get_high_y() == 66);
  assert(store.find_shape(slot_shapes[0])->bbox.lx == 12);
  assert(store.find_shape(slot_shapes[0])->bbox.hy == 66);
  assert(store.delta_events().back().command_id == 809);
}

void test_geometry_snapshot_writer_writes_manifest_and_core_binary_files()
{
  GeometryStore store;
  store.add_rect(1, Rect32{0, 0, 10, 20}, OwnerRef{OwnerType::kDie});

  LinePayload line;
  line.begin = Point32{10, 10};
  line.end = Point32{20, 10};
  line.width = 4;
  store.add_line(2, line, OwnerRef{OwnerType::kNetWireSegment});

  PointPayload point;
  point.point = Point32{7, 9};
  store.add_point(3, point, OwnerRef{OwnerType::kPinPortShape});

  const std::filesystem::path output_dir =
      std::filesystem::temp_directory_path() / "ecc_geometry_snapshot_writer_test";
  std::filesystem::remove_all(output_dir);

  SnapshotWriteOptions write_options{output_dir};
  write_options.design_name = "unit_design";
  write_options.design_version = "5.8";
  write_options.dbu_per_micron = 2000;
  write_options.manufacture_grid = 5;
  GeometryLayerMetadata layer_metadata;
  layer_metadata.layer_id = 1;
  layer_metadata.order = 7;
  layer_metadata.name = "M1";
  layer_metadata.type = "routing";
  layer_metadata.direction = "horizontal";
  layer_metadata.width = 100;
  layer_metadata.pitch_x = 200;
  layer_metadata.pitch_y = 300;
  layer_metadata.min_spacing = 70;
  layer_metadata.min_area = 400;
  layer_metadata.min_step = 50;
  layer_metadata.cut_spacing = 0;
  layer_metadata.enclosure_below = "1,2";
  layer_metadata.enclosure_above = "3,4";
  layer_metadata.lef58_rule_count = 5;
  write_options.layers.push_back(layer_metadata);
  GeometrySiteMetadata site_metadata;
  site_metadata.name = "core_site";
  site_metadata.site_class = "CORE";
  site_metadata.symmetry = "X";
  site_metadata.orient = "N";
  site_metadata.width = 10;
  site_metadata.height = 20;
  site_metadata.is_overlap = true;
  write_options.sites.push_back(site_metadata);
  GeometryMasterMetadata master_metadata;
  master_metadata.name = "INVX1";
  master_metadata.master_type = "CORE";
  master_metadata.site = "core_site";
  master_metadata.symmetry = "X,Y";
  master_metadata.origin_x = -1;
  master_metadata.origin_y = 2;
  master_metadata.width = 30;
  master_metadata.height = 40;
  master_metadata.term_count = 3;
  master_metadata.obs_count = 2;
  write_options.masters.push_back(master_metadata);
  GeometryConnectivityMetadata connectivity_metadata;
  connectivity_metadata.net_name = "clk";
  connectivity_metadata.net_kind = "regular";
  connectivity_metadata.endpoint_type = "instance";
  connectivity_metadata.instance_name = "u0";
  connectivity_metadata.pin_name = "A";
  connectivity_metadata.master_name = "INVX1";
  write_options.connectivity.push_back(connectivity_metadata);
  GeometryBusMetadata bus_metadata;
  bus_metadata.name = "data";
  bus_metadata.bus_type = "net";
  bus_metadata.left = 7;
  bus_metadata.right = 0;
  bus_metadata.net_count = 8;
  bus_metadata.pin_count = 0;
  write_options.buses.push_back(bus_metadata);
  GeometryGroupMetadata group_metadata;
  group_metadata.name = "cluster0";
  group_metadata.region_name = "region0";
  group_metadata.instance_count = 4;
  write_options.groups.push_back(group_metadata);

  GeometrySnapshotWriter writer;
  const SnapshotWriteResult result = writer.write(store, write_options);

  assert(result.ok);
  assert(result.shape_count == 3);
  assert(result.owner_count == 3);
  assert(result.payload_size >= sizeof(RectPayload) + sizeof(LinePayload) + sizeof(PointPayload));

  const std::filesystem::path manifest_path = output_dir / "geometry.manifest";
  assert(std::filesystem::exists(manifest_path));
  assert(result.epoch != 0);
  assert(!std::filesystem::exists(output_dir / "geometry.manifest.tmp"));

  const auto snapshot_path = [&](const std::string& key) { return output_dir / manifest_value(manifest_path, key); };
  const std::filesystem::path shapes_path = snapshot_path("shapes");
  const std::filesystem::path owners_path = snapshot_path("owners");
  const std::filesystem::path payload_path = snapshot_path("payload");
  const std::filesystem::path names_path = snapshot_path("names");
  const std::filesystem::path name_index_path = snapshot_path("name_index");
  const std::filesystem::path meta_path = snapshot_path("meta");
  const std::filesystem::path sidmap_path = snapshot_path("sidmap");
  const std::filesystem::path delta_path = snapshot_path("delta");
  const std::filesystem::path view_path = snapshot_path("view");
  const std::filesystem::path layers_path = snapshot_path("layers");
  const std::filesystem::path sites_path = snapshot_path("sites");
  const std::filesystem::path masters_path = snapshot_path("masters");
  const std::filesystem::path connectivity_path = snapshot_path("connectivity");
  const std::filesystem::path buses_path = snapshot_path("buses");
  const std::filesystem::path groups_path = snapshot_path("groups");

  assert(shapes_path.parent_path().parent_path() == output_dir / "epochs");
  assert(std::filesystem::exists(meta_path));
  assert(std::filesystem::exists(shapes_path));
  assert(std::filesystem::exists(owners_path));
  assert(std::filesystem::exists(payload_path));
  assert(std::filesystem::exists(names_path));
  assert(std::filesystem::exists(name_index_path));
  assert(std::filesystem::exists(sidmap_path));
  assert(std::filesystem::exists(delta_path));
  assert(std::filesystem::exists(view_path));
  assert(std::filesystem::exists(layers_path));
  assert(std::filesystem::exists(sites_path));
  assert(std::filesystem::exists(masters_path));
  assert(std::filesystem::exists(connectivity_path));
  assert(std::filesystem::exists(buses_path));
  assert(std::filesystem::exists(groups_path));

  const GeometryFileHeader meta_header = read_header(meta_path);
  assert(meta_header.file_kind == GeometryFileKind::kMeta);
  assert(meta_header.record_size == sizeof(GeometryMetaRecord));
  assert(meta_header.record_count == 1);

  const GeometryFileHeader shapes_header = read_header(shapes_path);
  assert(shapes_header.magic == kGeometryFileMagic);
  assert(shapes_header.schema_version == kGeometrySchemaVersion);
  assert(shapes_header.file_kind == GeometryFileKind::kShapes);
  assert(shapes_header.record_size == sizeof(ShapeRecord));
  assert(shapes_header.record_count == 3);

  const GeometryFileHeader owners_header = read_header(owners_path);
  assert(owners_header.file_kind == GeometryFileKind::kOwners);
  assert(owners_header.record_size == sizeof(OwnerRef));
  assert(owners_header.record_count == 3);

  const GeometryFileHeader payload_header = read_header(payload_path);
  assert(payload_header.file_kind == GeometryFileKind::kPayload);
  assert(payload_header.record_size == 1);
  assert(payload_header.payload_size == result.payload_size);

  const GeometryFileHeader names_header = read_header(names_path);
  assert(names_header.file_kind == GeometryFileKind::kNames);
  assert(names_header.record_size == 1);

  const GeometryFileHeader name_index_header = read_header(name_index_path);
  assert(name_index_header.file_kind == GeometryFileKind::kNameIndex);
  assert(name_index_header.record_count == store.name_records().size());
  assert(name_index_header.payload_size == store.name_records().size_bytes());

  const GeometryFileHeader sidmap_header = read_header(sidmap_path);
  assert(sidmap_header.file_kind == GeometryFileKind::kSidMap);
  assert(sidmap_header.record_size == sizeof(GeometrySidMapRecord));
  assert(sidmap_header.record_count == store.records().size());

  const GeometryFileHeader delta_header = read_header(delta_path);
  assert(delta_header.file_kind == GeometryFileKind::kDelta);
  assert(delta_header.record_size == sizeof(GeometryDeltaEvent));
  assert(delta_header.record_count == store.delta_events().size());
  assert(delta_header.payload_size == store.delta_events().size_bytes());

  const GeometryFileHeader view_header = read_header(view_path);
  assert(view_header.file_kind == GeometryFileKind::kView);
  assert(view_header.record_size == sizeof(GeometryViewTileRecord));
  assert(view_header.record_count > 0);
  assert(view_header.payload_size == view_header.record_count * sizeof(GeometryViewTileRecord));

  const std::string manifest = read_text_file(manifest_path);
  assert(manifest.find("schema_version=1") != std::string::npos);
  assert(manifest.find("active_epoch=") != std::string::npos);
  assert(manifest.find("design_name=unit_design") != std::string::npos);
  assert(manifest.find("design_version=5.8") != std::string::npos);
  assert(manifest.find("dbu_per_micron=2000") != std::string::npos);
  assert(manifest.find("manufacture_grid=5") != std::string::npos);
  assert(manifest.find("geometry.meta.bin") != std::string::npos);
  assert(manifest.find("geometry.shapes.bin") != std::string::npos);
  assert(manifest.find("geometry.owners.bin") != std::string::npos);
  assert(manifest.find("geometry.payload.bin") != std::string::npos);
  assert(manifest.find("geometry.names.bin") != std::string::npos);
  assert(manifest.find("geometry.name_index.bin") != std::string::npos);
  assert(manifest.find("geometry.sidmap.bin") != std::string::npos);
  assert(manifest.find("geometry.delta.bin") != std::string::npos);
  assert(manifest.find("geometry.view.bin") != std::string::npos);
  assert(manifest.find("geometry.layers.txt") != std::string::npos);
  assert(manifest.find("geometry.sites.txt") != std::string::npos);
  assert(manifest.find("geometry.masters.txt") != std::string::npos);
  assert(manifest.find("geometry.connectivity.txt") != std::string::npos);
  assert(manifest.find("geometry.buses.txt") != std::string::npos);
  assert(manifest.find("geometry.groups.txt") != std::string::npos);

  const std::string layer_manifest = read_text_file(layers_path);
  assert(layer_manifest.find("layer_id\torder\ttype\tdirection\twidth\tpitch_x\tpitch_y\tname") != std::string::npos);
  assert(layer_manifest.find("1\t7\trouting\thorizontal\t100\t200\t300\tM1") != std::string::npos);
  assert(layer_manifest.find("min_spacing\tmin_area\tmin_step\tcut_spacing\tenclosure_below\tenclosure_above\tlef58_rule_count")
         != std::string::npos);
  assert(layer_manifest.find("1\t7\trouting\thorizontal\t100\t200\t300\tM1\t70\t400\t50\t0\t1,2\t3,4\t5")
         != std::string::npos);

  const std::string site_manifest = read_text_file(sites_path);
  assert(site_manifest.find("name\tclass\tsymmetry\torient\twidth\theight\tis_overlap") != std::string::npos);
  assert(site_manifest.find("core_site\tCORE\tX\tN\t10\t20\t1") != std::string::npos);

  const std::string master_manifest = read_text_file(masters_path);
  assert(master_manifest.find("name\ttype\tsite\tsymmetry\torigin_x\torigin_y\twidth\theight\tterm_count\tobs_count")
         != std::string::npos);
  assert(master_manifest.find("INVX1\tCORE\tcore_site\tX,Y\t-1\t2\t30\t40\t3\t2") != std::string::npos);

  const std::string connectivity_manifest = read_text_file(connectivity_path);
  assert(connectivity_manifest.find("net\tkind\tendpoint_type\tinstance\tpin\tmaster") != std::string::npos);
  assert(connectivity_manifest.find("clk\tregular\tinstance\tu0\tA\tINVX1") != std::string::npos);

  const std::string bus_manifest = read_text_file(buses_path);
  assert(bus_manifest.find("name\ttype\tleft\tright\tnet_count\tpin_count") != std::string::npos);
  assert(bus_manifest.find("data\tnet\t7\t0\t8\t0") != std::string::npos);

  const std::string group_manifest = read_text_file(groups_path);
  assert(group_manifest.find("name\tregion\tinstance_count") != std::string::npos);
  assert(group_manifest.find("cluster0\tregion0\t4") != std::string::npos);

  std::filesystem::remove_all(output_dir);
}

void test_geometry_builder_collects_site_and_master_metadata()
{
  idb::IdbLayout layout;
  idb::IdbDesign design(&layout);
  auto* site = layout.get_sites()->add_site_list("core_site");
  site->set_class(idb::IdbSiteClass::kCore);
  site->set_symmetry(idb::IdbSymmetry::kX);
  site->set_orient(idb::IdbOrient::kN_R0);
  site->set_width(10);
  site->set_height(20);
  site->set_occupied(true);

  auto* master = layout.get_cell_master_list()->set_cell_master("INVX1");
  master->set_type(idb::CellMasterType::kCore);
  master->set_site(site);
  master->set_symmetry_x(true);
  master->set_symmetry_y(true);
  master->set_origin_x(-1);
  master->set_origin_y(2);
  master->set_width(30);
  master->set_height(40);
  master->add_term("A");
  master->add_term("Y");
  master->add_obs();

  auto* instance = design.get_instance_list()->add_instance("u0");
  instance->set_id(77);
  instance->set_cell_master(master);
  auto* net = design.get_net_list()->add_net("clk", idb::IdbConnectType::kSignal);
  auto* inst_pin = instance->get_pin("A");
  inst_pin->set_net(net);
  inst_pin->set_net_name("clk");
  net->add_instance_pin(inst_pin);
  auto* io_pin = design.get_io_pin_list()->add_pin_list("clk_in");
  io_pin->set_as_io();
  io_pin->set_net(net);
  io_pin->set_net_name("clk");
  net->add_io_pin(io_pin);

  idb::IdbBus bus("data", 7, 0);
  bus.set_type(idb::IdbBus::kBusNet);
  bus.addNet(net, 0);
  design.get_bus_list()->addBusObject(std::move(bus));

  auto* region = design.get_region_list()->add_region("region0");
  auto* group = design.get_group_list()->add_group("cluster0");
  group->set_region(region);
  group->add_instance(instance);

  GeometryBuilder builder;
  const std::vector<GeometrySiteMetadata> sites = builder.collect_site_metadata(layout);
  assert(sites.size() == 1);
  assert(sites[0].name == "core_site");
  assert(sites[0].site_class == "CORE");
  assert(sites[0].symmetry == "X");
  assert(sites[0].orient == "N");
  assert(sites[0].width == 10);
  assert(sites[0].height == 20);
  assert(sites[0].is_overlap);

  const std::vector<GeometryMasterMetadata> masters = builder.collect_master_metadata(layout);
  assert(masters.size() == 1);
  assert(masters[0].name == "INVX1");
  assert(masters[0].master_type == "CORE");
  assert(masters[0].site == "core_site");
  assert(masters[0].symmetry == "X,Y");
  assert(masters[0].origin_x == -1);
  assert(masters[0].origin_y == 2);
  assert(masters[0].width == 30);
  assert(masters[0].height == 40);
  assert(masters[0].term_count == 2);
  assert(masters[0].obs_count == 1);

  const std::vector<GeometryConnectivityMetadata> connectivity = builder.collect_connectivity_metadata(design);
  assert(connectivity.size() == 2);
  assert(connectivity[0].net_name == "clk");
  assert(connectivity[0].endpoint_type == "instance");
  assert(connectivity[0].instance_name == "u0");
  assert(connectivity[0].pin_name == "A");
  assert(connectivity[0].master_name == "INVX1");
  assert(connectivity[1].endpoint_type == "io");
  assert(connectivity[1].pin_name == "clk_in");

  const std::vector<GeometryBusMetadata> buses = builder.collect_bus_metadata(design);
  assert(buses.size() == 1);
  assert(buses[0].name == "data");
  assert(buses[0].bus_type == "net");
  assert(buses[0].left == 7);
  assert(buses[0].right == 0);
  assert(buses[0].net_count == 1);

  const std::vector<GeometryGroupMetadata> groups = builder.collect_group_metadata(design);
  assert(groups.size() == 1);
  assert(groups[0].name == "cluster0");
  assert(groups[0].region_name == "region0");
  assert(groups[0].instance_count == 1);
}

void test_geometry_snapshot_exporter_writes_current_idb_design()
{
  idb::IdbLayout layout;
  layout.initDie(0, 0, 1000, 800);
  idb::IdbDesign design(&layout);
  design.set_design_name("exporter_design");
  design.set_version("5.8");

  const std::filesystem::path output_dir =
      std::filesystem::temp_directory_path() / "ecc_geometry_snapshot_exporter_test";
  std::filesystem::remove_all(output_dir);

  const SnapshotWriteResult result = export_geometry_snapshot(design, layout, output_dir);

  assert(result.ok);
  assert(result.shape_count == 1);
  assert(result.manifest_path == output_dir / "geometry.manifest");
  assert(manifest_value(result.manifest_path, "design_name") == "exporter_design");

  std::filesystem::remove_all(output_dir);
}

void test_geometry_snapshot_writer_switches_epoch_without_overwriting_previous_files()
{
  GeometryStore store;
  const ShapeId shape_id = store.add_rect(1, Rect32{0, 0, 10, 20}, OwnerRef{OwnerType::kDie});
  const std::filesystem::path output_dir =
      std::filesystem::temp_directory_path() / "ecc_geometry_snapshot_epoch_switch_test";
  std::filesystem::remove_all(output_dir);

  GeometrySnapshotWriter writer;
  const SnapshotWriteResult first = writer.write(store, SnapshotWriteOptions{output_dir});
  assert(first.ok);
  const std::filesystem::path manifest_path = output_dir / "geometry.manifest";
  const std::filesystem::path first_shapes = output_dir / manifest_value(manifest_path, "shapes");
  assert(std::filesystem::exists(first_shapes));

  assert(store.update_rect(shape_id, Rect32{100, 200, 110, 220}, 77));
  const SnapshotWriteResult second = writer.write(store, SnapshotWriteOptions{output_dir});
  assert(second.ok);
  const std::filesystem::path second_shapes = output_dir / manifest_value(manifest_path, "shapes");

  assert(second.epoch != first.epoch);
  assert(second_shapes != first_shapes);
  assert(std::filesystem::exists(first_shapes));
  assert(std::filesystem::exists(second_shapes));
  assert(!std::filesystem::exists(output_dir / "geometry.manifest.tmp"));

  GeometryStore loaded;
  GeometrySnapshotReader reader;
  const SnapshotReadResult read_result = reader.read(SnapshotReadOptions{manifest_path}, loaded);
  assert(read_result.ok);
  assert(loaded.find_shape(shape_id) != nullptr);
  assert(loaded.find_shape(shape_id)->bbox.lx == 100);
  assert(loaded.find_shape(shape_id)->version == 2);

  std::filesystem::remove_all(output_dir);
}

void test_geometry_snapshot_reader_round_trips_core_binary_files()
{
  GeometryStore store;

  OwnerRef rect_owner;
  rect_owner.type = OwnerType::kInstanceBBox;
  rect_owner.owner_id = 1001;
  rect_owner.path0 = 7;
  const ShapeId rect_id = store.add_rect(9, Rect32{10, 20, 30, 40}, rect_owner, 3);

  LinePayload line;
  line.begin = Point32{50, 60};
  line.end = Point32{80, 60};
  line.width = 6;
  OwnerRef line_owner;
  line_owner.type = OwnerType::kNetWireSegment;
  line_owner.owner_id = 2002;
  line_owner.path0 = 1;
  line_owner.path1 = 2;
  const ShapeId line_id = store.add_line(11, line, line_owner);
  store.add_owner_name(OwnerType::kInstanceBBox, 1001, "u_round_trip");
  store.add_owner_name(OwnerType::kNetWireSegment, 2002, "clk");

  const std::filesystem::path output_dir =
      std::filesystem::temp_directory_path() / "ecc_geometry_snapshot_reader_round_trip_test";
  std::filesystem::remove_all(output_dir);

  GeometrySnapshotWriter writer;
  const SnapshotWriteResult write_result = writer.write(store, SnapshotWriteOptions{output_dir});
  assert(write_result.ok);

  GeometryStore loaded;
  GeometrySnapshotReader reader;
  const SnapshotReadResult read_result = reader.read(SnapshotReadOptions{output_dir / "geometry.manifest"}, loaded);

  assert(read_result.ok);
  assert(read_result.shape_count == 2);
  assert(read_result.owner_count == 2);
  assert(read_result.payload_size == write_result.payload_size);
  assert(loaded.records().size() == 2);
  assert(loaded.owners().size() == 2);
  assert(loaded.payloads().size() == write_result.payload_size);
  assert(loaded.name_records().size() == 2);
  assert(loaded.name_payloads().size() == 15);

  const ShapeRecord* loaded_rect = loaded.find_shape(rect_id);
  const ShapeRecord* loaded_line = loaded.find_shape(line_id);

  assert(loaded_rect != nullptr);
  assert(loaded_rect->id == rect_id);
  assert(loaded_rect->layer_id == 9);
  assert(loaded_rect->flags == 3);
  assert(loaded_rect->bbox.lx == 10);
  assert(loaded_rect->bbox.hy == 40);
  assert(loaded.owner_of(rect_id).type == OwnerType::kInstanceBBox);
  assert(loaded.owner_of(rect_id).owner_id == 1001);
  assert(loaded.owner_of(rect_id).path0 == 7);

  assert(loaded_line != nullptr);
  assert(loaded_line->id == line_id);
  assert(loaded_line->kind == ShapeKind::kLine);
  assert(loaded_line->bbox.lx == 47);
  assert(loaded.owner_of(line_id).type == OwnerType::kNetWireSegment);
  assert(loaded.owner_of(line_id).path1 == 2);

  const std::vector<ShapeId> rect_hits = loaded.query_intersect(9, Rect32{15, 25, 16, 26});
  assert(rect_hits.size() == 1);
  assert(rect_hits[0] == rect_id);

  const std::vector<ShapeId> net_hits = loaded.query_owner(OwnerType::kNetWireSegment, 2002);
  assert(net_hits.size() == 1);
  assert(net_hits[0] == line_id);

  const std::vector<ShapeId> named_inst_hits = loaded.query_owner_name("u_round_trip");
  const std::vector<ShapeId> named_net_hits = loaded.query_owner_name("clk");
  assert(named_inst_hits.size() == 1);
  assert(named_inst_hits[0] == rect_id);
  assert(named_net_hits.size() == 1);
  assert(named_net_hits[0] == line_id);

  const ShapeId next_id = loaded.add_rect(1, Rect32{0, 0, 1, 1}, OwnerRef{OwnerType::kCore});
  assert(next_id == line_id + 1);

  std::filesystem::remove_all(output_dir);
}

void test_geometry_snapshot_reload_preserves_shape_id_and_version_during_rebuild()
{
  idb::IdbLayout layout;
  layout.initDie(0, 0, 1000, 800);
  idb::IdbDesign design(&layout);

  idb::IdbCellMaster master;
  master.set_name("persistent_master");
  master.set_width(50);
  master.set_height(30);

  idb::IdbInstance* instance = design.get_instance_list()->add_instance("u_persistent");
  instance->set_id(501);
  instance->set_cell_master(&master);
  instance->set_coodinate(100, 200);
  instance->set_status_placed();

  GeometryStore store;
  GeometryBuilder builder;
  builder.rebuild_from_design(design, layout, store);
  const ShapeId original_id = store.query_owner(OwnerType::kInstanceBBox, 501)[0];

  GeometryEditCommand command;
  command.command_id = 9901;
  command.shape_id = original_id;
  command.expected_version = 1;
  command.op = GeometryEditOp::kMoveShape;
  command.requested_bbox = Rect32{300, 400, 350, 430};
  GeometryEditApplier applier;
  const GeometryEditResult edit_result = applier.apply_edit(command, design, store);
  assert(edit_result.status == GeometryEditStatus::kAccepted);
  assert(edit_result.new_version == 2);

  const std::filesystem::path output_dir =
      std::filesystem::temp_directory_path() / "ecc_geometry_snapshot_persistent_rebuild_test";
  std::filesystem::remove_all(output_dir);
  GeometrySnapshotWriter writer;
  assert(writer.write(store, SnapshotWriteOptions{output_dir}).ok);

  GeometryStore reloaded;
  GeometrySnapshotReader reader;
  assert(reader.read(SnapshotReadOptions{output_dir / "geometry.manifest"}, reloaded).ok);
  builder.rebuild_from_design(design, layout, reloaded);

  const std::vector<ShapeId> rebuilt_shapes = reloaded.query_owner(OwnerType::kInstanceBBox, 501);
  assert(rebuilt_shapes.size() == 1);
  assert(rebuilt_shapes[0] == original_id);
  assert(reloaded.find_shape(original_id)->version == 2);
  assert(reloaded.find_shape(original_id)->bbox.lx == 300);
  assert(reloaded.delta_events().empty());

  std::filesystem::remove_all(output_dir);
}

void test_geometry_snapshot_apply_edit_on_reloaded_store_is_incremental_without_rebuild()
{
  idb::IdbLayout layout;
  idb::IdbDesign design(&layout);

  idb::IdbCellMaster master;
  master.set_name("incremental_master");
  master.set_width(40);
  master.set_height(20);

  idb::IdbInstance* instance = design.get_instance_list()->add_instance("u_incremental");
  instance->set_id(601);
  instance->set_cell_master(&master);
  instance->set_coodinate(10, 20);
  instance->set_status_placed();

  GeometryStore store;
  GeometryBuilder builder;
  builder.rebuild_from_design(design, layout, store);
  const std::vector<ShapeId> original_shapes = store.query_owner(OwnerType::kInstanceBBox, 601);
  assert(original_shapes.size() == 1);
  const ShapeId original_id = original_shapes[0];

  const std::filesystem::path output_dir =
      std::filesystem::temp_directory_path() / "ecc_geometry_snapshot_incremental_apply_edit_test";
  std::filesystem::remove_all(output_dir);
  GeometrySnapshotWriter writer;
  assert(writer.write(store, SnapshotWriteOptions{output_dir}).ok);

  GeometryStore reloaded;
  GeometrySnapshotReader reader;
  assert(reader.read(SnapshotReadOptions{output_dir / "geometry.manifest"}, reloaded).ok);
  assert(reloaded.delta_events().empty());

  GeometryEditCommand command;
  command.command_id = 9902;
  command.shape_id = original_id;
  command.expected_version = 1;
  command.op = GeometryEditOp::kMoveShape;
  command.requested_bbox = Rect32{110, 220, 150, 240};

  GeometryEditApplier applier;
  const GeometryEditResult edit_result = applier.apply_edit(command, design, reloaded);
  assert(edit_result.status == GeometryEditStatus::kAccepted);
  assert(edit_result.new_version == 2);

  const std::vector<ShapeId> edited_shapes = reloaded.query_owner(OwnerType::kInstanceBBox, 601);
  assert(edited_shapes.size() == 1);
  assert(edited_shapes[0] == original_id);
  assert(reloaded.find_shape(original_id)->version == 2);
  assert(reloaded.find_shape(original_id)->bbox.lx == 110);
  assert(reloaded.find_shape(original_id)->bbox.hy == 240);
  assert(reloaded.delta_events().size() == 1);
  assert(reloaded.delta_events()[0].command_id == 9902);
  assert(reloaded.delta_events()[0].op == GeometryDeltaOp::kUpdate);
  assert(reloaded.delta_events()[0].shape_id == original_id);
  assert(reloaded.dirty_lod_tile_count() > 0);

  const SnapshotWriteResult write_result = writer.write(reloaded, SnapshotWriteOptions{output_dir});
  assert(write_result.ok);
  const std::filesystem::path delta_path = output_dir / manifest_value(output_dir / "geometry.manifest", "delta");
  const GeometryFileHeader delta_header = read_header(delta_path);
  assert(delta_header.file_kind == GeometryFileKind::kDelta);
  assert(delta_header.record_count == 1);
  assert(reloaded.dirty_lod_tile_count() == 0);

  std::filesystem::remove_all(output_dir);
}

}  // namespace

int main()
{
  test_geometry_edit_json_parses_resize_rect_op();
  test_geometry_snapshot_workflow_skips_rebuild_for_restored_apply_edit_snapshot();
  test_geometry_edit_applier_moves_instance_bbox_back_to_idb();
  test_geometry_edit_applier_resolves_instance_from_design_owner_id();
  test_geometry_edit_applier_reports_adjusted_instance_bbox_when_master_size_is_preserved();
  test_geometry_edit_applier_reports_conflict_when_shape_version_changed();
  test_store_geometry_sink_emits_all_shape_kinds();
  test_geometry_builder_rebuilds_basic_layout_and_instance_shapes();
  test_geometry_builder_rebuilds_track_and_gcell_grid_lines();
  test_geometry_builder_rebuilds_instance_halo_shapes();
  test_geometry_builder_rebuild_replaces_previous_store_contents();
  test_geometry_builder_rebuild_preserves_shape_ids_when_new_earlier_shapes_appear();
  test_geometry_builder_uses_instance_index_owner_when_id_is_missing();
  test_geometry_builder_skips_unplaced_instance_geometry();
  test_geometry_builder_syncs_moved_instance_without_full_rebuild();
  test_geometry_builder_syncs_moved_instance_halo_without_full_rebuild();
  test_geometry_builder_syncs_moved_instance_obs_without_full_rebuild();
  test_geometry_builder_syncs_moved_instance_pin_ports_without_full_rebuild();
  test_geometry_builder_sync_instance_reports_missing_owner_shape();
  test_geometry_builder_syncs_regular_net_updates_and_adds_segments();
  test_geometry_builder_syncs_regular_net_deletes_removed_segments();
  test_geometry_builder_syncs_regular_net_vias_incrementally();
  test_geometry_builder_syncs_special_net_updates_and_adds_segments();
  test_geometry_builder_syncs_special_net_vias_incrementally();
  test_geometry_builder_syncs_blockage_rects_incrementally();
  test_geometry_builder_syncs_region_boundary_rects_incrementally();
  test_geometry_builder_syncs_slot_rects_incrementally();
  test_geometry_builder_syncs_layer_fill_rects_incrementally();
  test_geometry_builder_syncs_io_pin_ports_without_full_rebuild();
  test_geometry_builder_rebuilds_def_rect_and_wire_shapes();
  test_geometry_builder_rebuilds_wire_vias_and_instance_obs_shapes();
  test_geometry_edit_applier_moves_regular_net_wire_segment_back_to_idb();
  test_geometry_edit_applier_resizes_regular_net_wire_segment_back_to_idb();
  test_geometry_edit_applier_moves_special_net_wire_segment_back_to_idb();
  test_geometry_edit_applier_resizes_special_net_wire_segment_back_to_idb();
  test_geometry_edit_applier_resizes_short_horizontal_special_wire_by_points();
  test_geometry_edit_applier_updates_blockage_rect_back_to_idb();
  test_geometry_edit_applier_updates_fill_rect_back_to_idb();
  test_geometry_edit_applier_updates_region_boundary_rect_back_to_idb();
  test_geometry_edit_applier_updates_slot_rect_back_to_idb();
  test_geometry_snapshot_writer_writes_manifest_and_core_binary_files();
  test_geometry_builder_collects_site_and_master_metadata();
  test_geometry_snapshot_exporter_writes_current_idb_design();
  test_geometry_snapshot_writer_switches_epoch_without_overwriting_previous_files();
  test_geometry_snapshot_reader_round_trips_core_binary_files();
  test_geometry_snapshot_reload_preserves_shape_id_and_version_during_rebuild();
  test_geometry_snapshot_apply_edit_on_reloaded_store_is_incremental_without_rebuild();
  return 0;
}
