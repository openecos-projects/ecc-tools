#include "RectOwnerEditAdapter.h"

#include "IdbDesign.h"
#include "IdbFill.h"
#include "IdbGeometry.h"
#include "IdbRegion.h"
#include "IdbSlot.h"

#include <vector>

namespace ecc::geometry {

namespace {

idb::IdbRect* fill_rect(idb::IdbDesign& design, OwnerRef owner)
{
  auto* fill_list = design.get_fill_list();
  if (fill_list == nullptr) {
    return nullptr;
  }

  const std::vector<idb::IdbFill*> fills = fill_list->get_fill_list();
  if (owner.owner_id >= fills.size()) {
    return nullptr;
  }

  idb::IdbFill* fill = fills[static_cast<size_t>(owner.owner_id)];
  if (fill == nullptr || fill->get_type() != idb::IdbFill::IdbFillType::kLayer || fill->get_layer() == nullptr) {
    return nullptr;
  }

  std::vector<idb::IdbRect*>& rects = fill->get_layer()->get_rect_list();
  if (owner.path0 >= rects.size()) {
    return nullptr;
  }

  return rects[owner.path0];
}

idb::IdbRect* region_boundary_rect(idb::IdbDesign& design, OwnerRef owner)
{
  auto* region_list = design.get_region_list();
  if (region_list == nullptr) {
    return nullptr;
  }

  const std::vector<idb::IdbRegion*> regions = region_list->get_region_list();
  if (owner.owner_id >= regions.size()) {
    return nullptr;
  }

  idb::IdbRegion* region = regions[static_cast<size_t>(owner.owner_id)];
  if (region == nullptr) {
    return nullptr;
  }

  std::vector<idb::IdbRect*>& boundaries = region->get_boundary();
  if (owner.path0 >= boundaries.size()) {
    return nullptr;
  }

  return boundaries[owner.path0];
}

idb::IdbRect* slot_rect(idb::IdbDesign& design, OwnerRef owner)
{
  auto* slot_list = design.get_slot_list();
  if (slot_list == nullptr) {
    return nullptr;
  }

  const std::vector<idb::IdbSlot*> slots = slot_list->get_slot_list();
  if (owner.owner_id >= slots.size()) {
    return nullptr;
  }

  idb::IdbSlot* slot = slots[static_cast<size_t>(owner.owner_id)];
  if (slot == nullptr) {
    return nullptr;
  }

  std::vector<idb::IdbRect*>& rects = slot->get_rect_list();
  if (owner.path0 >= rects.size()) {
    return nullptr;
  }

  return rects[owner.path0];
}

idb::IdbRect* rect_for_owner(idb::IdbDesign& design, OwnerRef owner)
{
  switch (owner.type) {
    case OwnerType::kFill:
      return fill_rect(design, owner);
    case OwnerType::kRegion:
      return region_boundary_rect(design, owner);
    case OwnerType::kSlot:
      return slot_rect(design, owner);
    default:
      return nullptr;
  }
}

}  // namespace

bool RectOwnerEditAdapter::update_rect(idb::IdbDesign& design, OwnerRef owner, Rect32 requested_bbox,
                                       Rect32& committed_bbox) const
{
  idb::IdbRect* rect = rect_for_owner(design, owner);
  if (rect == nullptr) {
    return false;
  }

  committed_bbox = normalize(requested_bbox);
  rect->set_rect(committed_bbox.lx, committed_bbox.ly, committed_bbox.hx, committed_bbox.hy);
  return true;
}

}  // namespace ecc::geometry
