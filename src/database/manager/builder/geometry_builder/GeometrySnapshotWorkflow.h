#pragma once

namespace ecc::geometry {

enum class GeometrySnapshotRunMode
{
  kSnapshot,
  kApplyEdit,
};

struct GeometrySnapshotPreparation
{
  bool ok = false;
  bool rebuild_from_design = false;
};

GeometrySnapshotPreparation plan_geometry_snapshot_preparation(GeometrySnapshotRunMode mode, bool has_existing_manifest,
                                                               bool restored_existing_snapshot);

}  // namespace ecc::geometry
