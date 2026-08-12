#include "InstanceEditAdapter.h"

#include "IdbInstance.h"

namespace ecc::geometry {

Rect32 InstanceEditAdapter::move_bbox(idb::IdbInstance& instance, Rect32 requested_bbox) const
{
  requested_bbox = normalize(requested_bbox);
  instance.set_coodinate(requested_bbox.lx, requested_bbox.ly);

  auto* bbox = instance.get_bounding_box();
  if (bbox == nullptr) {
    return requested_bbox;
  }

  return Rect32{bbox->get_low_x(), bbox->get_low_y(), bbox->get_high_x(), bbox->get_high_y()};
}

}  // namespace ecc::geometry
