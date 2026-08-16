#pragma once

#include "GeometryEdit.h"
#include "GeometryStore.h"

namespace idb {
class IdbDesign;
class IdbInstance;
}

namespace ecc::geometry {

class GeometryEditApplier
{
 public:
  GeometryEditResult apply_edit(const GeometryEditCommand& command, idb::IdbDesign& design, GeometryStore& store) const;
  GeometryEditResult apply_instance_bbox_edit(const GeometryEditCommand& command, idb::IdbInstance& instance, GeometryStore& store) const;
  GeometryEditResult apply_instance_bbox_edit(const GeometryEditCommand& command, idb::IdbDesign& design, GeometryStore& store) const;
};

}  // namespace ecc::geometry
