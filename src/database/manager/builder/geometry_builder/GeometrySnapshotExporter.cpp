#include "GeometrySnapshotExporter.h"

#include "GeometryBuilder.h"
#include "GeometryStore.h"
#include "IdbDesign.h"
#include "IdbLayout.h"
#include "IdbUnits.h"

namespace ecc::geometry {
namespace {

void populate_design_metadata(SnapshotWriteOptions& options, idb::IdbDesign& design, idb::IdbLayout& layout)
{
  options.design_name = design.get_design_name();
  options.design_version = design.get_version();
  if (design.get_units() != nullptr) {
    options.dbu_per_micron = design.get_units()->get_micron_dbu();
  } else if (layout.get_units() != nullptr) {
    options.dbu_per_micron = layout.get_units()->get_micron_dbu();
  }
  options.manufacture_grid = layout.get_munufacture_grid();
}

}  // namespace

SnapshotWriteResult export_geometry_snapshot(idb::IdbDesign& design, idb::IdbLayout& layout,
                                             const std::filesystem::path& output_dir)
{
  if (output_dir.empty()) {
    return {};
  }

  GeometryStore store;
  GeometryBuilder builder;
  builder.rebuild_from_design(design, layout, store);

  SnapshotWriteOptions options{output_dir};
  options.layers = builder.collect_layer_metadata(layout);
  populate_design_metadata(options, design, layout);

  GeometrySnapshotWriter writer;
  return writer.write(store, options);
}

}  // namespace ecc::geometry
