#pragma once

#include "GeometryTypes.h"

namespace idb {
class IdbInstance;
}

namespace ecc::geometry {

class InstanceEditAdapter
{
 public:
  Rect32 move_bbox(idb::IdbInstance& instance, Rect32 requested_bbox) const;
};

}  // namespace ecc::geometry
