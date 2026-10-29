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

#include "GeometryEditSession.h"
#include "GeometrySnapshotExporter.h"
#include "IdbDesign.h"
#include "IdbNet.h"
#include "IdbSpecialNet.h"
#include "py_clock_tree.h"
#include "feature_manager.h"
#include <idm.h>

#include <optional>
#include <utility>

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

bool initIdb(const std::filesystem::path& config_path)
{
  return dmInst->init(config_path.string());
}

bool initTechLef(const std::filesystem::path& techlef_path)
{
  const std::string techlef_path_ = techlef_path.string();
  dmInst->get_config().set_tech_lef_path(techlef_path_);
  return dmInst->readLef(vector<string>{techlef_path_}, true);
}

bool initLef(const std::vector<std::filesystem::path>& lef_paths)
{
  std::vector<std::string> lef_paths_;
  lef_paths_.reserve(lef_paths.size());
  for (const auto& lef_path : lef_paths) {
    lef_paths_.push_back(lef_path.string());
  }
  dmInst->get_config().set_lef_paths(lef_paths_);
  return dmInst->readLef(lef_paths_);
}

bool initDef(const std::filesystem::path& def_path)
{
  const std::string def_path_ = def_path.string();
  dmInst->get_config().set_def_path(def_path_);
  return dmInst->readDef(def_path_);
}

bool initVerilog(const std::filesystem::path& verilog_path, const std::string& top_module)
{
  const std::string verilog_path_ = verilog_path.string();
  dmInst->get_config().set_verilog_path(verilog_path_);
  return dmInst->readVerilog(verilog_path_, top_module);
}

bool initLvsVerilog(const std::filesystem::path& verilog_path, const std::string& top_module)
{
  const std::string verilog_path_ = verilog_path.string();
  dmInst->get_config().set_verilog_path(verilog_path_);
  return dmInst->addVerilog(verilog_path_, top_module);
}

bool initLib(const std::vector<std::filesystem::path>& lib_paths, std::optional<int32_t> thread_number)
{
  std::vector<std::string> lib_paths_;
  lib_paths_.reserve(lib_paths.size());
  for (const auto& lib_path : lib_paths) {
    lib_paths_.push_back(lib_path.string());
  }
  dmInst->get_config().set_lib_paths(lib_paths_);
  if (thread_number.has_value()) {
    dmInst->get_config().set_thread_number(*thread_number);
  }
  return dmInst->readLib(lib_paths_);
}

bool initSdc(const std::filesystem::path& sdc_path)
{
  dmInst->get_config().set_sdc_path(sdc_path.string());
  return true;
}

bool initSpef(const std::filesystem::path& spef_path)
{
  const std::string spef_path_ = spef_path.string();
  dmInst->get_config().set_spef_path(spef_path_);
  return dmInst->readSpef(spef_path_);
}

bool initVcd(const std::filesystem::path& vcd_path)
{
  const std::string vcd_path_ = vcd_path.string();
  dmInst->get_config().set_vcd_path(vcd_path_);
  return dmInst->readVcd(vcd_path_);
}

bool saveDef(const std::filesystem::path& def_name)
{
  return dmInst->saveDef(def_name.string());
}

bool saveMacroTCL(const std::filesystem::path& tcl_name)
{
  return dmInst->saveMacroTCL(tcl_name.string());
}

std::size_t canonicalizeNetNames()
{
  auto* design = dmInst->get_idb_design();
  return design == nullptr ? 0 : design->canonicalizeNetNames();
}

bool saveNetList(const std::filesystem::path& netlist_path, std::set<std::string> exclude_cell_names /* = {} */,
                 bool is_add_space_for_escape_name /* = false*/)
{
  dmInst->saveVerilog(netlist_path.string(), std::move(exclude_cell_names), is_add_space_for_escape_name);
  return true;
}

bool saveGDSII(const std::filesystem::path& gds_name, const std::filesystem::path& layer_map_path)
{
  return dmInst->saveGDSII(gds_name.string(), layer_map_path.string());
}

bool saveJson(const std::filesystem::path& path)
{
  std::string options = "";

  return dmInst->saveJSON(path.string(), options);
}

