#include "GeometrySnapshotWorkflow.h"

namespace ecc::geometry {

GeometrySnapshotPreparation plan_geometry_snapshot_preparation(GeometrySnapshotRunMode mode, bool has_existing_manifest,
                                                               bool restored_existing_snapshot)
{
  if (mode == GeometrySnapshotRunMode::kApplyEdit) {
    return GeometrySnapshotPreparation{has_existing_manifest && restored_existing_snapshot, false};
  }

  return GeometrySnapshotPreparation{true, true};
}

}  // namespace ecc::geometry
