#include "tcl_geometry.h"

#include "GeometryBuilder.h"
#include "GeometrySnapshotWriter.h"
#include "GeometryStore.h"
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

  ecc::geometry::GeometrySnapshotWriter writer;
  const ecc::geometry::SnapshotWriteResult write_result =
      writer.write(store, ecc::geometry::SnapshotWriteOptions{std::filesystem::path(output_path)});
  if (!write_result.ok) {
    std::cerr << "failed to write geometry snapshot: " << output_path << "\n";
    return 0;
  }

  std::cout << "geometry snapshot written: " << write_result.manifest_path.string() << "\n";
  print_geometry_report(build_result, store, std::cout);
  return 1;
}

}  // namespace tcl
