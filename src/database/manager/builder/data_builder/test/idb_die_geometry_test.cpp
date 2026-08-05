// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// ***************************************************************************************

#include "layout_read.h"
#include "layout_write.h"

#include <filesystem>
#include <stdexcept>
#include <string>

#include "utility/logger/Logger.hpp"
namespace {

void require(bool condition, const std::string& message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void requireRect(idb::IdbRect* rect, int32_t lx, int32_t ly, int32_t ux, int32_t uy, const std::string& message)
{
  require(rect != nullptr, message + ": rectangle is null");
  require(rect->get_low_x() == lx && rect->get_low_y() == ly && rect->get_high_x() == ux && rect->get_high_y() == uy, message);
}

void testCoreDoesNotAliasDieBoundingBox()
{
  idb::IdbLayout layout;
  layout.initDie(0, 0, 49319, 49319);

  auto* die_bbox = layout.get_die()->get_bounding_box();
  auto* core = layout.get_core();
  const bool has_independent_bbox = core->get_bounding_box() != die_bbox;

  // Avoid a double delete while reporting the regression in the old implementation.
  if (!has_independent_bbox) {
    core->set_bounding_box(new idb::IdbRect());
  }

  require(has_independent_bbox, "core and die must own independent bounding boxes");
  core->set_bounding_box(2000, 2800, 47200, 46200);
  requireRect(layout.get_die()->get_bounding_box(), 0, 0, 49319, 49319, "changing core must not change die");
}

void testArchiveUsesDiePointsAsCanonicalGeometry()
{
  const auto archive_dir = std::filesystem::temp_directory_path() / "idb_die_geometry_test";
  std::filesystem::remove_all(archive_dir);

  idb::IdbLayout source;
  source.initDie(0, 0, 49319, 49319);
  source.get_die()->IdbObject::set_bounding_box(2000, 2800, 47200, 46200);

  idb::LayoutWrite writer(&source);
  require(writer.writeLayout(archive_dir.string(), false), "failed to write stale layout archive");

  idb::IdbLayout restored;
  idb::LayoutRead reader;
  const bool loaded = reader.readLayout(&restored, archive_dir.string(), false);

  std::filesystem::remove_all(archive_dir);

  require(loaded, "failed to read stale layout archive");
  requireRect(restored.get_die()->get_bounding_box(), 0, 0, 49319, 49319, "DIEAREA points must override stale serialized geometry");
}

void testArchiveWithoutDiePointsUsesSerializedGeometry()
{
  const auto archive_dir = std::filesystem::temp_directory_path() / "idb_die_geometry_without_points_test";
  std::filesystem::remove_all(archive_dir);

  idb::IdbLayout source;
  source.get_die()->IdbObject::set_bounding_box(0, 0, 49319, 49319);

  idb::LayoutWrite writer(&source);
  require(writer.writeLayout(archive_dir.string(), false), "failed to write layout archive without DIEAREA points");

  idb::IdbLayout restored;
  idb::LayoutRead reader;
  const bool loaded = reader.readLayout(&restored, archive_dir.string(), false);

  std::filesystem::remove_all(archive_dir);

  require(loaded, "failed to read layout archive without DIEAREA points");
  requireRect(restored.get_die()->get_bounding_box(), 0, 0, 49319, 49319,
              "serialized die geometry must be preserved when DIEAREA points are absent");
}

}  // namespace

int main()
{
  try {
    testCoreDoesNotAliasDieBoundingBox();
    testArchiveUsesDiePointsAsCanonicalGeometry();
    testArchiveWithoutDiePointsUsesSerializedGeometry();
  } catch (const std::exception& error) {
    ECCLOG.warn(ecc::Loc::current(), error.what());
    return 1;
  }
  return 0;
}
