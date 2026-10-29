#pragma once

#include "ClockTreeCollector.h"
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
                                             const std::filesystem::path& thumbnail_path = {},
                                             std::optional<ClockTreeCollectOptions> clock_tree_options = std::nullopt,
                                             std::optional<ClockTreeSnapshot> clock_tree_snapshot = std::nullopt);

}  // namespace ecc::geometry
