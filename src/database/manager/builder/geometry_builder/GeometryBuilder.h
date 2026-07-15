#pragma once

#include "GeometryDesignMetadata.h"
#include "GeometryLayerMetadata.h"
#include "GeometryStore.h"

#include <vector>

namespace idb {
class IdbDesign;
class IdbBlockage;
class IdbFill;
class IdbInstance;
class IdbLayout;
class IdbNet;
class IdbPin;
class IdbRegion;
class IdbSlot;
class IdbSpecialNet;
}

namespace ecc::geometry {

struct GeometryBuildResult
{
  uint64_t shape_count = 0;
  uint64_t die_shape_count = 0;
  uint64_t core_shape_count = 0;
  uint64_t row_shape_count = 0;
  uint64_t track_grid_shape_count = 0;
  uint64_t gcell_grid_shape_count = 0;
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
  std::vector<GeometryLayerMetadata> collect_layer_metadata(idb::IdbLayout& layout) const;
  std::vector<GeometrySiteMetadata> collect_site_metadata(idb::IdbLayout& layout) const;
  std::vector<GeometryMasterMetadata> collect_master_metadata(idb::IdbLayout& layout) const;
  std::vector<GeometryViaMetadata> collect_via_metadata(idb::IdbLayout& layout, idb::IdbDesign& design) const;
  std::vector<GeometryGridMetadata> collect_grid_metadata(idb::IdbLayout& layout) const;
  std::vector<GeometryConnectivityMetadata> collect_connectivity_metadata(idb::IdbDesign& design) const;
  std::vector<GeometryBusMetadata> collect_bus_metadata(idb::IdbDesign& design) const;
  std::vector<GeometryGroupMetadata> collect_group_metadata(idb::IdbDesign& design) const;
  GeometrySyncResult sync_layout(idb::IdbLayout& layout, GeometryStore& store) const;
  GeometrySyncResult sync_instance(idb::IdbInstance& instance, GeometryStore& store) const;
  GeometrySyncResult sync_net(idb::IdbDesign& design, idb::IdbNet& net, GeometryStore& store) const;
  GeometrySyncResult sync_special_net(idb::IdbDesign& design, idb::IdbSpecialNet& net, GeometryStore& store) const;
  GeometrySyncResult sync_blockage(idb::IdbDesign& design, idb::IdbBlockage& blockage, GeometryStore& store) const;
  GeometrySyncResult sync_region(idb::IdbDesign& design, idb::IdbRegion& region, GeometryStore& store) const;
  GeometrySyncResult sync_slot(idb::IdbDesign& design, idb::IdbSlot& slot, GeometryStore& store) const;
  GeometrySyncResult sync_fill(idb::IdbDesign& design, idb::IdbFill& fill, GeometryStore& store) const;
  GeometrySyncResult sync_io_pin(idb::IdbDesign& design, idb::IdbPin& pin, GeometryStore& store) const;
};

}  // namespace ecc::geometry
