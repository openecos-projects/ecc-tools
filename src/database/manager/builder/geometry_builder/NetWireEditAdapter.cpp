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

int32_t midpoint(int32_t low, int32_t high)
{
  return low + (high - low) / 2;
}

bool set_regular_wire_points_from_bbox(idb::IdbRegularWireSegment& segment, Rect32 current_bbox, Rect32 requested_bbox)
{
  auto* start = segment.get_point_start();
  auto* end = segment.get_point_second();
  if (start == nullptr || end == nullptr) {
    return false;
  }

  current_bbox = normalize(current_bbox);
  requested_bbox = normalize(requested_bbox);
  if (start->get_y() == end->get_y()) {
    const int32_t half_width = (current_bbox.hy - current_bbox.ly) / 2;
    const int32_t x0 = requested_bbox.lx + half_width;
    const int32_t x1 = requested_bbox.hx - half_width;
    if (x1 < x0) {
      return false;
    }
    const int32_t y = midpoint(requested_bbox.ly, requested_bbox.hy);
    start->set_xy(x0, y);
    end->set_xy(x1, y);
    return true;
  }

  if (start->get_x() != end->get_x()) {
    return false;
  }

  const int32_t half_width = (current_bbox.hx - current_bbox.lx) / 2;
  const int32_t y0 = requested_bbox.ly + half_width;
  const int32_t y1 = requested_bbox.hy - half_width;
  if (y1 < y0) {
    return false;
  }
  const int32_t x = midpoint(requested_bbox.lx, requested_bbox.hx);
  start->set_xy(x, y0);
  end->set_xy(x, y1);
  return true;
}

bool set_special_wire_points_from_bbox(idb::IdbSpecialWireSegment& segment, Rect32 requested_bbox)
{
  auto* start = segment.get_point_start();
  auto* end = segment.get_point_second();
  if (start == nullptr || end == nullptr) {
    return false;
  }

  requested_bbox = normalize(requested_bbox);
  if (start->get_y() == end->get_y()) {
    if (requested_bbox.hx < requested_bbox.lx) {
      return false;
    }
    const int32_t y = midpoint(requested_bbox.ly, requested_bbox.hy);
    start->set_xy(requested_bbox.lx, y);
    end->set_xy(requested_bbox.hx, y);
    return true;
  }

  if (start->get_x() != end->get_x()) {
    return false;
  }

  if (requested_bbox.hy < requested_bbox.ly) {
    return false;
  }
  const int32_t x = midpoint(requested_bbox.lx, requested_bbox.hx);
  start->set_xy(x, requested_bbox.ly);
  end->set_xy(x, requested_bbox.hy);
  return true;
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
    /// the delta rect is an offset from the start point, so move the anchor and keep the offset
    if (segment->get_delta_rect() == nullptr || segment->get_point_start() == nullptr) {
      return false;
    }

    translate_points(segment->get_point_list(), dx, dy);
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

bool NetWireEditAdapter::resize_regular_segment(idb::IdbDesign& design, OwnerRef owner, Rect32 current_bbox,
                                                Rect32 requested_bbox, Rect32& committed_bbox) const
{
  auto* segment = find_regular_segment(design, owner);
  if (segment == nullptr) {
    return false;
  }

  requested_bbox = normalize(requested_bbox);
  if (segment->is_rect()) {
    auto* rect = segment->get_delta_rect();
    auto* point_start = segment->get_point_start();
    if (rect == nullptr || point_start == nullptr) {
      return false;
    }

    /// store the resize result as an offset from the start point
    rect->set_rect(requested_bbox.lx - point_start->get_x(), requested_bbox.ly - point_start->get_y(),
                   requested_bbox.hx - point_start->get_x(), requested_bbox.hy - point_start->get_y());
    committed_bbox = requested_bbox;
    if (auto* net = find_regular_net(design, owner.owner_id); net != nullptr) {
      net->set_bounding_box();
    }
    return true;
  }

  if (!segment->is_wire() || !set_regular_wire_points_from_bbox(*segment, current_bbox, requested_bbox)) {
    return false;
  }

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

bool NetWireEditAdapter::resize_special_segment(idb::IdbDesign& design, OwnerRef owner, Rect32 current_bbox,
                                                Rect32 requested_bbox, Rect32& committed_bbox) const
{
  auto* segment = find_special_segment(design, owner);
  if (segment == nullptr) {
    return false;
  }

  requested_bbox = normalize(requested_bbox);
  if (segment->is_rect()) {
    auto* rect = segment->get_delta_rect();
    if (rect == nullptr) {
      return false;
    }

    rect->set_rect(requested_bbox.lx, requested_bbox.ly, requested_bbox.hx, requested_bbox.hy);
    committed_bbox = requested_bbox;
    return true;
  }

  if (segment->get_point_num() < 2 || !set_special_wire_points_from_bbox(*segment, requested_bbox)) {
    return false;
  }

  if (!segment->set_bounding_box()) {
    return false;
  }

  committed_bbox = rect_from_idb(segment->get_bounding_box());
  return true;
}

}  // namespace ecc::geometry
