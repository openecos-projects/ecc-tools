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
#include "py_db.h"

#include "../py_path_utils.h"
#include "db_fm/file_soc.h"
#include <idm.h>
#include "view_json_io.h"

namespace python_interface {

bool initIdb(const std::filesystem::path& config_path)
{
  const std::string config_path_ = config_path.string();
  return dmInst->init(config_path_);
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

bool initLib(const std::vector<std::filesystem::path>& lib_paths)
{
  std::vector<std::string> lib_paths_;
  lib_paths_.reserve(lib_paths.size());
  for (const auto& lib_path : lib_paths) {
    lib_paths_.push_back(lib_path.string());
  }
  dmInst->get_config().set_lib_paths(lib_paths_);
  return dmInst->readLib(lib_paths_);
}

bool initSdc(const std::optional<std::filesystem::path>& sdc_path)
{
  const std::string sdc_path_ = path_or_empty(sdc_path);
  dmInst->get_config().set_sdc_path(sdc_path_);
  return true;
}

bool initSpef(const std::filesystem::path& spef_path)
{
  const std::string spef_path_ = spef_path.string();
  dmInst->get_config().set_spef_path(spef_path_);
  return dmInst->readSpef(spef_path_);
}

bool saveDef(const std::filesystem::path& def_name)
{
  const std::string def_name_ = def_name.string();
  return dmInst->saveDef(def_name_);
}

bool saveMacroTCL(const std::filesystem::path& tcl_name)
{
  const std::string tcl_name_ = tcl_name.string();
  return dmInst->saveMacroTCL(tcl_name_);
}

bool saveNetList(const std::filesystem::path& netlist_path, std::set<std::string> exclude_cell_names /* = {} */,
                 bool is_add_space_for_escape_name /* = false*/)
{
  const std::string netlist_path_ = netlist_path.string();
  dmInst->saveVerilog(netlist_path_, std::move(exclude_cell_names), is_add_space_for_escape_name);
  return true;
}

bool saveGDSII(const std::filesystem::path& gds_name, bool is_hardened /* = false */)
{
  const std::string gds_name_ = gds_name.string();
  return dmInst->saveGDSII(gds_name_, is_hardened);
}

bool saveJson(const std::filesystem::path& path)
{
  const std::string path_ = path.string();
  std::string options = "";

  return dmInst->saveJSON(path_, options);
}

bool saveViewJson(const std::filesystem::path& output_dir, const std::string& json_format, bool compress)
{
  const std::string output_dir_ = output_dir.string();
  idb::ViewJsonWriteOptions options;
  if (!idb::parseViewJsonFormat(json_format, options.format)) {
    std::cout << "Save view json failed: unsupported json_format `" << json_format << "`, expected `pretty` or `compact`." << std::endl;
    return false;
  }
  options.compress = compress;
  return dmInst->saveViewJson(output_dir_, options);
}

bool applyViewJsonEdits(const std::filesystem::path& edits_path, bool compress)
{
  const std::string edits_path_ = edits_path.string();
  return dmInst->applyViewJsonEdits(edits_path_, compress);
}

bool saveData(const std::filesystem::path& path)
{
  const std::string path_ = path.string();
  return dmInst->saveData(path_);
}

bool resetData()
{
  dmInst->resetData();
  return true;
}

bool loadData(const std::filesystem::path& path)
{
  const std::string path_ = path.string();
  return dmInst->loadData(path_);
}

bool writeSocJson(const std::filesystem::path& path, const std::vector<std::string>& harden_cores /* = {} */)
{
  const std::string path_ = path.string();
  idb::JsonSoc soc_file(path_, harden_cores);
  return soc_file.saveFileData();
}

bool writeAbstractLef(const std::filesystem::path& output_lef_path)
{
  const std::string output_lef_path_ = output_lef_path.string();
  return dmInst->saveLef(output_lef_path_);
}

}  // namespace python_interface
