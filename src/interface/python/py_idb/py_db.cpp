// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "utility/logger/Logger.hpp"
#include "py_db.h"

#include "db_fm/file_soc.h"
#include "GeometryEditSession.h"
#include "GeometrySnapshotExporter.h"
#include <idm.h>
#include "view_json_io.h"

namespace python_interface {
namespace {

ecc::geometry::GeometryEditSession& geometry_edit_session()
{
  static ecc::geometry::GeometryEditSession session;
  return session;
}

pybind11::dict rect_to_dict(const ecc::geometry::Rect32& rect)
{
  pybind11::dict result;
  result["lx"] = rect.lx;
  result["ly"] = rect.ly;
  result["hx"] = rect.hx;
  result["hy"] = rect.hy;
  return result;
}

const char* delta_op_name(ecc::geometry::GeometryDeltaOp op)
{
  switch (op) {
    case ecc::geometry::GeometryDeltaOp::kInsert:
      return "insert";
    case ecc::geometry::GeometryDeltaOp::kUpdate:
      return "update";
    case ecc::geometry::GeometryDeltaOp::kDelete:
      return "delete";
    default:
      return "none";
  }
}

pybind11::dict owner_to_dict(const ecc::geometry::OwnerRef& owner)
{
  pybind11::dict result;
  result["type"] = std::string(ecc::geometry::owner_type_label(owner.type));
  result["ownerId"] = owner.owner_id;
  result["path0"] = owner.path0;
  result["path1"] = owner.path1;
  result["path2"] = owner.path2;
  result["path3"] = owner.path3;
  return result;
}

pybind11::dict delta_to_dict(const ecc::geometry::GeometryDeltaShape& delta)
{
  pybind11::dict result;
  result["sequenceId"] = delta.event.sequence_id;
  result["commandId"] = delta.event.command_id;
  result["op"] = delta_op_name(delta.event.op);
  result["shapeId"] = delta.event.shape_id;
  result["oldVersion"] = delta.event.old_version;
  result["newVersion"] = delta.event.new_version;
  result["oldBbox"] = rect_to_dict(delta.event.old_bbox);
  result["newBbox"] = rect_to_dict(delta.event.new_bbox);
  if (delta.has_shape) {
    pybind11::dict shape;
    shape["id"] = delta.shape.id;
    shape["version"] = delta.shape.version;
    shape["layerId"] = delta.shape.layer_id;
    shape["kind"] = static_cast<uint8_t>(delta.shape.kind);
    shape["state"] = static_cast<uint8_t>(delta.shape.state);
    shape["bbox"] = rect_to_dict(delta.shape.bbox);
    result["shape"] = std::move(shape);
    result["owner"] = owner_to_dict(delta.owner);
  }
  return result;
}

}  // namespace

bool initIdb(const std::string& config_path)
{
  return dmInst->init(config_path);
}

bool initTechLef(const std::string& techlef_path)
{
  dmInst->get_config().set_tech_lef_path(techlef_path);
  return dmInst->readLef(vector<string>{techlef_path}, true);
}

bool initLef(const std::vector<std::string>& lef_paths)
{
  dmInst->get_config().set_lef_paths(lef_paths);
  return dmInst->readLef(lef_paths);
}

bool initDef(const std::string& def_path)
{
  dmInst->get_config().set_def_path(def_path);
  return dmInst->readDef(def_path);
}

bool initVerilog(const std::string& verilog_path, const std::string& top_module)
{
  dmInst->get_config().set_verilog_path(verilog_path);
  return dmInst->readVerilog(verilog_path, top_module);
}

bool initLvsVerilog(const std::string& verilog_path, const std::string& top_module)
{
  dmInst->get_config().set_verilog_path(verilog_path);
  return dmInst->addVerilog(verilog_path, top_module);
}

bool initLib(const std::vector<std::string>& lib_paths)
{
  dmInst->get_config().set_lib_paths(lib_paths);
  return dmInst->readLib(lib_paths);
}

bool initSdc(const std::string& sdc_path)
{
  dmInst->get_config().set_sdc_path(sdc_path);
  return true;
}

bool initSpef(const std::string& spef_path)
{
  dmInst->get_config().set_spef_path(spef_path);
  return dmInst->readSpef(spef_path);
}

bool initVcd(const std::string& vcd_path)
{
  dmInst->get_config().set_vcd_path(vcd_path);
  return dmInst->readVcd(vcd_path);
}

bool saveDef(const std::string& def_name)
{
  return dmInst->saveDef(def_name);
}

bool saveMacroTCL(const std::string& def_name)
{
  return dmInst->saveMacroTCL(def_name);
}

bool saveNetList(const std::string& netlist_path, std::set<std::string> exclude_cell_names /* = {} */,
                 bool is_add_space_for_escape_name /* = false*/)
{
  dmInst->saveVerilog(netlist_path, std::move(exclude_cell_names), is_add_space_for_escape_name);
  return true;
}

bool saveGDSII(const std::string& gds_name, bool is_hardened /* = false */)
{
  return dmInst->saveGDSII(gds_name, is_hardened);
}

bool saveJson(const std::string& path)
{
  std::string options = "";

  return dmInst->saveJSON(path, options);
}

bool saveViewJson(const std::string& output_dir, const std::string& json_format, bool compress)
{
  idb::ViewJsonWriteOptions options;
  if (!idb::parseViewJsonFormat(json_format, options.format)) {
    ECCLOG.warn(ecc::Loc::current(), "Save view json failed: unsupported json_format `", json_format, "`, expected `pretty` or `compact`.");
    return false;
  }
  options.compress = compress;
  return dmInst->saveViewJson(output_dir, options);
}

bool saveGeometrySnapshot(const std::string& output_dir)
{
  idb::IdbDesign* design = dmInst->get_idb_design();
  idb::IdbLayout* layout = dmInst->get_idb_layout();
  if (design == nullptr || layout == nullptr) {
    return false;
  }

  return ecc::geometry::export_geometry_snapshot(*design, *layout, output_dir).ok;
}

bool placeInstance(const std::string& inst_name, int llx, int lly, const std::string& orient, const std::string& cellmaster,
                   const std::string& source, const std::string& placement_status, bool create_if_missing)
{
  return dmInst->placeInst(inst_name, llx, lly, orient, cellmaster, source, placement_status, create_if_missing);
}

bool initializeGeometrySession()
{
  idb::IdbDesign* design = dmInst->get_idb_design();
  idb::IdbLayout* layout = dmInst->get_idb_layout();
  if (design == nullptr || layout == nullptr) {
    return false;
  }

  return geometry_edit_session().begin(*design, *layout);
}

pybind11::dict syncInstanceGeometry(const std::string& inst_name)
{
  pybind11::dict result;
  result["ok"] = false;
  result["snapshotRequired"] = true;
  result["updatedShapeCount"] = 0;
  result["insertedShapeCount"] = 0;
  result["deletedShapeCount"] = 0;
  result["missingShapeCount"] = 0;
  result["events"] = pybind11::list();

  if (!geometry_edit_session().initialized()) {
    return result;
  }

  idb::IdbDesign* design = dmInst->get_idb_design();
  if (design == nullptr || design->get_instance_list() == nullptr) {
    return result;
  }

  idb::IdbInstance* instance = design->get_instance_list()->find_instance(inst_name);
  if (instance == nullptr) {
    result["missingShapeCount"] = 1;
    return result;
  }

  const ecc::geometry::GeometryInstanceSyncResult sync = geometry_edit_session().sync_instance(*instance);
  result["ok"] = sync.ok;
  result["snapshotRequired"] = sync.snapshot_required;
  result["updatedShapeCount"] = sync.sync.updated_shape_count;
  result["insertedShapeCount"] = sync.sync.added_shape_count;
  result["deletedShapeCount"] = sync.sync.deleted_shape_count;
  result["missingShapeCount"] = sync.sync.missing_shape_count;

  pybind11::list events;
  for (const ecc::geometry::GeometryDeltaShape& delta : sync.events) {
    events.append(delta_to_dict(delta));
  }
  result["events"] = std::move(events);
  return result;
}

bool saveGeometrySessionSnapshot(const std::string& output_dir)
{
  return geometry_edit_session().write_snapshot(output_dir).ok;
}

bool resetGeometrySession()
{
  geometry_edit_session().reset();
  return true;
}

bool applyViewJsonEdits(const std::string& edits_path, bool compress)
{
  return dmInst->applyViewJsonEdits(edits_path, compress);
}

bool saveData(const std::string& path)
{
  return dmInst->saveData(path);
}

bool resetData()
{
  // resetData destroys the IDB objects retained by the process-wide geometry
  // edit session. Drop those raw pointers before releasing the database.
  geometry_edit_session().reset();
  dmInst->resetData();
  return true;
}

bool loadData(const std::string& path)
{
  // DataManager::loadData begins by resetting its current IdbBuilder, so the
  // session must be cleared before it can invalidate its design/layout
  // pointers. Callers initialize a new geometry session after a successful
  // load.
  geometry_edit_session().reset();
  return dmInst->loadData(path);
}

bool writeSocJson(const std::string& path, const std::vector<std::string>& harden_cores /* = {} */)
{
  idb::JsonSoc soc_file(path, harden_cores);
  return soc_file.saveFileData();
}

bool writeAbstractLef(const std::string& output_lef_path)
{
  namespace fs = std::filesystem;

  return dmInst->saveLef(output_lef_path);
}

}  // namespace python_interface
