#pragma once

#include "GeometrySnapshotWriter.h"

#include <filesystem>

namespace idb {
class IdbDesign;
class IdbLayout;
}

namespace ecc::geometry {

SnapshotWriteResult export_geometry_snapshot(idb::IdbDesign& design, idb::IdbLayout& layout,
                                             const std::filesystem::path& output_dir);

}  // namespace ecc::geometry
