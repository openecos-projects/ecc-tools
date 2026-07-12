#pragma once

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
  uint64_t epoch = 0;
};

class GeometrySnapshotWriter
{
 public:
  SnapshotWriteResult write(GeometryStore& store, const SnapshotWriteOptions& options) const;
};

}  // namespace ecc::geometry
