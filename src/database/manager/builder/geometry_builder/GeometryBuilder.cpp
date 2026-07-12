#include "GeometryBuilder.h"

#include "IdbBlockages.h"
#include "IdbDesign.h"
#include "IdbGCellGrid.h"
#include "IdbInstance.h"
#include "IdbLayout.h"
#include "IdbNet.h"
#include "IdbRegularWire.h"
#include "IdbSpecialNet.h"
#include "IdbSpecialWire.h"
#include "IdbTrackGrid.h"
#include "IdbVias.h"

#include <algorithm>
#include <limits>
#include <unordered_set>
#include <vector>

namespace ecc::geometry {

namespace {

constexpr LayerId kLayoutGeometryLayer = 0;
constexpr OwnerId kDerivedOwnerPayloadMask = 0x00ffffffffffffffULL;

OwnerId make_derived_owner_id(OwnerType parent_type, OwnerId parent_owner_id)
{
  return (static_cast<OwnerId>(static_cast<uint8_t>(parent_type)) << 56U)
         | (parent_owner_id & kDerivedOwnerPayloadMask);
}

Rect32 rect_from_idb(idb::IdbRect* rect)
{
  if (rect == nullptr) {
    return Rect32{};
  }

  return Rect32{rect->get_low_x(), rect->get_low_y(), rect->get_high_x(), rect->get_high_y()};
}

Rect32 rect_from_idb(idb::IdbRect& rect)
{
  return Rect32{rect.get_low_x(), rect.get_low_y(), rect.get_high_x(), rect.get_high_y()};
}

LayerId layer_id_from_idb(idb::IdbLayer* layer)
{
  if (layer == nullptr) {
    return kLayoutGeometryLayer;
  }

  const int32_t layer_id = layer->get_id();
  return layer_id > 0 ? static_cast<LayerId>(layer_id) : static_cast<LayerId>(layer->get_order());
}

const char* layer_type_name(idb::IdbLayerType type)
{
  switch (type) {
    case idb::IdbLayerType::kLayerCut:
      return "cut";
    case idb::IdbLayerType::kLayerImplant:
      return "implant";
    case idb::IdbLayerType::kLayerMasterslice:
      return "masterslice";
    case idb::IdbLayerType::kLayerOverlap:
      return "overlap";
    case idb::IdbLayerType::kLayerRouting:
      return "routing";
    default:
      return "unknown";
  }
}

const char* layer_direction_name(idb::IdbLayerDirection direction)
{
  switch (direction) {
    case idb::IdbLayerDirection::kHorizontal:
      return "horizontal";
    case idb::IdbLayerDirection::kVertical:
      return "vertical";
    case idb::IdbLayerDirection::kDiag45:
      return "diag45";
    case idb::IdbLayerDirection::kDiag135:
      return "diag135";
    default:
      return "unknown";
  }
}

GeometryLayerMetadata layer_metadata_from_idb(idb::IdbLayer* layer)
{
  GeometryLayerMetadata metadata;
  if (layer == nullptr) {
    return metadata;
  }

  metadata.layer_id = layer_id_from_idb(layer);
  metadata.order = layer->get_order();
  metadata.name = layer->get_name();
  metadata.type = layer_type_name(layer->get_type());

  if (auto* routing_layer = dynamic_cast<idb::IdbLayerRouting*>(layer); routing_layer != nullptr) {
    metadata.direction = layer_direction_name(routing_layer->get_direction());
    metadata.width = routing_layer->get_width();
    metadata.pitch_x = routing_layer->get_pitch_x();
    metadata.pitch_y = routing_layer->get_pitch_y();
  } else if (auto* cut_layer = dynamic_cast<idb::IdbLayerCut*>(layer); cut_layer != nullptr) {
    metadata.width = cut_layer->get_width();
  }

  return metadata;
}

bool is_non_empty(Rect32 rect)
{
  rect = normalize(rect);
  return rect.lx != rect.hx || rect.ly != rect.hy;
}

bool is_same_rect(Rect32 lhs, Rect32 rhs)
{
  lhs = normalize(lhs);
  rhs = normalize(rhs);
  return lhs.lx == rhs.lx && lhs.ly == rhs.ly && lhs.hx == rhs.hx && lhs.hy == rhs.hy;
}

ShapeId emit_rect_if_present(GeometryStore& store, LayerId layer_id, Rect32 rect, OwnerRef owner)
{
  if (!is_non_empty(rect)) {
    return 0;
  }

  return store.add_rect(layer_id, rect, owner);
}

ShapeId emit_layout_rect_if_present(GeometryStore& store, Rect32 rect, OwnerRef owner)
{
  return emit_rect_if_present(store, kLayoutGeometryLayer, rect, owner);
}

Rect32 grid_reference_bounds(idb::IdbLayout& layout)
{
  if (auto* die = layout.get_die(); die != nullptr) {
    const Rect32 bounds = rect_from_idb(die->get_bounding_box());
    if (is_non_empty(bounds)) {
      return normalize(bounds);
    }
  }

  if (layout.get_rows() != nullptr && layout.get_rows()->get_row_num() > 0) {
    auto* core = layout.get_core();
    const Rect32 bounds = rect_from_idb(core->get_bounding_box());
    if (is_non_empty(bounds)) {
      return normalize(bounds);
    }
  }

  return {};
}

bool coordinate_to_i32(int64_t coordinate, int32_t& out)
{
  if (coordinate < std::numeric_limits<int32_t>::min() || coordinate > std::numeric_limits<int32_t>::max()) {
    return false;
  }
  out = static_cast<int32_t>(coordinate);
  return true;
}

bool make_grid_line(idb::IdbTrackDirection direction, int32_t coordinate, Rect32 bounds, int32_t width, LinePayload& line)
{
  bounds = normalize(bounds);
  if (!is_non_empty(bounds)) {
    return false;
  }

  line.width = width > 0 ? width : 1;
  line.flags = 0;

  switch (direction) {
    case idb::IdbTrackDirection::kDirectionX:
      line.begin = Point32{coordinate, bounds.ly};
      line.end = Point32{coordinate, bounds.hy};
      return true;
    case idb::IdbTrackDirection::kDirectionY:
      line.begin = Point32{bounds.lx, coordinate};
      line.end = Point32{bounds.hx, coordinate};
      return true;
    default:
      return false;
  }
}

uint64_t emit_track_grid_lines(GeometryStore& store, idb::IdbTrackGrid* track_grid, Rect32 bounds, OwnerId owner_id,
                               idb::IdbLayer* fallback_layer)
{
  if (track_grid == nullptr || track_grid->get_track() == nullptr || track_grid->get_track_num() == 0) {
    return 0;
  }

  idb::IdbTrack* track = track_grid->get_track();
  const uint32_t pitch = track->get_pitch();
  if (pitch == 0) {
    return 0;
  }

  std::vector<idb::IdbLayer*> layers = track_grid->get_layer_list();
  if (layers.empty() && fallback_layer != nullptr) {
    layers.push_back(fallback_layer);
  }
  if (layers.empty()) {
    layers.push_back(nullptr);
  }

  uint64_t shape_count = 0;
  uint32_t layer_index = 0;
  for (auto* layer : layers) {
    const LayerId layer_id = layer_id_from_idb(layer);
    for (uint32_t track_index = 0; track_index < track_grid->get_track_num(); ++track_index) {
      int32_t coordinate = 0;
      if (!coordinate_to_i32(static_cast<int64_t>(track->get_start()) + static_cast<int64_t>(track_index) * pitch, coordinate)) {
        continue;
      }

      LinePayload line;
      if (!make_grid_line(track->get_direction(), coordinate, bounds, static_cast<int32_t>(track->get_width()), line)) {
        continue;
      }

      OwnerRef owner;
      owner.type = OwnerType::kTrackGrid;
      owner.owner_id = owner_id;
      owner.path0 = layer_index;
      owner.path1 = track_index;
      owner.path2 = static_cast<uint32_t>(track->get_direction());

      if (store.add_line(layer_id, line, owner) != 0) {
        ++shape_count;
      }
    }

    ++layer_index;
  }

  return shape_count;
}

uint64_t emit_gcell_grid_lines(GeometryStore& store, idb::IdbGCellGrid* gcell_grid, Rect32 bounds, OwnerId owner_id)
{
  if (gcell_grid == nullptr || gcell_grid->get_num() <= 0 || gcell_grid->get_space() <= 0) {
    return 0;
  }

  uint64_t shape_count = 0;
  for (int32_t line_index = 0; line_index < gcell_grid->get_num(); ++line_index) {
    int32_t coordinate = 0;
    if (!coordinate_to_i32(static_cast<int64_t>(gcell_grid->get_start())
                               + static_cast<int64_t>(line_index) * gcell_grid->get_space(),
                           coordinate)) {
      continue;
    }

    LinePayload line;
    if (!make_grid_line(gcell_grid->get_direction(), coordinate, bounds, 1, line)) {
      continue;
    }

    OwnerRef owner;
    owner.type = OwnerType::kGCellGrid;
    owner.owner_id = owner_id;
    owner.path0 = static_cast<uint32_t>(line_index);
    owner.path1 = static_cast<uint32_t>(gcell_grid->get_direction());

    if (store.add_line(kLayoutGeometryLayer, line, owner) != 0) {
      ++shape_count;
    }
  }

  return shape_count;
}

uint64_t emit_layer_shape_rects(GeometryStore& store, idb::IdbLayerShape& layer_shape, OwnerRef owner)
{
  uint64_t shape_count = 0;
  uint32_t rect_index = 0;
  for (auto* rect : layer_shape.get_rect_list()) {
    OwnerRef rect_owner = owner;
    rect_owner.path3 = rect_index++;
    if (emit_rect_if_present(store, layer_id_from_idb(layer_shape.get_layer()), rect_from_idb(rect), rect_owner) != 0) {
      ++shape_count;
    }
  }

  return shape_count;
}

uint64_t emit_via_cut_shapes(GeometryStore& store, idb::IdbVia* via, OwnerRef owner)
{
  if (via == nullptr) {
    return 0;
  }

  if (via->get_instance() == nullptr || via->get_instance()->get_cut_layer_shape() == nullptr) {
    return 0;
  }

  idb::IdbLayerShape cut_shape = via->get_cut_layer_shape();
  return emit_layer_shape_rects(store, cut_shape, owner);
}

uint64_t emit_via_cut_shapes_at(GeometryStore& store, idb::IdbVia* via, idb::IdbCoordinate<int32_t>* origin, OwnerRef owner)
{
  if (via == nullptr || origin == nullptr || via->get_instance() == nullptr
      || via->get_instance()->get_cut_layer_shape() == nullptr) {
    return 0;
  }

  idb::IdbLayerShape cut_shape;
  via->get_instance()->get_cut_layer_shape()->clone(cut_shape);
  cut_shape.moveToLocation(origin);
  return emit_layer_shape_rects(store, cut_shape, owner);
}

ShapeId find_shape_by_owner_path(const GeometryStore& store, OwnerType type, OwnerId owner_id, uint32_t path0, uint32_t path1,
                                 uint32_t path2, uint32_t path3)
{
  for (const ShapeId shape_id : store.query_owner(type, owner_id)) {
    const OwnerRef owner = store.owner_of(shape_id);
    if (owner.path0 == path0 && owner.path1 == path1 && owner.path2 == path2 && owner.path3 == path3) {
      return shape_id;
    }
  }

  return 0;
}

bool resolve_regular_net_owner_id(idb::IdbDesign& design, idb::IdbNet& net, OwnerId& owner_id)
{
  auto* net_list = design.get_net_list();
  if (net_list == nullptr) {
    return false;
  }

  uint32_t net_index = 0;
  for (auto* candidate : net_list->get_net_list()) {
    if (candidate == &net) {
      owner_id = net.get_id() != 0 ? net.get_id() : net_index;
      return true;
    }
    ++net_index;
  }

  return false;
}

bool resolve_special_net_owner_id(idb::IdbDesign& design, idb::IdbSpecialNet& net, OwnerId& owner_id)
{
  auto* net_list = design.get_special_net_list();
  if (net_list == nullptr) {
    return false;
  }

  uint32_t net_index = 0;
  for (auto* candidate : net_list->get_net_list()) {
    if (candidate == &net) {
      owner_id = net_index;
      return true;
    }
    ++net_index;
  }

  return false;
}

bool resolve_blockage_owner_id(idb::IdbDesign& design, idb::IdbBlockage& blockage, OwnerId& owner_id)
{
  auto* blockage_list = design.get_blockage_list();
  if (blockage_list == nullptr) {
    return false;
  }

  uint32_t blockage_index = 0;
  for (auto* candidate : blockage_list->get_blockage_list()) {
    if (candidate == &blockage) {
      owner_id = blockage_index;
      return true;
    }
    ++blockage_index;
  }

  return false;
}

void reconcile_rect_shape(GeometryStore& store, OwnerType type, OwnerId owner_id, LayerId layer_id, Rect32 bbox, OwnerRef owner,
                          std::unordered_set<ShapeId>& seen_shape_ids, GeometrySyncResult& result)
{
  bbox = normalize(bbox);
  const ShapeId shape_id = find_shape_by_owner_path(store, type, owner_id, owner.path0, owner.path1, owner.path2, owner.path3);
  if (shape_id == 0) {
    const ShapeId added_shape_id = store.add_rect(layer_id, bbox, owner);
    if (added_shape_id != 0) {
      seen_shape_ids.insert(added_shape_id);
      ++result.added_shape_count;
    } else {
      ++result.missing_shape_count;
    }
    return;
  }

  seen_shape_ids.insert(shape_id);

  const ShapeRecord* record = store.find_shape(shape_id);
  if (record == nullptr || record->state != ShapeState::kAlive || record->kind != ShapeKind::kRect) {
    ++result.missing_shape_count;
    return;
  }

  if (record->layer_id != layer_id) {
    const bool deleted = store.delete_shape(shape_id);
    const ShapeId added_shape_id = deleted ? store.add_rect(layer_id, bbox, owner) : 0;
    if (deleted && added_shape_id != 0) {
      seen_shape_ids.insert(added_shape_id);
      ++result.deleted_shape_count;
      ++result.added_shape_count;
    } else {
      ++result.missing_shape_count;
    }
    return;
  }

  if (is_same_rect(record->bbox, bbox)) {
    return;
  }

  if (store.update_rect(shape_id, bbox)) {
    ++result.updated_shape_count;
  } else {
    ++result.missing_shape_count;
  }
}

void reconcile_layer_shape_rects(GeometryStore& store, idb::IdbLayerShape& layer_shape, OwnerRef owner,
                                 std::unordered_set<ShapeId>& seen_shape_ids, GeometrySyncResult& result)
{
  uint32_t rect_index = 0;
  for (auto* rect : layer_shape.get_rect_list()) {
    OwnerRef rect_owner = owner;
    rect_owner.path3 = rect_index++;
    const Rect32 bbox = rect_from_idb(rect);
    if (is_non_empty(bbox)) {
      reconcile_rect_shape(store, rect_owner.type, rect_owner.owner_id, layer_id_from_idb(layer_shape.get_layer()), bbox, rect_owner,
                           seen_shape_ids, result);
    }
  }
}

void reconcile_via_cut_shapes(GeometryStore& store, idb::IdbVia* via, OwnerRef owner,
                              std::unordered_set<ShapeId>& seen_shape_ids, GeometrySyncResult& result)
{
  if (via == nullptr || via->get_instance() == nullptr || via->get_instance()->get_cut_layer_shape() == nullptr) {
    return;
  }

  idb::IdbLayerShape cut_shape = via->get_cut_layer_shape();
  reconcile_layer_shape_rects(store, cut_shape, owner, seen_shape_ids, result);
}

Rect32 rect_from_regular_segment(idb::IdbRegularWireSegment* segment)
{
  if (segment == nullptr) {
    return Rect32{};
  }

  if (segment->is_rect()) {
    return rect_from_idb(segment->get_delta_rect());
  }

  if (segment->is_wire() && segment->get_layer() != nullptr) {
    idb::IdbRect rect = segment->get_segment_rect();
    return rect_from_idb(rect);
  }

  return Rect32{};
}

Rect32 rect_from_special_segment(idb::IdbSpecialWireSegment* segment)
{
  if (segment == nullptr) {
    return Rect32{};
  }

  if (segment->is_rect()) {
    return rect_from_idb(segment->get_delta_rect());
  }

  if (is_non_empty(rect_from_idb(segment->get_bounding_box()))) {
    return rect_from_idb(segment->get_bounding_box());
  }

  if (segment->get_point_num() < 2 || segment->get_route_width() < 0) {
    return Rect32{};
  }

  idb::IdbCoordinate<int32_t>* begin = segment->get_point_start();
  idb::IdbCoordinate<int32_t>* end = segment->get_point_second();
  if (begin == nullptr || end == nullptr) {
    return Rect32{};
  }

  const int32_t half_width = segment->get_route_width() / 2;
  if (begin->get_y() == end->get_y()) {
    return Rect32{std::min(begin->get_x(), end->get_x()), begin->get_y() - half_width,
                  std::max(begin->get_x(), end->get_x()), begin->get_y() + segment->get_route_width() - half_width};
  }

  return Rect32{begin->get_x() - half_width, std::min(begin->get_y(), end->get_y()),
                begin->get_x() + segment->get_route_width() - half_width, std::max(begin->get_y(), end->get_y())};
}

struct PinPortShapeCounts
{
  uint64_t port_shape_count = 0;
  uint64_t via_shape_count = 0;
};

OwnerId pin_owner_id_from_path(idb::IdbPin* pin, uint32_t path0, uint32_t path1)
{
  if (pin == nullptr) {
    return 0;
  }

  return pin->get_id() != 0 ? pin->get_id() : (static_cast<OwnerId>(path0) << 32U | path1);
}

PinPortShapeCounts emit_pin_port_shapes(GeometryStore& store, idb::IdbPin* pin, uint32_t path0, uint32_t path1)
{
  if (pin == nullptr) {
    return {};
  }

  const OwnerId pin_owner_id = pin_owner_id_from_path(pin, path0, path1);
  store.add_owner_name(OwnerType::kPinPortShape, pin_owner_id, pin->get_pin_name());
  const OwnerId via_owner_id = make_derived_owner_id(OwnerType::kPinPortShape, pin_owner_id);
  store.add_owner_name(OwnerType::kVia, via_owner_id, pin->get_pin_name());

  PinPortShapeCounts counts;
  uint32_t layer_shape_index = 0;
  for (auto* layer_shape : pin->get_port_box_list()) {
    if (layer_shape == nullptr) {
      ++layer_shape_index;
      continue;
    }

    uint32_t rect_index = 0;
    for (auto* rect : layer_shape->get_rect_list()) {
      OwnerRef owner;
      owner.type = OwnerType::kPinPortShape;
      owner.owner_id = pin_owner_id;
      owner.path0 = path0;
      owner.path1 = path1;
      owner.path2 = layer_shape_index;
      owner.path3 = rect_index++;

      if (emit_rect_if_present(store, layer_id_from_idb(layer_shape->get_layer()), rect_from_idb(rect), owner) != 0) {
        ++counts.port_shape_count;
      }
    }

    ++layer_shape_index;
  }

  uint32_t via_index = 0;
  for (auto* via : pin->get_via_list()) {
    OwnerRef owner;
    owner.type = OwnerType::kVia;
    owner.owner_id = via_owner_id;
    owner.path0 = path0;
    owner.path1 = path1;
    owner.path2 = via_index++;
    counts.via_shape_count += emit_via_cut_shapes(store, via, owner);
  }

  return counts;
}

void reconcile_pin_port_shapes(GeometryStore& store, idb::IdbPin* pin, uint32_t path0, uint32_t path1,
                               std::unordered_set<ShapeId>& seen_pin_shape_ids,
                               std::unordered_set<ShapeId>& seen_via_shape_ids, GeometrySyncResult& result)
{
  if (pin == nullptr) {
    return;
  }

  const OwnerId pin_owner_id = pin_owner_id_from_path(pin, path0, path1);
  uint32_t layer_shape_index = 0;
  for (auto* layer_shape : pin->get_port_box_list()) {
    if (layer_shape == nullptr) {
      ++layer_shape_index;
      continue;
    }

    uint32_t rect_index = 0;
    for (auto* rect : layer_shape->get_rect_list()) {
      OwnerRef owner;
      owner.type = OwnerType::kPinPortShape;
      owner.owner_id = pin_owner_id;
      owner.path0 = path0;
      owner.path1 = path1;
      owner.path2 = layer_shape_index;
      owner.path3 = rect_index++;
      reconcile_rect_shape(store, owner.type, owner.owner_id, layer_id_from_idb(layer_shape->get_layer()), rect_from_idb(rect),
                           owner, seen_pin_shape_ids, result);
    }

    ++layer_shape_index;
  }

  const OwnerId via_owner_id = make_derived_owner_id(OwnerType::kPinPortShape, pin_owner_id);
  uint32_t via_index = 0;
  for (auto* via : pin->get_via_list()) {
    OwnerRef owner;
    owner.type = OwnerType::kVia;
    owner.owner_id = via_owner_id;
    owner.path0 = path0;
    owner.path1 = path1;
    owner.path2 = via_index++;
    reconcile_via_cut_shapes(store, via, owner, seen_via_shape_ids, result);
  }
}

}  // namespace

GeometryBuildResult GeometryBuilder::rebuild_from_design(idb::IdbDesign& design, idb::IdbLayout& layout, GeometryStore& store) const
{
  store.clear_preserving_shape_ids();

  GeometryBuildResult result;

  if (auto* die = layout.get_die(); die != nullptr) {
    OwnerRef owner;
    owner.type = OwnerType::kDie;
    owner.owner_id = die->get_id();
    if (emit_layout_rect_if_present(store, rect_from_idb(die->get_bounding_box()), owner) != 0) {
      ++result.die_shape_count;
    }
  }

  if (layout.get_rows() != nullptr && layout.get_rows()->get_row_num() > 0) {
    auto* core = layout.get_core();
    OwnerRef owner;
    owner.type = OwnerType::kCore;
    owner.owner_id = core->get_id();
    if (emit_layout_rect_if_present(store, rect_from_idb(core->get_bounding_box()), owner) != 0) {
      ++result.core_shape_count;
    }
  }

  if (auto* rows = layout.get_rows(); rows != nullptr) {
    uint32_t row_index = 0;
    for (auto* row : rows->get_row_list()) {
      if (row == nullptr) {
        ++row_index;
        continue;
      }

      OwnerRef owner;
      owner.type = OwnerType::kRow;
      owner.owner_id = row->get_id();
      owner.path0 = row_index++;
      if (emit_layout_rect_if_present(store, rect_from_idb(row->get_bounding_box()), owner) != 0) {
        ++result.row_shape_count;
      }
    }
  }

  const Rect32 grid_bounds = grid_reference_bounds(layout);
  std::unordered_set<idb::IdbTrackGrid*> emitted_track_grids;
  OwnerId track_grid_owner_id = 0;
  if (auto* track_grids = layout.get_track_grid_list(); track_grids != nullptr) {
    for (auto* track_grid : track_grids->get_track_grid_list()) {
      if (track_grid == nullptr || !emitted_track_grids.insert(track_grid).second) {
        continue;
      }
      result.track_grid_shape_count += emit_track_grid_lines(store, track_grid, grid_bounds, track_grid_owner_id++, nullptr);
    }
  }

  if (auto* layers = layout.get_layers(); layers != nullptr) {
    for (auto* layer : layers->get_layers()) {
      auto* routing_layer = dynamic_cast<idb::IdbLayerRouting*>(layer);
      if (routing_layer == nullptr) {
        continue;
      }

      for (auto* track_grid : routing_layer->get_track_grid_list()) {
        if (track_grid == nullptr || !emitted_track_grids.insert(track_grid).second) {
          continue;
        }
        result.track_grid_shape_count += emit_track_grid_lines(store, track_grid, grid_bounds, track_grid_owner_id++, routing_layer);
      }
    }
  }

  if (auto* gcell_grids = layout.get_gcell_grid_list(); gcell_grids != nullptr) {
    OwnerId gcell_grid_owner_id = 0;
    for (auto* gcell_grid : gcell_grids->get_gcell_grid_list()) {
      result.gcell_grid_shape_count += emit_gcell_grid_lines(store, gcell_grid, grid_bounds, gcell_grid_owner_id++);
    }
  }

  if (auto* instances = design.get_instance_list(); instances != nullptr) {
    uint32_t instance_index = 0;
    for (auto* instance : instances->get_instance_list()) {
      if (instance == nullptr) {
        ++instance_index;
        continue;
      }

      if (!instance->is_fixed() && !instance->is_cover() && !instance->is_placed()) {
        ++instance_index;
        continue;
      }

      const OwnerId instance_owner_id = instance->get_id() != 0 ? instance->get_id() : instance_index;
      OwnerRef owner;
      owner.type = OwnerType::kInstanceBBox;
      owner.owner_id = instance_owner_id;
      owner.path0 = instance_index;
      store.add_owner_name(owner.type, owner.owner_id, instance->get_name());
      if (emit_layout_rect_if_present(store, rect_from_idb(instance->get_bounding_box()), owner) != 0) {
        ++result.instance_shape_count;
      }

      if (auto* halo = instance->get_halo(); halo != nullptr) {
        instance->set_halo_coodinate();

        OwnerRef halo_owner;
        halo_owner.type = OwnerType::kInstanceHalo;
        halo_owner.owner_id = instance_owner_id;
        halo_owner.path0 = instance_index;
        store.add_owner_name(halo_owner.type, halo_owner.owner_id, instance->get_name());
        if (emit_layout_rect_if_present(store, rect_from_idb(halo->get_bounding_box()), halo_owner) != 0) {
          ++result.instance_halo_shape_count;
        }
      }

      if (auto* pins = instance->get_pin_list(); pins != nullptr) {
        uint32_t pin_index = 0;
        for (auto* pin : pins->get_pin_list()) {
          const PinPortShapeCounts pin_counts = emit_pin_port_shapes(store, pin, instance_index, pin_index++);
          result.pin_shape_count += pin_counts.port_shape_count;
          result.via_shape_count += pin_counts.via_shape_count;
        }
      }

      if (instance->get_cell_master() != nullptr) {
        store.add_owner_name(OwnerType::kObs, instance_owner_id, instance->get_name());
        instance->set_obs_box_list();
        uint32_t obs_layer_index = 0;
        for (auto* obs_shape : instance->get_obs_box_list()) {
          if (obs_shape == nullptr) {
            ++obs_layer_index;
            continue;
          }

          uint32_t rect_index = 0;
          for (auto* rect : obs_shape->get_rect_list()) {
            OwnerRef obs_owner;
            obs_owner.type = OwnerType::kObs;
            obs_owner.owner_id = instance_owner_id;
            obs_owner.path0 = instance_index;
            obs_owner.path1 = obs_layer_index;
            obs_owner.path2 = rect_index++;

            if (emit_rect_if_present(store, layer_id_from_idb(obs_shape->get_layer()), rect_from_idb(rect), obs_owner) != 0) {
              ++result.obs_shape_count;
            }
          }

          ++obs_layer_index;
        }
      }

      ++instance_index;
    }
  }

  if (auto* io_pins = design.get_io_pin_list(); io_pins != nullptr) {
    uint32_t pin_index = 0;
    for (auto* pin : io_pins->get_pin_list()) {
      const PinPortShapeCounts pin_counts = emit_pin_port_shapes(store, pin, 0, pin_index++);
      result.pin_shape_count += pin_counts.port_shape_count;
      result.via_shape_count += pin_counts.via_shape_count;
    }
  }

  if (auto* nets = design.get_net_list(); nets != nullptr) {
    uint32_t net_index = 0;
    for (auto* net : nets->get_net_list()) {
      if (net == nullptr) {
        ++net_index;
        continue;
      }

      const OwnerId net_owner_id = net->get_id() != 0 ? net->get_id() : net_index;
      store.add_owner_name(OwnerType::kNetWireSegment, net_owner_id, net->get_net_name());
      const OwnerId via_owner_id = make_derived_owner_id(OwnerType::kNetWireSegment, net_owner_id);
      store.add_owner_name(OwnerType::kVia, via_owner_id, net->get_net_name());

      uint32_t wire_index = 0;
      for (auto* wire : net->get_wire_list()->get_wire_list()) {
        if (wire == nullptr) {
          ++wire_index;
          continue;
        }

        uint32_t segment_index = 0;
        for (auto* segment : wire->get_segment_list()) {
          const uint32_t current_segment_index = segment_index++;
          OwnerRef owner;
          owner.type = OwnerType::kNetWireSegment;
          owner.owner_id = net_owner_id;
          owner.path0 = wire_index;
          owner.path1 = current_segment_index;

          if (emit_rect_if_present(store, layer_id_from_idb(segment == nullptr ? nullptr : segment->get_layer()),
                                   rect_from_regular_segment(segment), owner) != 0) {
            ++result.net_wire_shape_count;
          }

          if (segment != nullptr) {
            uint32_t via_index = 0;
            for (auto* via : segment->get_via_list()) {
              OwnerRef via_owner;
              via_owner.type = OwnerType::kVia;
              via_owner.owner_id = via_owner_id;
              via_owner.path0 = wire_index;
              via_owner.path1 = current_segment_index;
              via_owner.path2 = via_index++;
              result.via_shape_count += emit_via_cut_shapes(store, via, via_owner);
            }
          }
        }

        ++wire_index;
      }

      ++net_index;
    }
  }

  if (auto* special_nets = design.get_special_net_list(); special_nets != nullptr) {
    uint32_t net_index = 0;
    for (auto* net : special_nets->get_net_list()) {
      if (net == nullptr) {
        ++net_index;
        continue;
      }

      const OwnerId net_owner_id = net_index;
      store.add_owner_name(OwnerType::kSpecialWireSegment, net_owner_id, net->get_net_name());
      const OwnerId via_owner_id = make_derived_owner_id(OwnerType::kSpecialWireSegment, net_owner_id);
      store.add_owner_name(OwnerType::kVia, via_owner_id, net->get_net_name());

      uint32_t wire_index = 0;
      for (auto* wire : net->get_wire_list()->get_wire_list()) {
        if (wire == nullptr) {
          ++wire_index;
          continue;
        }

        uint32_t segment_index = 0;
        for (auto* segment : wire->get_segment_list()) {
          const uint32_t current_segment_index = segment_index++;
          OwnerRef owner;
          owner.type = OwnerType::kSpecialWireSegment;
          owner.owner_id = net_owner_id;
          owner.path0 = wire_index;
          owner.path1 = current_segment_index;

          if (segment != nullptr && !segment->is_via()
              && emit_rect_if_present(store, layer_id_from_idb(segment->get_layer()), rect_from_special_segment(segment), owner)
                     != 0) {
            ++result.special_net_wire_shape_count;
          }

          if (segment != nullptr && segment->get_via() != nullptr) {
            OwnerRef via_owner;
            via_owner.type = OwnerType::kVia;
            via_owner.owner_id = via_owner_id;
            via_owner.path0 = wire_index;
            via_owner.path1 = current_segment_index;
            via_owner.path2 = 0;
            result.via_shape_count += emit_via_cut_shapes(store, segment->get_via(), via_owner);
          }
        }

        ++wire_index;
      }

      ++net_index;
    }
  }

  if (auto* blockages = design.get_blockage_list(); blockages != nullptr) {
    uint32_t blockage_index = 0;
    for (auto* blockage : blockages->get_blockage_list()) {
      idb::IdbLayer* layer = nullptr;
      if (auto* routing_blockage = dynamic_cast<idb::IdbRoutingBlockage*>(blockage); routing_blockage != nullptr) {
        layer = routing_blockage->get_layer();
      }

      uint32_t rect_index = 0;
      for (auto* rect : blockage == nullptr ? std::vector<idb::IdbRect*>{} : blockage->get_rect_list()) {
        OwnerRef owner;
        owner.type = OwnerType::kBlockage;
        owner.owner_id = blockage_index;
        owner.path0 = rect_index++;

        if (emit_rect_if_present(store, layer_id_from_idb(layer), rect_from_idb(rect), owner) != 0) {
          ++result.blockage_shape_count;
        }
      }

      ++blockage_index;
    }
  }

  if (auto* fills = design.get_fill_list(); fills != nullptr) {
    uint32_t fill_index = 0;
    for (auto* fill : fills->get_fill_list()) {
      if (fill == nullptr) {
        ++fill_index;
        continue;
      }

      if (fill->get_type() == idb::IdbFill::IdbFillType::kLayer && fill->get_layer() != nullptr) {
        uint32_t rect_index = 0;
        for (auto* rect : fill->get_layer()->get_rect_list()) {
          OwnerRef owner;
          owner.type = OwnerType::kFill;
          owner.owner_id = fill_index;
          owner.path0 = rect_index++;

          if (emit_rect_if_present(store, layer_id_from_idb(fill->get_layer()->get_layer()), rect_from_idb(rect), owner)
              != 0) {
            ++result.fill_shape_count;
          }
        }
      } else if (fill->get_type() == idb::IdbFill::IdbFillType::kVia && fill->get_via() != nullptr
                 && fill->get_via()->get_via() != nullptr) {
        uint32_t coordinate_index = 0;
        for (auto* coordinate : fill->get_via()->get_coordinate_list()) {
          OwnerRef owner;
          owner.type = OwnerType::kFill;
          owner.owner_id = fill_index;
          owner.path0 = coordinate_index++;

          result.fill_shape_count += emit_via_cut_shapes_at(store, fill->get_via()->get_via(), coordinate, owner);
        }
      }

      ++fill_index;
    }
  }

  if (auto* regions = design.get_region_list(); regions != nullptr) {
    uint32_t region_index = 0;
    for (auto* region : regions->get_region_list()) {
      if (region != nullptr) {
        store.add_owner_name(OwnerType::kRegion, region_index, region->get_name());
      }

      uint32_t boundary_index = 0;
      for (auto* rect : region == nullptr ? std::vector<idb::IdbRect*>{} : region->get_boundary()) {
        OwnerRef owner;
        owner.type = OwnerType::kRegion;
        owner.owner_id = region_index;
        owner.path0 = boundary_index++;

        if (emit_layout_rect_if_present(store, rect_from_idb(rect), owner) != 0) {
          ++result.region_shape_count;
        }
      }

      ++region_index;
    }
  }

  if (auto* slots = design.get_slot_list(); slots != nullptr) {
    uint32_t slot_index = 0;
    for (auto* slot : slots->get_slot_list()) {
      uint32_t rect_index = 0;
      for (auto* rect : slot == nullptr ? std::vector<idb::IdbRect*>{} : slot->get_rect_list()) {
        OwnerRef owner;
        owner.type = OwnerType::kSlot;
        owner.owner_id = slot_index;
        owner.path0 = rect_index++;

        if (emit_rect_if_present(store, layer_id_from_idb(slot == nullptr ? nullptr : slot->get_layer()), rect_from_idb(rect),
                                 owner) != 0) {
          ++result.slot_shape_count;
        }
      }

      ++slot_index;
    }
  }

  result.shape_count = result.die_shape_count + result.core_shape_count + result.row_shape_count + result.track_grid_shape_count
                       + result.gcell_grid_shape_count + result.instance_shape_count + result.instance_halo_shape_count
                       + result.net_wire_shape_count + result.special_net_wire_shape_count + result.via_shape_count
                       + result.blockage_shape_count + result.fill_shape_count + result.region_shape_count + result.slot_shape_count
                       + result.pin_shape_count + result.obs_shape_count;
  store.clear_delta_events();
  return result;
}

std::vector<GeometryLayerMetadata> GeometryBuilder::collect_layer_metadata(idb::IdbLayout& layout) const
{
  std::vector<GeometryLayerMetadata> layers;
  auto* idb_layers = layout.get_layers();
  if (idb_layers == nullptr) {
    return layers;
  }

  for (auto* layer : idb_layers->get_layers()) {
    if (layer == nullptr) {
      continue;
    }
    layers.push_back(layer_metadata_from_idb(layer));
  }

  std::sort(layers.begin(), layers.end(), [](const GeometryLayerMetadata& lhs, const GeometryLayerMetadata& rhs) {
    if (lhs.order != rhs.order) {
      return lhs.order < rhs.order;
    }
    return lhs.layer_id < rhs.layer_id;
  });
  return layers;
}

GeometrySyncResult GeometryBuilder::sync_net(idb::IdbDesign& design, idb::IdbNet& net, GeometryStore& store) const
{
  GeometrySyncResult result;
  OwnerId owner_id = 0;
  if (!resolve_regular_net_owner_id(design, net, owner_id)) {
    result.missing_shape_count = 1;
    return result;
  }

  const OwnerId via_owner_id = make_derived_owner_id(OwnerType::kNetWireSegment, owner_id);

  std::unordered_set<ShapeId> seen_shape_ids;
  std::unordered_set<ShapeId> seen_via_shape_ids;
  if (auto* wire_list = net.get_wire_list(); wire_list != nullptr) {
    uint32_t wire_index = 0;
    for (auto* wire : wire_list->get_wire_list()) {
      if (wire == nullptr) {
        ++wire_index;
        continue;
      }

      uint32_t segment_index = 0;
      for (auto* segment : wire->get_segment_list()) {
        const uint32_t current_segment_index = segment_index++;
        OwnerRef owner;
        owner.type = OwnerType::kNetWireSegment;
        owner.owner_id = owner_id;
        owner.path0 = wire_index;
        owner.path1 = current_segment_index;

        const Rect32 bbox = rect_from_regular_segment(segment);
        if (is_non_empty(bbox)) {
          reconcile_rect_shape(store, owner.type, owner.owner_id, layer_id_from_idb(segment == nullptr ? nullptr : segment->get_layer()),
                               bbox, owner, seen_shape_ids, result);
        }

        if (segment != nullptr) {
          uint32_t via_index = 0;
          for (auto* via : segment->get_via_list()) {
            OwnerRef via_owner;
            via_owner.type = OwnerType::kVia;
            via_owner.owner_id = via_owner_id;
            via_owner.path0 = wire_index;
            via_owner.path1 = current_segment_index;
            via_owner.path2 = via_index++;
            reconcile_via_cut_shapes(store, via, via_owner, seen_via_shape_ids, result);
          }
        }
      }

      ++wire_index;
    }
  }

  for (const ShapeId shape_id : store.query_owner(OwnerType::kNetWireSegment, owner_id)) {
    if (seen_shape_ids.contains(shape_id)) {
      continue;
    }
    if (store.delete_shape(shape_id)) {
      ++result.deleted_shape_count;
    } else {
      ++result.missing_shape_count;
    }
  }

  for (const ShapeId shape_id : store.query_owner(OwnerType::kVia, via_owner_id)) {
    if (seen_via_shape_ids.contains(shape_id)) {
      continue;
    }
    if (store.delete_shape(shape_id)) {
      ++result.deleted_shape_count;
    } else {
      ++result.missing_shape_count;
    }
  }

  result.ok = result.missing_shape_count == 0;
  return result;
}

GeometrySyncResult GeometryBuilder::sync_special_net(idb::IdbDesign& design, idb::IdbSpecialNet& net, GeometryStore& store) const
{
  GeometrySyncResult result;
  OwnerId owner_id = 0;
  if (!resolve_special_net_owner_id(design, net, owner_id)) {
    result.missing_shape_count = 1;
    return result;
  }

  const OwnerId via_owner_id = make_derived_owner_id(OwnerType::kSpecialWireSegment, owner_id);

  std::unordered_set<ShapeId> seen_shape_ids;
  std::unordered_set<ShapeId> seen_via_shape_ids;
  if (auto* wire_list = net.get_wire_list(); wire_list != nullptr) {
    uint32_t wire_index = 0;
    for (auto* wire : wire_list->get_wire_list()) {
      if (wire == nullptr) {
        ++wire_index;
        continue;
      }

      uint32_t segment_index = 0;
      for (auto* segment : wire->get_segment_list()) {
        const uint32_t current_segment_index = segment_index++;
        OwnerRef owner;
        owner.type = OwnerType::kSpecialWireSegment;
        owner.owner_id = owner_id;
        owner.path0 = wire_index;
        owner.path1 = current_segment_index;

        if (segment != nullptr && !segment->is_rect()) {
          segment->set_bounding_box();
        }

        const Rect32 bbox = segment != nullptr && !segment->is_via() ? rect_from_special_segment(segment) : Rect32{};
        if (is_non_empty(bbox)) {
          reconcile_rect_shape(store, owner.type, owner.owner_id, layer_id_from_idb(segment == nullptr ? nullptr : segment->get_layer()),
                               bbox, owner, seen_shape_ids, result);
        }

        if (segment != nullptr && segment->get_via() != nullptr) {
          OwnerRef via_owner;
          via_owner.type = OwnerType::kVia;
          via_owner.owner_id = via_owner_id;
          via_owner.path0 = wire_index;
          via_owner.path1 = current_segment_index;
          via_owner.path2 = 0;
          reconcile_via_cut_shapes(store, segment->get_via(), via_owner, seen_via_shape_ids, result);
        }
      }

      ++wire_index;
    }
  }

  for (const ShapeId shape_id : store.query_owner(OwnerType::kSpecialWireSegment, owner_id)) {
    if (seen_shape_ids.contains(shape_id)) {
      continue;
    }
    if (store.delete_shape(shape_id)) {
      ++result.deleted_shape_count;
    } else {
      ++result.missing_shape_count;
    }
  }

  for (const ShapeId shape_id : store.query_owner(OwnerType::kVia, via_owner_id)) {
    if (seen_via_shape_ids.contains(shape_id)) {
      continue;
    }
    if (store.delete_shape(shape_id)) {
      ++result.deleted_shape_count;
    } else {
      ++result.missing_shape_count;
    }
  }

  result.ok = result.missing_shape_count == 0;
  return result;
}

GeometrySyncResult GeometryBuilder::sync_blockage(idb::IdbDesign& design, idb::IdbBlockage& blockage, GeometryStore& store) const
{
  GeometrySyncResult result;
  OwnerId owner_id = 0;
  if (!resolve_blockage_owner_id(design, blockage, owner_id)) {
    result.missing_shape_count = 1;
    return result;
  }

  idb::IdbLayer* layer = nullptr;
  if (auto* routing_blockage = dynamic_cast<idb::IdbRoutingBlockage*>(&blockage); routing_blockage != nullptr) {
    layer = routing_blockage->get_layer();
  }

  std::unordered_set<ShapeId> seen_shape_ids;
  uint32_t rect_index = 0;
  for (auto* rect : blockage.get_rect_list()) {
    OwnerRef owner;
    owner.type = OwnerType::kBlockage;
    owner.owner_id = owner_id;
    owner.path0 = rect_index++;

    const Rect32 bbox = rect_from_idb(rect);
    if (is_non_empty(bbox)) {
      reconcile_rect_shape(store, owner.type, owner.owner_id, layer_id_from_idb(layer), bbox, owner, seen_shape_ids, result);
    }
  }

  for (const ShapeId shape_id : store.query_owner(OwnerType::kBlockage, owner_id)) {
    if (seen_shape_ids.contains(shape_id)) {
      continue;
    }
    if (store.delete_shape(shape_id)) {
      ++result.deleted_shape_count;
    } else {
      ++result.missing_shape_count;
    }
  }

  result.ok = result.missing_shape_count == 0;
  return result;
}

GeometrySyncResult GeometryBuilder::sync_instance(idb::IdbInstance& instance, GeometryStore& store) const
{
  GeometrySyncResult result;
  const OwnerId instance_owner_id = instance.get_id();
  uint32_t instance_path0 = 0;
  const std::vector<ShapeId> instance_shape_ids = store.query_owner(OwnerType::kInstanceBBox, instance_owner_id);
  if (!instance_shape_ids.empty()) {
    instance_path0 = store.owner_of(instance_shape_ids[0]).path0;
  }

  const auto sync_owner_rect = [&](OwnerType owner_type, Rect32 bbox) {
    const std::vector<ShapeId> shape_ids = store.query_owner(owner_type, instance_owner_id);
    if (shape_ids.empty()) {
      ++result.missing_shape_count;
      return;
    }

    for (const ShapeId shape_id : shape_ids) {
      const ShapeRecord* record = store.find_shape(shape_id);
      if (record == nullptr || record->state != ShapeState::kAlive || record->kind != ShapeKind::kRect) {
        ++result.missing_shape_count;
        continue;
      }

      if (is_same_rect(record->bbox, bbox)) {
        continue;
      }

      if (store.update_rect(shape_id, bbox)) {
        ++result.updated_shape_count;
      } else {
        ++result.missing_shape_count;
      }
    }
  };

  sync_owner_rect(OwnerType::kInstanceBBox, rect_from_idb(instance.get_bounding_box()));

  if (auto* halo = instance.get_halo(); halo != nullptr) {
    instance.set_halo_coodinate();
    sync_owner_rect(OwnerType::kInstanceHalo, rect_from_idb(halo->get_bounding_box()));
  }

  if (instance.get_cell_master() != nullptr) {
    instance.set_obs_box_list();
    std::unordered_set<ShapeId> seen_obs_shape_ids;
    uint32_t obs_layer_index = 0;
    for (auto* obs_shape : instance.get_obs_box_list()) {
      if (obs_shape == nullptr) {
        ++obs_layer_index;
        continue;
      }

      uint32_t rect_index = 0;
      for (auto* rect : obs_shape->get_rect_list()) {
        OwnerRef obs_owner;
        obs_owner.type = OwnerType::kObs;
        obs_owner.owner_id = instance_owner_id;
        obs_owner.path0 = instance_path0;
        obs_owner.path1 = obs_layer_index;
        obs_owner.path2 = rect_index++;

        reconcile_rect_shape(store, obs_owner.type, obs_owner.owner_id, layer_id_from_idb(obs_shape->get_layer()),
                             rect_from_idb(rect), obs_owner, seen_obs_shape_ids, result);
      }

      ++obs_layer_index;
    }

    for (const ShapeId shape_id : store.query_owner(OwnerType::kObs, instance_owner_id)) {
      if (seen_obs_shape_ids.contains(shape_id)) {
        continue;
      }
      if (store.delete_shape(shape_id)) {
        ++result.deleted_shape_count;
      } else {
        ++result.missing_shape_count;
      }
    }
  }

  if (auto* pins = instance.get_pin_list(); pins != nullptr) {
    std::unordered_set<ShapeId> seen_pin_shape_ids;
    std::unordered_set<ShapeId> seen_pin_via_shape_ids;
    std::vector<OwnerId> current_pin_owner_ids;
    std::vector<OwnerId> current_pin_via_owner_ids;
    uint32_t pin_index = 0;
    for (auto* pin : pins->get_pin_list()) {
      const OwnerId pin_owner_id = pin_owner_id_from_path(pin, instance_path0, pin_index);
      current_pin_owner_ids.push_back(pin_owner_id);
      current_pin_via_owner_ids.push_back(make_derived_owner_id(OwnerType::kPinPortShape, pin_owner_id));
      reconcile_pin_port_shapes(store, pin, instance_path0, pin_index++, seen_pin_shape_ids, seen_pin_via_shape_ids, result);
    }

    for (const OwnerId pin_owner_id : current_pin_owner_ids) {
      for (const ShapeId shape_id : store.query_owner(OwnerType::kPinPortShape, pin_owner_id)) {
        if (seen_pin_shape_ids.contains(shape_id)) {
          continue;
        }
        if (store.delete_shape(shape_id)) {
          ++result.deleted_shape_count;
        } else {
          ++result.missing_shape_count;
        }
      }
    }

    for (const OwnerId via_owner_id : current_pin_via_owner_ids) {
      for (const ShapeId shape_id : store.query_owner(OwnerType::kVia, via_owner_id)) {
        if (seen_pin_via_shape_ids.contains(shape_id)) {
          continue;
        }
        if (store.delete_shape(shape_id)) {
          ++result.deleted_shape_count;
        } else {
          ++result.missing_shape_count;
        }
      }
    }
  }

  result.ok = result.missing_shape_count == 0;
  return result;
}

}  // namespace ecc::geometry
