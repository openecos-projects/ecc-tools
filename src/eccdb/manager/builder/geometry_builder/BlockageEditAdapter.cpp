#include "BlockageEditAdapter.h"

#include "IdbBlockages.h"
#include "IdbDesign.h"

namespace ecc::geometry {

bool BlockageEditAdapter::update_rect(idb::IdbDesign& design, OwnerRef owner, Rect32 requested_bbox,
                                      Rect32& committed_bbox) const
{
  if (owner.type != OwnerType::kBlockage) {
    return false;
  }

  auto* blockage_list = design.get_blockage_list();
  if (blockage_list == nullptr) {
    return false;
  }

  const std::vector<idb::IdbBlockage*> blockages = blockage_list->get_blockage_list();
  if (owner.owner_id >= blockages.size()) {
    return false;
  }

  idb::IdbBlockage* blockage = blockages[static_cast<size_t>(owner.owner_id)];
  if (blockage == nullptr) {
    return false;
  }

  const std::vector<idb::IdbRect*> rects = blockage->get_rect_list();
  if (owner.path0 >= rects.size()) {
    return false;
  }

  idb::IdbRect* rect = rects[owner.path0];
  if (rect == nullptr) {
    return false;
  }

  committed_bbox = normalize(requested_bbox);
  rect->set_rect(committed_bbox.lx, committed_bbox.ly, committed_bbox.hx, committed_bbox.hy);
  return true;
}

}  // namespace ecc::geometry
