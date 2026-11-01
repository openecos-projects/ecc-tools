#pragma once

#include "GeometrySnapshotWriter.h"

#include <filesystem>

namespace idb {
class IdbDesign;
class IdbLayout;
}

namespace ecc::geometry {

SnapshotWriteResult export_geometry_snapshot(idb::IdbDesign& design, idb::IdbLayout& layout,
                                             const std::filesystem::path& output_dir,
                                             std::optional<GeometryDrcDistribution> drc = std::nullopt,
                                             const std::filesystem::path& thumbnail_path = {});

}  // namespace ecc::geometry
