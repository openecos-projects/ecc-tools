#pragma once

#include "GeometryStore.h"

#include <cstdint>
#include <filesystem>

namespace ecc::geometry {

struct SnapshotReadOptions
{
  std::filesystem::path manifest_path;
};

struct SnapshotReadResult
{
  bool ok = false;
  std::filesystem::path manifest_path;
  uint64_t shape_count = 0;
  uint64_t owner_count = 0;
  uint64_t payload_size = 0;
};

class GeometrySnapshotReader
{
 public:
  SnapshotReadResult read(const SnapshotReadOptions& options, GeometryStore& store) const;
};

}  // namespace ecc::geometry
