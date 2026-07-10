#include "GeometryBuilder.h"

#include "IdbDesign.h"
#include "IdbInstance.h"
#include "IdbLayout.h"
#include "IdbNet.h"
#include "IdbRegularWire.h"
#include "IdbSpecialNet.h"
#include "IdbSpecialWire.h"

#include <algorithm>
#include <unordered_set>

namespace ecc::geometry {

namespace {

constexpr LayerId kLayoutGeometryLayer = 0;

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

void reconcile_rect_shape(GeometryStore& store, OwnerType type, OwnerId owner_id, LayerId layer_id, Rect32 bbox, OwnerRef owner,
                          std::unordered_set<ShapeId>& seen_shape_ids, GeometrySyncResult& result)
{
  bbox = normalize(bbox);
  const ShapeId shape_id = find_shape_by_owner_path(store, type, owner_id, owner.path0, owner.path1);
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

uint64_t emit_pin_port_shapes(GeometryStore& store, idb::IdbPin* pin, uint32_t path0, uint32_t path1)
{
  if (pin == nullptr) {
    return 0;
  }

  const OwnerId pin_owner_id = pin->get_id() != 0 ? pin->get_id() : (static_cast<OwnerId>(path0) << 32U | path1);
  store.add_owner_name(OwnerType::kPinPortShape, pin_owner_id, pin->get_pin_name());

  uint64_t shape_count = 0;
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
        ++shape_count;
      }
    }

    ++layer_shape_index;
  }

  return shape_count;
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
          result.pin_shape_count += emit_pin_port_shapes(store, pin, instance_index, pin_index++);
        }
      }

      ++instance_index;
    }
  }

  if (auto* io_pins = design.get_io_pin_list(); io_pins != nullptr) {
    uint32_t pin_index = 0;
    for (auto* pin : io_pins->get_pin_list()) {
      result.pin_shape_count += emit_pin_port_shapes(store, pin, 0, pin_index++);
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

      uint32_t wire_index = 0;
      for (auto* wire : net->get_wire_list()->get_wire_list()) {
        if (wire == nullptr) {
          ++wire_index;
          continue;
        }

        uint32_t segment_index = 0;
        for (auto* segment : wire->get_segment_list()) {
          OwnerRef owner;
          owner.type = OwnerType::kNetWireSegment;
          owner.owner_id = net_owner_id;
          owner.path0 = wire_index;
          owner.path1 = segment_index++;

          if (emit_rect_if_present(store, layer_id_from_idb(segment == nullptr ? nullptr : segment->get_layer()),
                                   rect_from_regular_segment(segment), owner) != 0) {
            ++result.net_wire_shape_count;
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

      uint32_t wire_index = 0;
      for (auto* wire : net->get_wire_list()->get_wire_list()) {
        if (wire == nullptr) {
          ++wire_index;
          continue;
        }

        uint32_t segment_index = 0;
        for (auto* segment : wire->get_segment_list()) {
          OwnerRef owner;
          owner.type = OwnerType::kSpecialWireSegment;
          owner.owner_id = net_owner_id;
          owner.path0 = wire_index;
          owner.path1 = segment_index++;

          if (emit_rect_if_present(store, layer_id_from_idb(segment == nullptr ? nullptr : segment->get_layer()),
                                   rect_from_special_segment(segment), owner) != 0) {
            ++result.special_net_wire_shape_count;
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
      if (fill == nullptr || fill->get_type() != idb::IdbFill::IdbFillType::kLayer || fill->get_layer() == nullptr) {
        ++fill_index;
        continue;
      }

      uint32_t rect_index = 0;
      for (auto* rect : fill->get_layer()->get_rect_list()) {
        OwnerRef owner;
        owner.type = OwnerType::kFill;
        owner.owner_id = fill_index;
        owner.path0 = rect_index++;

        if (emit_rect_if_present(store, layer_id_from_idb(fill->get_layer()->get_layer()), rect_from_idb(rect), owner) != 0) {
          ++result.fill_shape_count;
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

  result.shape_count = result.die_shape_count + result.core_shape_count + result.row_shape_count + result.instance_shape_count
                       + result.instance_halo_shape_count + result.net_wire_shape_count + result.special_net_wire_shape_count
                       + result.blockage_shape_count + result.fill_shape_count + result.region_shape_count + result.slot_shape_count
                       + result.pin_shape_count;
  store.clear_delta_events();
  return result;
}

GeometrySyncResult GeometryBuilder::sync_net(idb::IdbDesign& design, idb::IdbNet& net, GeometryStore& store) const
{
  GeometrySyncResult result;
  OwnerId owner_id = 0;
  if (!resolve_regular_net_owner_id(design, net, owner_id)) {
    result.missing_shape_count = 1;
    return result;
  }

  std::unordered_set<ShapeId> seen_shape_ids;
  if (auto* wire_list = net.get_wire_list(); wire_list != nullptr) {
    uint32_t wire_index = 0;
    for (auto* wire : wire_list->get_wire_list()) {
      if (wire == nullptr) {
        ++wire_index;
        continue;
      }

      uint32_t segment_index = 0;
      for (auto* segment : wire->get_segment_list()) {
        OwnerRef owner;
        owner.type = OwnerType::kNetWireSegment;
        owner.owner_id = owner_id;
        owner.path0 = wire_index;
        owner.path1 = segment_index++;

        const Rect32 bbox = rect_from_regular_segment(segment);
        if (is_non_empty(bbox)) {
          reconcile_rect_shape(store, owner.type, owner.owner_id, layer_id_from_idb(segment == nullptr ? nullptr : segment->get_layer()),
                               bbox, owner, seen_shape_ids, result);
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

  std::unordered_set<ShapeId> seen_shape_ids;
  if (auto* wire_list = net.get_wire_list(); wire_list != nullptr) {
    uint32_t wire_index = 0;
    for (auto* wire : wire_list->get_wire_list()) {
      if (wire == nullptr) {
        ++wire_index;
        continue;
      }

      uint32_t segment_index = 0;
      for (auto* segment : wire->get_segment_list()) {
        OwnerRef owner;
        owner.type = OwnerType::kSpecialWireSegment;
        owner.owner_id = owner_id;
        owner.path0 = wire_index;
        owner.path1 = segment_index++;

        if (segment != nullptr && !segment->is_rect()) {
          segment->set_bounding_box();
        }

        const Rect32 bbox = rect_from_special_segment(segment);
        if (is_non_empty(bbox)) {
          reconcile_rect_shape(store, owner.type, owner.owner_id, layer_id_from_idb(segment == nullptr ? nullptr : segment->get_layer()),
                               bbox, owner, seen_shape_ids, result);
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

  result.ok = result.missing_shape_count == 0;
  return result;
}

GeometrySyncResult GeometryBuilder::sync_instance(idb::IdbInstance& instance, GeometryStore& store) const
{
  GeometrySyncResult result;

  const auto sync_owner_rect = [&](OwnerType owner_type, Rect32 bbox) {
    const std::vector<ShapeId> shape_ids = store.query_owner(owner_type, instance.get_id());
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

  result.ok = result.missing_shape_count == 0;
  return result;
}

}  // namespace ecc::geometry
