#pragma once

#include "GeometryStore.h"

namespace idb {
class IdbDesign;
class IdbInstance;
class IdbLayout;
class IdbNet;
class IdbSpecialNet;
}

namespace ecc::geometry {

struct GeometryBuildResult
{
  uint64_t shape_count = 0;
  uint64_t die_shape_count = 0;
  uint64_t core_shape_count = 0;
  uint64_t row_shape_count = 0;
  uint64_t instance_shape_count = 0;
  uint64_t instance_halo_shape_count = 0;
  uint64_t net_wire_shape_count = 0;
  uint64_t special_net_wire_shape_count = 0;
  uint64_t via_shape_count = 0;
  uint64_t blockage_shape_count = 0;
  uint64_t fill_shape_count = 0;
  uint64_t region_shape_count = 0;
  uint64_t slot_shape_count = 0;
  uint64_t pin_shape_count = 0;
  uint64_t obs_shape_count = 0;
};

struct GeometrySyncResult
{
  bool ok = false;
  uint64_t added_shape_count = 0;
  uint64_t updated_shape_count = 0;
  uint64_t deleted_shape_count = 0;
  uint64_t missing_shape_count = 0;
};

class GeometryBuilder
{
 public:
  GeometryBuildResult rebuild_from_design(idb::IdbDesign& design, idb::IdbLayout& layout, GeometryStore& store) const;
  GeometrySyncResult sync_instance(idb::IdbInstance& instance, GeometryStore& store) const;
  GeometrySyncResult sync_net(idb::IdbDesign& design, idb::IdbNet& net, GeometryStore& store) const;
  GeometrySyncResult sync_special_net(idb::IdbDesign& design, idb::IdbSpecialNet& net, GeometryStore& store) const;
};

}  // namespace ecc::geometry
