#pragma once

#include "GeometryStore.h"

#include <cstdint>
#include <filesystem>

namespace ecc::geometry {

struct SnapshotWriteOptions
{
  std::filesystem::path output_dir;
};

struct SnapshotWriteResult
{
  bool ok = false;
  std::filesystem::path manifest_path;
  uint64_t shape_count = 0;
  uint64_t owner_count = 0;
  uint64_t payload_size = 0;
  uint64_t delta_count = 0;
  uint64_t epoch = 0;
};

class GeometrySnapshotWriter
{
 public:
  SnapshotWriteResult write(GeometryStore& store, const SnapshotWriteOptions& options) const;
};

}  // namespace ecc::geometry
