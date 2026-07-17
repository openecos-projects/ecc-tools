#include "tcl_geometry.h"

#include "GeometryBuilder.h"
#include "GeometrySnapshotWriter.h"
#include "GeometryStore.h"
#include "IdbUnits.h"
#include "idm.h"

#include <filesystem>
#include <iostream>

namespace tcl {
namespace {

void print_geometry_report(const ecc::geometry::GeometryBuildResult& build_result, const ecc::geometry::GeometryStore& store,
                           std::ostream& out)
{
  out << "shape_count=" << build_result.shape_count << "\n";
  for (const auto& [owner_type, count] : store.count_alive_shapes_by_owner_type()) {
    out << "owner_type." << ecc::geometry::owner_type_label(owner_type) << "=" << count << "\n";
  }
  for (const auto& [layer_id, count] : store.count_alive_shapes_by_layer()) {
    out << "layer." << layer_id << "=" << count << "\n";
  }
}

void populate_snapshot_design_metadata(ecc::geometry::SnapshotWriteOptions& write_options, idb::IdbDesign& design,
                                       idb::IdbLayout& layout)
{
  write_options.design_name = design.get_design_name();
  write_options.design_version = design.get_version();
  if (design.get_units() != nullptr) {
    write_options.dbu_per_micron = design.get_units()->get_micron_dbu();
  } else if (layout.get_units() != nullptr) {
    write_options.dbu_per_micron = layout.get_units()->get_micron_dbu();
  }
  write_options.manufacture_grid = layout.get_munufacture_grid();
}

}  // namespace

CmdGeometrySnapshot::CmdGeometrySnapshot(const char* cmd_name) : TclCmd(cmd_name)
{
  addOption(new TclStringOption(TCL_PATH, 1, nullptr));
}

unsigned CmdGeometrySnapshot::check()
{
  TclOption* path_option = getOptionOrArg(TCL_PATH);
  LOG_FATAL_IF(!path_option);
  return 1;
}

unsigned CmdGeometrySnapshot::exec()
{
  if (!check()) {
    return 0;
  }

  TclOption* path_option = getOptionOrArg(TCL_PATH);
  const char* output_path = path_option->getStringVal();
  if (output_path == nullptr || std::string(output_path).empty()) {
    std::cerr << "geometry_snapshot requires -path <snapshot-dir>\n";
    return 0;
  }

  idb::IdbDesign* design = dmInst->get_idb_design();
  idb::IdbLayout* layout = dmInst->get_idb_layout();
  if (layout == nullptr && design != nullptr) {
    layout = design->get_layout();
  }
  if (design == nullptr || layout == nullptr) {
    std::cerr << "geometry_snapshot requires loaded idb design and layout\n";
    return 0;
  }

  ecc::geometry::GeometryStore store;
  ecc::geometry::GeometryBuilder builder;
  const ecc::geometry::GeometryBuildResult build_result = builder.rebuild_from_design(*design, *layout, store);
  ecc::geometry::SnapshotWriteOptions write_options{std::filesystem::path(output_path)};
  write_options.layers = builder.collect_layer_metadata(*layout);
  write_options.sites = builder.collect_site_metadata(*layout);
  write_options.masters = builder.collect_master_metadata(*layout);
  write_options.vias = builder.collect_via_metadata(*layout, *design);
  write_options.grids = builder.collect_grid_metadata(*layout);
  write_options.connectivity = builder.collect_connectivity_metadata(*design);
  write_options.nets = builder.collect_net_metadata(*design);
  write_options.buses = builder.collect_bus_metadata(*design);
  write_options.groups = builder.collect_group_metadata(*design);
  populate_snapshot_design_metadata(write_options, *design, *layout);

  ecc::geometry::GeometrySnapshotWriter writer;
  const ecc::geometry::SnapshotWriteResult write_result = writer.write(store, write_options);
  if (!write_result.ok) {
    std::cerr << "failed to write geometry snapshot: " << output_path << "\n";
    return 0;
  }

  std::cout << "geometry snapshot written: " << write_result.manifest_path.string() << "\n";
  print_geometry_report(build_result, store, std::cout);
  return 1;
}

}  // namespace tcl
