#pragma once

#include "GeometryTypes.h"
#include "OwnerRef.h"

namespace idb {
class IdbDesign;
}

namespace ecc::geometry {

class NetWireEditAdapter
{
 public:
  bool move_regular_segment(idb::IdbDesign& design, OwnerRef owner, Rect32 current_bbox, Rect32 requested_bbox,
                            Rect32& committed_bbox) const;
  bool move_special_segment(idb::IdbDesign& design, OwnerRef owner, Rect32 current_bbox, Rect32 requested_bbox,
                            Rect32& committed_bbox) const;
};

}  // namespace ecc::geometry
