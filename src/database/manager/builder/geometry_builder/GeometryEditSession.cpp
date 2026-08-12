#include "GeometryEditSession.h"

#include "IdbDesign.h"
#include "IdbInstance.h"
#include "IdbLayout.h"
#include "IdbUnits.h"

namespace ecc::geometry {

bool GeometryEditSession::begin(idb::IdbDesign& design, idb::IdbLayout& layout)
{
  _builder.rebuild_from_design(design, layout, _store);
  _store.clear_delta_events();
  _design = &design;
  _layout = &layout;
  _initialized = true;
  return true;
}

GeometryInstanceSyncResult GeometryEditSession::sync_instance(idb::IdbInstance& instance)
{
  GeometryInstanceSyncResult result;
  if (!_initialized || _design == nullptr || _layout == nullptr) {
    result.snapshot_required = true;
    return result;
  }

  _store.clear_delta_events();
  result.sync = _builder.sync_instance(instance, _store);
  if (result.sync.ok) {
    result.ok = true;
    result.events = collect_delta_events();
    return result;
  }

  // Do not leave a partially synchronized render cache after an incremental
  // miss. Rebuild the derived cache and make the caller explicitly reload a
  // snapshot instead of applying an incomplete delta.
  _builder.rebuild_from_design(*_design, *_layout, _store);
  _store.clear_delta_events();
  result.ok = true;
  result.snapshot_required = true;
  return result;
}

SnapshotWriteResult GeometryEditSession::write_snapshot(const std::filesystem::path& output_dir)
{
  if (!_initialized || _design == nullptr || _layout == nullptr || output_dir.empty()) {
    return {};
  }

  SnapshotWriteOptions options{output_dir};
  options.layers = _builder.collect_layer_metadata(*_layout);
  options.sites = _builder.collect_site_metadata(*_layout);
  options.masters = _builder.collect_master_metadata(*_layout);
  options.vias = _builder.collect_via_metadata(*_layout, *_design);
  options.grids = _builder.collect_grid_metadata(*_layout);
  options.connectivity = _builder.collect_connectivity_metadata(*_design);
  options.nets = _builder.collect_net_metadata(*_design);
  options.buses = _builder.collect_bus_metadata(*_design);
  options.groups = _builder.collect_group_metadata(*_design);
  options.design_name = _design->get_design_name();
  options.design_version = _design->get_version();
  if (_design->get_units() != nullptr) {
    options.dbu_per_micron = _design->get_units()->get_micron_dbu();
  } else if (_layout->get_units() != nullptr) {
    options.dbu_per_micron = _layout->get_units()->get_micron_dbu();
  }
  options.manufacture_grid = _layout->get_munufacture_grid();

  GeometrySnapshotWriter writer;
  return writer.write(_store, options);
}

void GeometryEditSession::reset()
{
  _store.clear();
  _design = nullptr;
  _layout = nullptr;
  _initialized = false;
}

std::vector<GeometryDeltaShape> GeometryEditSession::collect_delta_events() const
{
  std::vector<GeometryDeltaShape> result;
  result.reserve(_store.delta_events().size());
  for (const GeometryDeltaEvent& event : _store.delta_events()) {
    GeometryDeltaShape delta;
    delta.event = event;
    if (const ShapeRecord* shape = _store.find_shape(event.shape_id); shape != nullptr) {
      delta.shape = *shape;
      delta.owner = _store.owner_of(event.shape_id);
      delta.has_shape = true;
    }
    result.push_back(delta);
  }
  return result;
}

}  // namespace ecc::geometry
