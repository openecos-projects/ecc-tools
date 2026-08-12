#pragma once

#include "GeometryEdit.h"
#include "OwnerRef.h"

namespace idb {
class IdbDesign;
}  // namespace idb

namespace ecc::geometry {

class PinPortEditAdapter
{
 public:
  bool update_rect(idb::IdbDesign& design, OwnerRef owner, Rect32 requested_bbox, Rect32& committed_bbox,
                   GeometryEditDiagnostic* diagnostic = nullptr) const;
};

}  // namespace ecc::geometry