bool saveGeometrySnapshot(const std::filesystem::path& output_dir, bool include_drc,
                          const std::optional<std::filesystem::path>& thumbnail_path,
                          const std::optional<std::string>& clock_tree_stage,
                          const pybind11::object& clock_tree_graph)
{
  idb::IdbDesign* design = dmInst->get_idb_design();
  idb::IdbLayout* layout = dmInst->get_idb_layout();
  if (design == nullptr || layout == nullptr) {
    return false;
  }

  std::optional<ecc::geometry::GeometryDrcDistribution> drc;
  if (include_drc) {
    drc.emplace();
    const auto& nets = design->get_net_list()->get_net_list();
    const auto& special_nets = design->get_special_net_list()->get_net_list();
    for (const auto& [type, layers] : featureInst->get_type_layer_violation_map()) {
      for (const auto& [layer, violations] : layers) {
        auto& records = (*drc)[type][layer];
        records.reserve(violations.size());
        for (const ids::Violation& violation : violations) {
          ecc::geometry::GeometryDrcViolation record;
          record.bbox = {violation.ll_x, violation.ll_y, violation.ur_x, violation.ur_y};
          record.required_size = violation.required_size;
          for (int32_t net_idx : violation.violation_net_set) {
            if (net_idx >= 0 && net_idx < static_cast<int32_t>(nets.size())) {
              record.nets.push_back(nets[net_idx]->get_net_name());
            } else {
              const int32_t special_idx = net_idx - static_cast<int32_t>(nets.size());
              record.nets.push_back(special_idx >= 0 && special_idx < static_cast<int32_t>(special_nets.size())
                                        ? special_nets[special_idx]->get_net_name()
                                        : "obs");
            }
          }
          records.push_back(std::move(record));
        }
      }
    }
  }

  std::optional<ecc::geometry::ClockTreeCollectOptions> clock_tree_options;
  std::optional<ecc::geometry::ClockTreeSnapshot> clock_tree_snapshot;
  if (clock_tree_stage) {
    ecc::geometry::ClockTreeCollectOptions options;
    if (*clock_tree_stage == "postFloorplan") {
      options.stage = ecc::geometry::ClockTreeStage::kPostFloorplan;
      options.timing_unavailable_reason =
          "postFloorplan clock network has topology only; insertion delay was not requested";
    } else if (*clock_tree_stage == "CTS") {
      options.stage = ecc::geometry::ClockTreeStage::kCts;
      options.analysis_source = ecc::geometry::ClockTreeAnalysisSource::kCtsFastSta;
      options.timing_unavailable_reason =
          "CTS clock topology is available; committed FastSTA edge-level timing was unavailable";
      if (!clock_tree_graph.is_none()) {
        std::uint32_t dbu_per_micron = 0;
        if (design->get_units() != nullptr) {
          dbu_per_micron = static_cast<std::uint32_t>(design->get_units()->get_micron_dbu());
        } else if (layout->get_units() != nullptr) {
          dbu_per_micron = static_cast<std::uint32_t>(layout->get_units()->get_micron_dbu());
        }
        clock_tree_snapshot =
            python_interface::clockTreeSnapshotFromPython(clock_tree_graph, dbu_per_micron);
      }
    } else if (*clock_tree_stage == "filler") {
      options.stage = ecc::geometry::ClockTreeStage::kFiller;
      options.analysis_source = ecc::geometry::ClockTreeAnalysisSource::kIstaTypical;
      options.rc_corner = "TYPICAL";
      options.liberty_corner = "TYP";
      options.temperature_c = 25.0;
      options.timing_unavailable_reason =
          "filler clock topology is available; TYPICAL RC/iSTA timing capture was unavailable";
      if (!clock_tree_graph.is_none()) {
        std::uint32_t dbu_per_micron = 0;
        if (design->get_units() != nullptr) {
          dbu_per_micron = static_cast<std::uint32_t>(design->get_units()->get_micron_dbu());
        } else if (layout->get_units() != nullptr) {
          dbu_per_micron = static_cast<std::uint32_t>(layout->get_units()->get_micron_dbu());
        }
        clock_tree_snapshot =
            python_interface::clockTreeSnapshotFromPython(clock_tree_graph, dbu_per_micron);
      }
    } else {
      return false;
    }
    clock_tree_options = std::move(options);
  }
  return ecc::geometry::export_geometry_snapshot(*design, *layout, output_dir, std::move(drc),
                                                thumbnail_path.value_or(std::filesystem::path{}),
                                                std::move(clock_tree_options), std::move(clock_tree_snapshot))
      .ok;
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

bool saveGeometrySessionSnapshot(const std::filesystem::path& output_dir)
{
  return geometry_edit_session().write_snapshot(output_dir).ok;
}

bool resetGeometrySession()
{
  geometry_edit_session().reset();
  return true;
}

bool saveData(const std::filesystem::path& path)
{
  return dmInst->saveData(path.string());
}

bool resetData()
{
  // resetData destroys the IDB objects retained by the process-wide geometry
  // edit session. Drop those raw pointers before releasing the database.
  geometry_edit_session().reset();
  dmInst->resetData();
  return true;
}

bool loadData(const std::filesystem::path& path)
{
  // DataManager::loadData begins by resetting its current IdbBuilder, so the
  // session must be cleared before it can invalidate its design/layout
  // pointers. Callers initialize a new geometry session after a successful
  // load.
  geometry_edit_session().reset();
  return dmInst->loadData(path.string());
}

bool writeAbstractLef(const std::filesystem::path& output_lef_path)
{
  namespace fs = std::filesystem;

  return dmInst->saveLef(output_lef_path.string());
}

}  // namespace python_interface
