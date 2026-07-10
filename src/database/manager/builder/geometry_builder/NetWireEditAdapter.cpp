#include "NetWireEditAdapter.h"

#include "IdbDesign.h"
#include "IdbNet.h"
#include "IdbRegularWire.h"
#include "IdbSpecialNet.h"
#include "IdbSpecialWire.h"

#include <cstdint>
#include <vector>

namespace ecc::geometry {
namespace {

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

Rect32 translate_rect(Rect32 rect, int32_t dx, int32_t dy)
{
  rect = normalize(rect);
  return Rect32{rect.lx + dx, rect.ly + dy, rect.hx + dx, rect.hy + dy};
}

void translate_points(std::vector<idb::IdbCoordinate<int32_t>*>& points, int32_t dx, int32_t dy)
{
  for (auto* point : points) {
    if (point == nullptr) {
      continue;
    }

    point->set_xy(point->get_x() + dx, point->get_y() + dy);
  }
}

idb::IdbNet* find_regular_net(idb::IdbDesign& design, OwnerId owner_id)
{
  auto* net_list = design.get_net_list();
  if (net_list == nullptr) {
    return nullptr;
  }

  uint32_t net_index = 0;
  for (auto* net : net_list->get_net_list()) {
    if (net != nullptr && (net->get_id() != 0 ? net->get_id() == owner_id : net_index == owner_id)) {
      return net;
    }
    ++net_index;
  }

  return nullptr;
}

idb::IdbRegularWireSegment* find_regular_segment(idb::IdbDesign& design, OwnerRef owner)
{
  idb::IdbNet* net = find_regular_net(design, owner.owner_id);
  if (net == nullptr || net->get_wire_list() == nullptr) {
    return nullptr;
  }

  auto* wire = net->get_wire_list()->find_wire(owner.path0);
  if (wire == nullptr || wire->get_segment_list().size() <= owner.path1) {
    return nullptr;
  }

  return wire->get_segment_list()[owner.path1];
}

idb::IdbSpecialNet* find_special_net(idb::IdbDesign& design, OwnerId owner_id)
{
  auto* net_list = design.get_special_net_list();
  if (net_list == nullptr || net_list->get_net_list().size() <= owner_id) {
    return nullptr;
  }

  return net_list->get_net_list()[owner_id];
}

idb::IdbSpecialWireSegment* find_special_segment(idb::IdbDesign& design, OwnerRef owner)
{
  idb::IdbSpecialNet* net = find_special_net(design, owner.owner_id);
  if (net == nullptr || net->get_wire_list() == nullptr) {
    return nullptr;
  }

  auto* wire = net->get_wire_list()->find_wire(owner.path0);
  if (wire == nullptr || wire->get_segment_list().size() <= owner.path1) {
    return nullptr;
  }

  return wire->get_segment_list()[owner.path1];
}

}  // namespace

bool NetWireEditAdapter::move_regular_segment(idb::IdbDesign& design, OwnerRef owner, Rect32 current_bbox,
                                              Rect32 requested_bbox, Rect32& committed_bbox) const
{
  auto* segment = find_regular_segment(design, owner);
  if (segment == nullptr) {
    return false;
  }

  current_bbox = normalize(current_bbox);
  requested_bbox = normalize(requested_bbox);
  const int32_t dx = requested_bbox.lx - current_bbox.lx;
  const int32_t dy = requested_bbox.ly - current_bbox.ly;
  committed_bbox = translate_rect(current_bbox, dx, dy);

  if (segment->is_rect()) {
    auto* rect = segment->get_delta_rect();
    if (rect == nullptr) {
      return false;
    }

    rect->set_rect(committed_bbox.lx, committed_bbox.ly, committed_bbox.hx, committed_bbox.hy);
    return true;
  }

  if (!segment->is_wire()) {
    return false;
  }

  translate_points(segment->get_point_list(), dx, dy);
  idb::IdbRect segment_rect = segment->get_segment_rect();
  committed_bbox = rect_from_idb(segment_rect);

  if (auto* net = find_regular_net(design, owner.owner_id); net != nullptr) {
    net->set_bounding_box();
  }

  return true;
}

bool NetWireEditAdapter::move_special_segment(idb::IdbDesign& design, OwnerRef owner, Rect32 current_bbox,
                                              Rect32 requested_bbox, Rect32& committed_bbox) const
{
  auto* segment = find_special_segment(design, owner);
  if (segment == nullptr) {
    return false;
  }

  current_bbox = normalize(current_bbox);
  requested_bbox = normalize(requested_bbox);
  const int32_t dx = requested_bbox.lx - current_bbox.lx;
  const int32_t dy = requested_bbox.ly - current_bbox.ly;
  committed_bbox = translate_rect(current_bbox, dx, dy);

  if (segment->is_rect()) {
    auto* rect = segment->get_delta_rect();
    if (rect == nullptr) {
      return false;
    }

    rect->set_rect(committed_bbox.lx, committed_bbox.ly, committed_bbox.hx, committed_bbox.hy);
    return true;
  }

  if (segment->get_point_num() < 2) {
    return false;
  }

  translate_points(segment->get_point_list(), dx, dy);
  if (!segment->set_bounding_box()) {
    return false;
  }

  committed_bbox = rect_from_idb(segment->get_bounding_box());
  return true;
}

}  // namespace ecc::geometry
