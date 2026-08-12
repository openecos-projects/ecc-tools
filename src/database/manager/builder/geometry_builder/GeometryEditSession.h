#pragma once

#include "GeometryBuilder.h"
#include "GeometryDelta.h"
#include "GeometrySnapshotWriter.h"
#include "GeometryStore.h"
#include "OwnerRef.h"
#include "ShapeRecord.h"

#include <filesystem>
#include <vector>

namespace idb {
class IdbDesign;
class IdbInstance;
class IdbLayout;
}  // namespace idb

namespace ecc::geometry {

// A delta event plus the current, stable GeometryStore identity for its shape.
// Deleted shapes retain their last record and owner in GeometryStore, so they
// can be addressed by the consumer that receives the deletion event.
struct GeometryDeltaShape
{
  GeometryDeltaEvent event;
  ShapeRecord shape;
  OwnerRef owner;
  bool has_shape = false;
};

struct GeometryInstanceSyncResult
{
  bool ok = false;
  bool snapshot_required = false;
  GeometrySyncResult sync;
  std::vector<GeometryDeltaShape> events;
};

// Owns the derived GeometryStore for one in-memory IDB editing session. The
// caller owns the IDB lifetime and must initialize this object after loading
// the editable design, before applying incremental mutations.
class GeometryEditSession
{
 public:
  bool begin(idb::IdbDesign& design, idb::IdbLayout& layout);
  GeometryInstanceSyncResult sync_instance(idb::IdbInstance& instance);
  SnapshotWriteResult write_snapshot(const std::filesystem::path& output_dir);
  void reset();

  bool initialized() const { return _initialized; }
  GeometryStore& store() { return _store; }
  const GeometryStore& store() const { return _store; }

 private:
  std::vector<GeometryDeltaShape> collect_delta_events() const;

  GeometryBuilder _builder;
  GeometryStore _store;
  idb::IdbDesign* _design = nullptr;
  idb::IdbLayout* _layout = nullptr;
  bool _initialized = false;
};

}  // namespace ecc::geometry
