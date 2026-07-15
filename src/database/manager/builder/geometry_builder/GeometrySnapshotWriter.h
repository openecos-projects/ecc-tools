#pragma once

#include "GeometryDesignMetadata.h"
#include "GeometryLayerMetadata.h"
#include "GeometryStore.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ecc::geometry {

struct SnapshotWriteOptions
{
  std::filesystem::path output_dir;
  std::vector<GeometryLayerMetadata> layers;
  std::vector<GeometrySiteMetadata> sites;
  std::vector<GeometryMasterMetadata> masters;
  std::vector<GeometryViaMetadata> vias;
  std::vector<GeometryGridMetadata> grids;
  std::vector<GeometryConnectivityMetadata> connectivity;
  std::vector<GeometryBusMetadata> buses;
  std::vector<GeometryGroupMetadata> groups;
  std::string design_name;
  std::string design_version;
  int32_t dbu_per_micron = 0;
  int32_t manufacture_grid = -1;
};

struct SnapshotWriteResult
{
  bool ok = false;
  std::filesystem::path manifest_path;
  uint64_t shape_count = 0;
  uint64_t owner_count = 0;
  uint64_t payload_size = 0;
  uint64_t delta_count = 0;
  uint64_t layer_count = 0;
  uint64_t site_count = 0;
  uint64_t master_count = 0;
  uint64_t via_count = 0;
  uint64_t grid_count = 0;
  uint64_t connectivity_count = 0;
  uint64_t bus_count = 0;
  uint64_t group_count = 0;
  uint64_t epoch = 0;
};

class GeometrySnapshotWriter
{
 public:
  SnapshotWriteResult write(GeometryStore& store, const SnapshotWriteOptions& options) const;
};

}  // namespace ecc::geometry
