#include "GeometryEditApplier.h"

#include "BlockageEditAdapter.h"
#include "IdbDesign.h"
#include "IdbInstance.h"
#include "InstanceEditAdapter.h"
#include "NetWireEditAdapter.h"

namespace ecc::geometry {

namespace {

GeometryEditResult make_result(const GeometryEditCommand& command, GeometryEditStatus status)
{
  GeometryEditResult result;
  result.command_id = command.command_id;
  result.shape_id = command.shape_id;
  result.status = status;
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
    return make_result(command, GeometryEditStatus::kRejected);
  }

  if (record->version != command.expected_version) {
    GeometryEditResult result = make_result(command, GeometryEditStatus::kConflict);
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
      GeometryEditResult result = make_result(command, GeometryEditStatus::kRejected);
      result.new_version = record->version;
      result.committed_bbox = record->bbox;
      return result;
    }

    Rect32 committed_bbox;
    const BlockageEditAdapter adapter;
    if (!adapter.update_rect(design, owner, command.requested_bbox, committed_bbox)
        || !store.update_rect(command.shape_id, committed_bbox, command.command_id)) {
      GeometryEditResult result = make_result(command, GeometryEditStatus::kRejected);
      result.new_version = record->version;
      result.committed_bbox = record->bbox;
      return result;
    }

    const ShapeRecord* updated_record = store.find_shape(command.shape_id);
    GeometryEditResult result = make_result(command, status_for_committed_bbox(command, committed_bbox));
    result.new_version = updated_record == nullptr ? record->version : updated_record->version;
    result.committed_bbox = committed_bbox;
    return result;
  }

  if (command.op != GeometryEditOp::kMoveShape && command.op != GeometryEditOp::kResizeRect) {
    GeometryEditResult result = make_result(command, GeometryEditStatus::kRejected);
    result.new_version = record->version;
    result.committed_bbox = record->bbox;
    return result;
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
    GeometryEditResult result = make_result(command, GeometryEditStatus::kRejected);
    result.new_version = record->version;
    result.committed_bbox = record->bbox;
    return result;
  }

  if (!updated || !store.update_rect(command.shape_id, committed_bbox, command.command_id)) {
    GeometryEditResult result = make_result(command, GeometryEditStatus::kRejected);
    result.new_version = record->version;
    result.committed_bbox = record->bbox;
    return result;
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
    return make_result(command, GeometryEditStatus::kRejected);
  }

  if (record->version != command.expected_version) {
    GeometryEditResult result = make_result(command, GeometryEditStatus::kConflict);
    result.new_version = record->version;
    result.committed_bbox = record->bbox;
    return result;
  }

  const OwnerRef owner = store.owner_of(command.shape_id);
  if (owner.type != OwnerType::kInstanceBBox || owner.owner_id != instance.get_id() || command.op != GeometryEditOp::kMoveShape) {
    GeometryEditResult result = make_result(command, GeometryEditStatus::kRejected);
    result.new_version = record->version;
    result.committed_bbox = record->bbox;
    return result;
  }

  const InstanceEditAdapter adapter;
  const Rect32 committed_bbox = adapter.move_bbox(instance, command.requested_bbox);

  if (!store.update_rect(command.shape_id, committed_bbox, command.command_id)) {
    GeometryEditResult result = make_result(command, GeometryEditStatus::kRejected);
    result.new_version = record->version;
    result.committed_bbox = record->bbox;
    return result;
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
    return make_result(command, GeometryEditStatus::kRejected);
  }

  auto* instance_list = design.get_instance_list();
  if (instance_list == nullptr) {
    return make_result(command, GeometryEditStatus::kRejected);
  }

  for (auto* instance : instance_list->get_instance_list()) {
    if (instance != nullptr && instance->get_id() == owner.owner_id) {
      return apply_instance_bbox_edit(command, *instance, store);
    }
  }

  return make_result(command, GeometryEditStatus::kRejected);
}

}  // namespace ecc::geometry
