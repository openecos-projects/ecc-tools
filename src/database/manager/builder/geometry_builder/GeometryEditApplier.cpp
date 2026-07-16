#include "GeometryEditApplier.h"

#include "BlockageEditAdapter.h"
#include "IdbDesign.h"
#include "IdbInstance.h"
#include "InstanceEditAdapter.h"
#include "NetWireEditAdapter.h"
#include "PinPortEditAdapter.h"
#include "RectOwnerEditAdapter.h"

#include <vector>

namespace ecc::geometry {

namespace {

constexpr OwnerId kDerivedOwnerPayloadMask = 0x00ffffffffffffffULL;

OwnerId make_derived_owner_id(OwnerType parent_type, OwnerId parent_owner_id)
{
  return (static_cast<OwnerId>(static_cast<uint8_t>(parent_type)) << 56U)
         | (parent_owner_id & kDerivedOwnerPayloadMask);
}

OwnerId pin_owner_id_from_path(idb::IdbPin* pin, uint32_t path0, uint32_t path1)
{
  if (pin == nullptr) {
    return 0;
  }

  return pin->get_id() != 0 ? pin->get_id() : (static_cast<OwnerId>(path0) << 32U | path1);
}

Rect32 offset_rect(Rect32 rect, int32_t dx, int32_t dy)
{
  return Rect32{rect.lx + dx, rect.ly + dy, rect.hx + dx, rect.hy + dy};
}

bool translate_owner_shapes(GeometryStore& store, OwnerType owner_type, OwnerId owner_id, int32_t dx, int32_t dy,
                            uint64_t command_id)
{
  for (const ShapeId shape_id : store.query_owner(owner_type, owner_id)) {
    const ShapeRecord* record = store.find_shape(shape_id);
    if (record == nullptr || record->state != ShapeState::kAlive || record->kind != ShapeKind::kRect) {
      continue;
    }
    if (!store.update_rect(shape_id, offset_rect(record->bbox, dx, dy), command_id)) {
      return false;
    }
  }
  return true;
}

bool translate_instance_pin_shapes(GeometryStore& store, idb::IdbInstance& instance, OwnerType pin_owner_type,
                                   uint32_t instance_path0, int32_t dx, int32_t dy, uint64_t command_id)
{
  auto* pins = instance.get_pin_list();
  if (pins == nullptr) {
    return true;
  }

  uint32_t pin_index = 0;
  for (auto* pin : pins->get_pin_list()) {
    const OwnerId pin_owner_id = pin_owner_id_from_path(pin, instance_path0, pin_index++);
    if (!translate_owner_shapes(store, pin_owner_type, pin_owner_id, dx, dy, command_id)) {
      return false;
    }

    const OwnerId via_owner_id = make_derived_owner_id(pin_owner_type, pin_owner_id);
    if (!translate_owner_shapes(store, OwnerType::kVia, via_owner_id, dx, dy, command_id)) {
      return false;
    }
  }

  return true;
}

bool translate_instance_dependent_shapes(GeometryStore& store, idb::IdbInstance& instance, OwnerRef instance_owner,
                                         int32_t dx, int32_t dy, uint64_t command_id)
{
  if (!translate_owner_shapes(store, OwnerType::kInstanceHalo, instance_owner.owner_id, dx, dy, command_id)) {
    return false;
  }
  if (!translate_owner_shapes(store, OwnerType::kObs, instance_owner.owner_id, dx, dy, command_id)) {
    return false;
  }
  if (!translate_instance_pin_shapes(store, instance, OwnerType::kInstancePinPortShape, instance_owner.path0, dx, dy,
                                     command_id)) {
    return false;
  }
  return translate_instance_pin_shapes(store, instance, OwnerType::kPinPortShape, instance_owner.path0, dx, dy,
                                       command_id);
}

bool is_editable_io_pin_owner(OwnerRef owner)
{
  return owner.type == OwnerType::kIoPinPortShape || owner.type == OwnerType::kPinPortShape;
}

GeometryEditResult make_result(const GeometryEditCommand& command, GeometryEditStatus status,
                               GeometryEditDiagnostic diagnostic = GeometryEditDiagnostic::kNone)
{
  GeometryEditResult result;
  result.command_id = command.command_id;
  result.shape_id = command.shape_id;
  result.status = status;
  result.flags = set_geometry_edit_diagnostic(result.flags, diagnostic);
  return result;
}

GeometryEditResult make_record_result(const GeometryEditCommand& command, GeometryEditStatus status,
                                      const ShapeRecord& record, GeometryEditDiagnostic diagnostic)
{
  GeometryEditResult result = make_result(command, status, diagnostic);
  result.new_version = record.version;
  result.committed_bbox = record.bbox;
  return result;
}

bool is_same_rect(Rect32 lhs, Rect32 rhs)
{
  lhs = normalize(lhs);
  rhs = normalize(rhs);
  return lhs.lx == rhs.lx && lhs.ly == rhs.ly && lhs.hx == rhs.hx && lhs.hy == rhs.hy;
}

GeometryEditStatus status_for_committed_bbox(const GeometryEditCommand& command, Rect32 committed_bbox)
{
  return is_same_rect(command.requested_bbox, committed_bbox) ? GeometryEditStatus::kAccepted
                                                             : GeometryEditStatus::kAdjustedAccepted;
}

}  // namespace

GeometryEditResult GeometryEditApplier::apply_edit(const GeometryEditCommand& command, idb::IdbDesign& design,
                                                   GeometryStore& store) const
{
  const ShapeRecord* record = store.find_shape(command.shape_id);
  if (record == nullptr || record->state != ShapeState::kAlive || record->kind != ShapeKind::kRect) {
    return make_result(command, GeometryEditStatus::kRejected, GeometryEditDiagnostic::kShapeUnavailable);
  }

  if (record->version != command.expected_version) {
    GeometryEditResult result =
        make_result(command, GeometryEditStatus::kConflict, GeometryEditDiagnostic::kVersionConflict);
    result.new_version = record->version;
    result.committed_bbox = record->bbox;
    return result;
  }

  const OwnerRef owner = store.owner_of(command.shape_id);
  if (owner.type == OwnerType::kInstanceBBox) {
    return apply_instance_bbox_edit(command, design, store);
  }

  if (owner.type == OwnerType::kBlockage) {
    if (command.op != GeometryEditOp::kMoveShape && command.op != GeometryEditOp::kResizeRect) {
      return make_record_result(command, GeometryEditStatus::kRejected, *record,
                                GeometryEditDiagnostic::kUnsupportedOperation);
    }

    Rect32 committed_bbox;
    const BlockageEditAdapter adapter;
    if (!adapter.update_rect(design, owner, command.requested_bbox, committed_bbox)) {
      return make_record_result(command, GeometryEditStatus::kRejected, *record,
                                GeometryEditDiagnostic::kBackendUpdateFailed);
    }
    if (!store.update_rect(command.shape_id, committed_bbox, command.command_id)) {
      return make_record_result(command, GeometryEditStatus::kRejected, *record,
                                GeometryEditDiagnostic::kStoreUpdateFailed);
    }

    const ShapeRecord* updated_record = store.find_shape(command.shape_id);
    GeometryEditResult result = make_result(command, status_for_committed_bbox(command, committed_bbox));
    result.new_version = updated_record == nullptr ? record->version : updated_record->version;
    result.committed_bbox = committed_bbox;
    return result;
  }

  if (owner.type == OwnerType::kFill || owner.type == OwnerType::kRegion || owner.type == OwnerType::kSlot) {
    if (command.op != GeometryEditOp::kMoveShape && command.op != GeometryEditOp::kResizeRect) {
      return make_record_result(command, GeometryEditStatus::kRejected, *record,
                                GeometryEditDiagnostic::kUnsupportedOperation);
    }

    Rect32 committed_bbox;
    const RectOwnerEditAdapter adapter;
    if (!adapter.update_rect(design, owner, command.requested_bbox, committed_bbox)) {
      return make_record_result(command, GeometryEditStatus::kRejected, *record,
                                GeometryEditDiagnostic::kBackendUpdateFailed);
    }
    if (!store.update_rect(command.shape_id, committed_bbox, command.command_id)) {
      return make_record_result(command, GeometryEditStatus::kRejected, *record,
                                GeometryEditDiagnostic::kStoreUpdateFailed);
    }

    const ShapeRecord* updated_record = store.find_shape(command.shape_id);
    GeometryEditResult result = make_result(command, status_for_committed_bbox(command, committed_bbox));
    result.new_version = updated_record == nullptr ? record->version : updated_record->version;
    result.committed_bbox = committed_bbox;
    return result;
  }

  if (is_editable_io_pin_owner(owner)) {
    if (command.op != GeometryEditOp::kMoveShape && command.op != GeometryEditOp::kResizeRect) {
      return make_record_result(command, GeometryEditStatus::kRejected, *record,
                                GeometryEditDiagnostic::kUnsupportedOperation);
    }

    Rect32 committed_bbox;
    GeometryEditDiagnostic diagnostic = GeometryEditDiagnostic::kBackendUpdateFailed;
    const PinPortEditAdapter adapter;
    if (!adapter.update_rect(design, owner, command.requested_bbox, committed_bbox, &diagnostic)) {
      return make_record_result(command, GeometryEditStatus::kRejected, *record, diagnostic);
    }
    if (!store.update_rect(command.shape_id, committed_bbox, command.command_id)) {
      return make_record_result(command, GeometryEditStatus::kRejected, *record,
                                GeometryEditDiagnostic::kStoreUpdateFailed);
    }

    const ShapeRecord* updated_record = store.find_shape(command.shape_id);
    GeometryEditResult result = make_result(command, status_for_committed_bbox(command, committed_bbox));
    result.new_version = updated_record == nullptr ? record->version : updated_record->version;
    result.committed_bbox = committed_bbox;
    return result;
  }

  if (command.op != GeometryEditOp::kMoveShape && command.op != GeometryEditOp::kResizeRect) {
    return make_record_result(command, GeometryEditStatus::kRejected, *record,
                              GeometryEditDiagnostic::kUnsupportedOperation);
  }

  Rect32 committed_bbox;
  const NetWireEditAdapter adapter;
  bool updated = false;
  if (owner.type == OwnerType::kNetWireSegment) {
    updated = command.op == GeometryEditOp::kMoveShape
                  ? adapter.move_regular_segment(design, owner, record->bbox, command.requested_bbox, committed_bbox)
                  : adapter.resize_regular_segment(design, owner, record->bbox, command.requested_bbox, committed_bbox);
  } else if (owner.type == OwnerType::kSpecialWireSegment) {
    updated = command.op == GeometryEditOp::kMoveShape
                  ? adapter.move_special_segment(design, owner, record->bbox, command.requested_bbox, committed_bbox)
                  : adapter.resize_special_segment(design, owner, record->bbox, command.requested_bbox, committed_bbox);
  } else {
    return make_record_result(command, GeometryEditStatus::kRejected, *record,
                              GeometryEditDiagnostic::kUnsupportedOwner);
  }

  if (!updated) {
    return make_record_result(command, GeometryEditStatus::kRejected, *record,
                              GeometryEditDiagnostic::kBackendUpdateFailed);
  }
  if (!store.update_rect(command.shape_id, committed_bbox, command.command_id)) {
    return make_record_result(command, GeometryEditStatus::kRejected, *record,
                              GeometryEditDiagnostic::kStoreUpdateFailed);
  }

  const ShapeRecord* updated_record = store.find_shape(command.shape_id);
  GeometryEditResult result = make_result(command, status_for_committed_bbox(command, committed_bbox));
  result.new_version = updated_record == nullptr ? record->version : updated_record->version;
  result.committed_bbox = committed_bbox;
  return result;
}

GeometryEditResult GeometryEditApplier::apply_instance_bbox_edit(const GeometryEditCommand& command,
                                                                 idb::IdbInstance& instance,
                                                                 GeometryStore& store) const
{
  const ShapeRecord* record = store.find_shape(command.shape_id);
  if (record == nullptr || record->state != ShapeState::kAlive || record->kind != ShapeKind::kRect) {
    return make_result(command, GeometryEditStatus::kRejected, GeometryEditDiagnostic::kShapeUnavailable);
  }

  if (record->version != command.expected_version) {
    GeometryEditResult result =
        make_result(command, GeometryEditStatus::kConflict, GeometryEditDiagnostic::kVersionConflict);
    result.new_version = record->version;
    result.committed_bbox = record->bbox;
    return result;
  }

  const OwnerRef owner = store.owner_of(command.shape_id);
  if (owner.type != OwnerType::kInstanceBBox || owner.owner_id != instance.get_id()
      || command.op != GeometryEditOp::kMoveShape) {
    return make_record_result(command, GeometryEditStatus::kRejected, *record,
                              GeometryEditDiagnostic::kInstanceOwnerMismatch);
  }

  const Rect32 old_bbox = record->bbox;
  const InstanceEditAdapter adapter;
  const Rect32 committed_bbox = adapter.move_bbox(instance, command.requested_bbox);

  if (!store.update_rect(command.shape_id, committed_bbox, command.command_id)) {
    return make_record_result(command, GeometryEditStatus::kRejected, *record,
                              GeometryEditDiagnostic::kStoreUpdateFailed);
  }

  const int32_t dx = committed_bbox.lx - old_bbox.lx;
  const int32_t dy = committed_bbox.ly - old_bbox.ly;
  if ((dx != 0 || dy != 0)
      && !translate_instance_dependent_shapes(store, instance, owner, dx, dy, command.command_id)) {
    return make_record_result(command, GeometryEditStatus::kRejected, *record,
                              GeometryEditDiagnostic::kStoreUpdateFailed);
  }

  const ShapeRecord* updated_record = store.find_shape(command.shape_id);
  GeometryEditResult result = make_result(command, status_for_committed_bbox(command, committed_bbox));
  result.new_version = updated_record == nullptr ? record->version : updated_record->version;
  result.committed_bbox = committed_bbox;
  return result;
}

GeometryEditResult GeometryEditApplier::apply_instance_bbox_edit(const GeometryEditCommand& command,
                                                                 idb::IdbDesign& design,
                                                                 GeometryStore& store) const
{
  const OwnerRef owner = store.owner_of(command.shape_id);
  if (owner.type != OwnerType::kInstanceBBox) {
    return make_result(command, GeometryEditStatus::kRejected, GeometryEditDiagnostic::kInstanceOwnerMismatch);
  }

  auto* instance_list = design.get_instance_list();
  if (instance_list == nullptr) {
    return make_result(command, GeometryEditStatus::kRejected, GeometryEditDiagnostic::kInstanceListUnavailable);
  }

  for (auto* instance : instance_list->get_instance_list()) {
    if (instance != nullptr && instance->get_id() == owner.owner_id) {
      return apply_instance_bbox_edit(command, *instance, store);
    }
  }

  return make_result(command, GeometryEditStatus::kRejected, GeometryEditDiagnostic::kInstanceNotFound);
}

}  // namespace ecc::geometry
