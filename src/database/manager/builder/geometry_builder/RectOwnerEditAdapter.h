#pragma once

#include "GeometryTypes.h"
#include "OwnerRef.h"

namespace idb {
class IdbDesign;
}

namespace ecc::geometry {

class RectOwnerEditAdapter
{
 public:
  bool update_rect(idb::IdbDesign& design, OwnerRef owner, Rect32 requested_bbox, Rect32& committed_bbox) const;
};

}  // namespace ecc::geometry
