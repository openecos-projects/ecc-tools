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
#include "tcl_db_file.h"

#include "db_fm/file_soc.h"
#include "idm.h"
#include "report_manager.h"
#include "tool_manager.h"
namespace tcl {

CmdInitIdb::CmdInitIdb(const char* cmd_name) : TclCmd(cmd_name)
{
  auto* option = new TclStringOption(TCL_CONFIG, 1, nullptr);

  addOption(option);
}

unsigned CmdInitIdb::check()
{
  TclOption* option = getOptionOrArg(TCL_CONFIG);

  LOG_FATAL_IF(!option);

  return 1;
}

unsigned CmdInitIdb::exec()
{
  if (!check()) {
    return 0;
  }

  TclOption* option = getOptionOrArg(TCL_CONFIG);

  auto data_config = option->getStringVal();

  if (iplf::tmInst->idbStart(data_config)) {
    std::cout << "idb start." << std::endl;
  }

  return 1;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
CmdInitTechLef::CmdInitTechLef(const char* cmd_name) : TclCmd(cmd_name)
{
  auto* option = new TclStringOption(TCL_PATH, 1, nullptr);
  addOption(option);
}

unsigned CmdInitTechLef::check()
{
  //   TclOption* option = getOptionOrArg(TCL_PATH);
  //   LOG_FATAL_IF(!option);
  return 1;
}

unsigned CmdInitTechLef::exec()
{
  if (!check()) {
    return 0;
  }

  TclOption* option = getOptionOrArg(TCL_PATH);
  auto path = option->getStringVal();
  if (path != nullptr) {
    vector<string> path_list;
    path_list.push_back(path);
    dmInst->get_config().set_tech_lef_path(path);
    dmInst->readLef(path_list, true);
    return 1;
  } else {
    if (dmInst->get_config().get_tech_lef_path() != "") {
      vector<string> path_list;
      path_list.push_back(dmInst->get_config().get_tech_lef_path());
      dmInst->readLef(path_list, true);
    }
  }

  return 1;
}

CmdInitLef::CmdInitLef(const char* cmd_name) : TclCmd(cmd_name)
{
  auto* name_list = new TclStringListOption(TCL_PATH, 1);
  addOption(name_list);
}

unsigned CmdInitLef::check()
{
  //   TclOption* name_list = getOptionOrArg(TCL_PATH);
  //   LOG_FATAL_IF(!name_list);
  return 1;
}

unsigned CmdInitLef::exec()
{
  if (!check()) {
    return 0;
  }

  TclOption* name_list_option = getOptionOrArg(TCL_PATH);
  auto lef_path = name_list_option->getStringList();
  if (!lef_path.empty()) {
    dmInst->get_config().set_lef_paths(lef_path);
    dmInst->readLef(lef_path);
    return 1;
  } else {
    if (dmInst->get_config().get_lef_paths().size() > 0) {
      dmInst->readLef(dmInst->get_config().get_lef_paths());
    }
  }

  return 1;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

CmdInitDef::CmdInitDef(const char* cmd_name) : TclCmd(cmd_name)
{
  auto* path = new TclStringOption(TCL_PATH, 1);
  addOption(path);
}

unsigned CmdInitDef::check()
{
  TclOption* path = getOptionOrArg(TCL_PATH);
  LOG_FATAL_IF(!path);
  return 1;
}

unsigned CmdInitDef::exec()
{
  if (!check()) {
    return 0;
  }

  TclOption* def_name = getOptionOrArg(TCL_PATH);
  auto def_path = def_name->getStringVal();
  if (def_path != nullptr) {
    dmInst->get_config().set_def_path(def_path);
    dmInst->readDef(def_path);
    return 1;
  }
  return 1;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

CmdInitVerilog::CmdInitVerilog(const char* cmd_name) : TclCmd(cmd_name)
{
  auto* path = new TclStringOption(TCL_PATH, 1);
  auto* top = new TclStringOption(TCL_VERILOG_TOP, 1);
  addOption(path);
  addOption(top);
}

unsigned CmdInitVerilog::check()
{
  TclOption* path = getOptionOrArg(TCL_PATH);
  TclOption* top = getOptionOrArg(TCL_VERILOG_TOP);
  LOG_FATAL_IF(!path);
  LOG_FATAL_IF(!top);
  return 1;
}

unsigned CmdInitVerilog::exec()
{
  if (!check()) {
    return 0;
  }

  TclOption* path = getOptionOrArg(TCL_PATH);
  TclOption* top = getOptionOrArg(TCL_VERILOG_TOP);

  auto path_string = path->getStringVal();
  auto top_module = top->getStringVal();
  if (path_string != nullptr && top_module != nullptr) {
    dmInst->get_config().set_verilog_path(path_string);
    dmInst->readVerilog(path_string, top_module);
    return 1;
  }

  return 1;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

CmdSaveDef::CmdSaveDef(const char* cmd_name) : TclCmd(cmd_name)
{
  auto* option = new TclStringOption(TCL_NAME, 1, nullptr);
  addOption(option);

  auto* path = new TclStringOption(TCL_PATH, 1);
  addOption(path);
}

unsigned CmdSaveDef::check()
{
  TclOption* option = getOptionOrArg(TCL_NAME);
  LOG_FATAL_IF(!option);

  TclOption* path = getOptionOrArg(TCL_PATH);
  LOG_FATAL_IF(!path);
  return 1;
}

unsigned CmdSaveDef::exec()
{
  if (!check()) {
    return 0;
  }

  TclOption* option = getOptionOrArg(TCL_NAME);
  auto name = option->getStringVal();
  if (name != nullptr) {
    if (iplf::tmInst->idbSave(name)) {
      std::cout << "idb save success." << std::endl;
      return 1;
    }
  }

  TclOption* def_path = getOptionOrArg(TCL_PATH);
  auto str_path = def_path->getStringVal();
  if (str_path != nullptr) {
    dmInst->saveDef(str_path);
    return 1;
  }

  return 1;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

CmdSaveLef::CmdSaveLef(const char* cmd_name) : TclCmd(cmd_name)
{
  auto* path = new TclStringOption(TCL_PATH, 1);
  addOption(path);
}

unsigned CmdSaveLef::check()
{
  TclOption* path = getOptionOrArg(TCL_PATH);
  LOG_FATAL_IF(!path);
  return 1;
}

unsigned CmdSaveLef::exec()
{
  if (!check()) {
    return 0;
  }

  TclOption* path = getOptionOrArg(TCL_PATH);
  auto str_path = path->getStringVal();
  if (str_path != nullptr) {
    dmInst->saveLef(str_path);
    return 1;
  }

  return 1;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

CmdSaveNetlist::CmdSaveNetlist(const char* cmd_name) : TclCmd(cmd_name)
{
  auto* option = new TclStringOption(TCL_NAME, 1, nullptr);
  addOption(option);

  auto* path = new TclStringOption(TCL_PATH, 1);
  addOption(path);

  auto* exclude_cell_names = new TclStringListOption(EXCLUDE_CELL_NAMES, 1, {});
  addOption(exclude_cell_names);

  auto* is_add_space = new TclSwitchOption(TCL_ADD_SPACE);
  addOption(is_add_space);
}

unsigned CmdSaveNetlist::check()
{
  TclOption* option = getOptionOrArg(TCL_NAME);
  LOG_FATAL_IF(!option);

  TclOption* path = getOptionOrArg(TCL_PATH);
  LOG_FATAL_IF(!path);

  TclOption* exclude_cell_names = getOptionOrArg(EXCLUDE_CELL_NAMES);
  LOG_FATAL_IF(!exclude_cell_names);

  TclOption* is_add_space = getOptionOrArg(TCL_ADD_SPACE);
  LOG_FATAL_IF(!is_add_space);

  return 1;
}

unsigned CmdSaveNetlist::exec()
{
  if (!check()) {
    return 0;
  }

  TclOption* option = getOptionOrArg(TCL_NAME);
  auto name = option->getStringVal();
  if (name != nullptr) {
    if (iplf::tmInst->idbSave(name)) {
      std::cout << "idb save success." << std::endl;
      return 1;
    }
  }

  TclOption* verilog_path = getOptionOrArg(TCL_PATH);
  auto str_path = verilog_path->getStringVal();

  TclOption* exclude_cell_names_option = getOptionOrArg(EXCLUDE_CELL_NAMES);
  auto exclude_cell_names = exclude_cell_names_option->getStringList();

  std::set<std::string> new_exclude_cell_names;
  for (auto& exclude_cell_name : exclude_cell_names) {
    new_exclude_cell_names.insert(exclude_cell_name);
  }

  TclOption* is_add_space_option = getOptionOrArg(TCL_ADD_SPACE);
  bool is_add_space = false;
  if (is_add_space_option->is_set_val()) {
    is_add_space = true;
  }

  if (str_path != nullptr) {
    dmInst->saveVerilog(str_path, std::move(new_exclude_cell_names), is_add_space);
    return 1;
  }

  return 1;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

CmdSaveGDS::CmdSaveGDS(const char* cmd_name) : TclCmd(cmd_name)
{
  auto* option = new TclStringOption(TCL_NAME, 1, nullptr);
  addOption(option);

  auto* path = new TclStringOption(TCL_PATH, 1);
  addOption(path);

  auto* harden_option = new TclSwitchOption("-harden");
  addOption(harden_option);
}

unsigned CmdSaveGDS::check()
{
  TclOption* option = getOptionOrArg(TCL_NAME);
  LOG_FATAL_IF(!option);

  TclOption* path = getOptionOrArg(TCL_PATH);
  LOG_FATAL_IF(!path);

  TclOption* harden_option = getOptionOrArg("-harden");
  LOG_FATAL_IF(!harden_option);

  return 1;
}

unsigned CmdSaveGDS::exec()
{
  if (!check()) {
    return 0;
  }

  TclOption* def_path = getOptionOrArg(TCL_PATH);
  auto str_path = def_path->getStringVal();
  TclOption* harden_option = getOptionOrArg("-harden");
  bool is_harden = false;
  if (harden_option->is_set_val()) {
    is_harden = true;
  }
  if (str_path != nullptr) {
    dmInst->saveGDSII(str_path, is_harden);
    return 1;
  }
  return 1;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

CmdSaveJSON::CmdSaveJSON(const char* cmd_name) : TclCmd(cmd_name)
{
  // auto* option = new TclStringOption(TCL_NAME, 1, nullptr);
  // addOption(option);

  auto* path = new TclStringOption(TCL_PATH, 1);
  addOption(path);

  // auto* discard = new TclStringOption(TCL_JSON_OPTION, 1, nullptr);
  // addOption(discard);
}

unsigned CmdSaveJSON::check()
{
  // TclOption* option = getOptionOrArg(TCL_NAME);
  // LOG_FATAL_IF(!option);

  // TclOption* discard = getOptionOrArg(TCL_JSON_OPTION);
  // LOG_FATAL_IF(!discard);

  TclOption* path = getOptionOrArg(TCL_PATH);
  LOG_FATAL_IF(!path);
  return 1;
}

unsigned CmdSaveJSON::exec()
{
  if (!check()) {
    return 0;
  }

  TclOption* def_path = getOptionOrArg(TCL_PATH);
  // TclOption* discard = getOptionOrArg(TCL_JSON_OPTION);
  auto str_path = def_path->getStringVal();
  auto str_option = "";
  // std::cout<<str_path<<std::endl;
  if (str_path != nullptr) {
    dmInst->saveJSON(str_path, str_option);
    return 1;
  }

  return 1;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

CmdSaveViewJson::CmdSaveViewJson(const char* cmd_name) : TclCmd(cmd_name)
{
  auto* path = new TclStringOption(TCL_PATH, 1);
  addOption(path);
}

unsigned CmdSaveViewJson::check()
{
  TclOption* path = getOptionOrArg(TCL_PATH);
  LOG_FATAL_IF(!path);
  return 1;
}

unsigned CmdSaveViewJson::exec()
{
  if (!check()) {
    return 0;
  }

  TclOption* path = getOptionOrArg(TCL_PATH);
  auto* str_path = path->getStringVal();
  if (str_path == nullptr) {
    return 0;
  }

  return dmInst->saveViewJson(str_path) ? 1 : 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

CmdApplyViewJsonEdits::CmdApplyViewJsonEdits(const char* cmd_name) : TclCmd(cmd_name)
{
  auto* path = new TclStringOption(TCL_PATH, 1);
  addOption(path);
}

unsigned CmdApplyViewJsonEdits::check()
{
  TclOption* path = getOptionOrArg(TCL_PATH);
  LOG_FATAL_IF(!path);
  return 1;
}

unsigned CmdApplyViewJsonEdits::exec()
{
  if (!check()) {
    return 0;
  }

  TclOption* path = getOptionOrArg(TCL_PATH);
  auto* str_path = path->getStringVal();
  if (str_path == nullptr) {
    return 0;
  }

  return dmInst->applyViewJsonEdits(str_path) ? 1 : 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
CmdWriteSocJson::CmdWriteSocJson(const char* cmd_name) : TclCmd(cmd_name)
{
  auto* path = new TclStringOption(TCL_PATH, 1);
  addOption(path);

  auto* harden_cores = new TclStringListOption("-harden_cores", 1);
  addOption(harden_cores);
}

unsigned CmdWriteSocJson::check()
{
  TclOption* path = getOptionOrArg(TCL_PATH);
  LOG_FATAL_IF(!path);

  TclOption* harden_cores = getOptionOrArg("-harden_cores");
  LOG_FATAL_IF(!harden_cores);

  return 1;
}

unsigned CmdWriteSocJson::exec()
{
  if (!check()) {
    return 0;
  }

  TclOption* path = getOptionOrArg(TCL_PATH);
  auto* str_path = path->getStringVal();
  if (str_path == nullptr) {
    return 0;
  }

  TclOption* harden_cores = getOptionOrArg("-harden_cores");
  std::vector<std::string> harden_core_list;
  if (harden_cores) {
    harden_core_list = harden_cores->getStringList();
  }

  idb::JsonSoc soc_file(str_path, harden_core_list);
  return soc_file.saveFileData() ? 1 : 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
CmdWriteAbstractLef::CmdWriteAbstractLef(const char* cmd_name) : TclCmd(cmd_name)
{
  auto* path = new TclStringOption(TCL_PATH, 1);
  addOption(path);
}

unsigned CmdWriteAbstractLef::check()
{
  TclOption* path = getOptionOrArg(TCL_PATH);
  LOG_FATAL_IF(!path);

  return 1;
}

unsigned CmdWriteAbstractLef::exec()
{
  if (!check()) {
    return 0;
  }

  TclOption* path = getOptionOrArg(TCL_PATH);
  auto* str_path = path->getStringVal();
  if (str_path == nullptr) {
    return 0;
  }

  dmInst->saveLef(str_path);
  return 1;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
CmdSaveData::CmdSaveData(const char* cmd_name) : TclCmd(cmd_name)
{
  auto* path = new TclStringOption(TCL_PATH, 1);
  addOption(path);
}

unsigned CmdSaveData::check()
{
  TclOption* path = getOptionOrArg(TCL_PATH);
  LOG_FATAL_IF(!path);

  return 1;
}

unsigned CmdSaveData::exec()
{
  if (!check()) {
    return 0;
  }

  TclOption* path = getOptionOrArg(TCL_PATH);
  auto* str_path = path->getStringVal();
  if (str_path == nullptr) {
    return 0;
  }

  return dmInst->saveData(str_path) ? 1 : 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
CmdLoadData::CmdLoadData(const char* cmd_name) : TclCmd(cmd_name)
{
  auto* path = new TclStringOption(TCL_PATH, 1);
  addOption(path);
}

unsigned CmdLoadData::check()
{
  TclOption* path = getOptionOrArg(TCL_PATH);
  LOG_FATAL_IF(!path);

  return 1;
}

unsigned CmdLoadData::exec()
{
  if (!check()) {
    return 0;
  }

  TclOption* path = getOptionOrArg(TCL_PATH);
  auto* str_path = path->getStringVal();
  if (str_path == nullptr) {
    return 0;
  }

  return dmInst->loadData(str_path) ? 1 : 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
CmdResetData::CmdResetData(const char* cmd_name) : TclCmd(cmd_name)
{
}

unsigned CmdResetData::check()
{
  return 1;
}

unsigned CmdResetData::exec()
{
  if (!check()) {
    return 0;
  }

  dmInst->resetData();
  return 1;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
CmdValidateIdb::CmdValidateIdb(const char* cmd_name) : TclCmd(cmd_name)
{
  addOption(new TclStringOption(TCL_PATH, 1, nullptr));
  addOption(new TclIntOption("-check_floating", 1, 0));
}

unsigned CmdValidateIdb::check()
{
  return 1;
}

unsigned CmdValidateIdb::exec()
{
  if (!check()) {
    return 0;
  }

  auto* design = dmInst->get_idb_design();
  if (design == nullptr) {
    std::cout << "iDB validate failed: design is null." << std::endl;
    return 0;
  }

  const bool check_floating = getOptionOrArg("-check_floating")->getIntVal() != 0;
  auto result = design->validateConnectivity(check_floating);

  TclOption* path_option = getOptionOrArg(TCL_PATH);
  const char* path = path_option == nullptr ? nullptr : path_option->getStringVal();
  if (path != nullptr && path[0] != '\0' && !design->writeConnectivitySnapshot(path, check_floating)) {
    std::cout << "iDB validate failed: cannot write snapshot " << path << std::endl;
    return 0;
  }

  std::cout << "iDB validate " << (result.ok ? "passed" : "failed") << ": duplicate_net=" << result.duplicate_net_count
            << ", duplicate_instance=" << result.duplicate_instance_count << ", duplicate_io_pin=" << result.duplicate_io_pin_count
            << ", stale_regular_pin_ref=" << result.stale_regular_pin_ref_count
            << ", stale_special_pin_ref=" << result.stale_special_pin_ref_count
            << ", pin_reverse_mismatch=" << result.pin_reverse_mismatch_count
            << ", net_instance_mismatch=" << result.net_instance_mismatch_count
            << ", duplicate_pin_ref=" << result.duplicate_pin_ref_count << ", floating_pin=" << result.floating_pin_count << std::endl;
  for (const auto& message : result.messages) {
    std::cout << "  " << message << std::endl;
  }

  return result.ok ? 1 : 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
CmdGenerateMPScript::CmdGenerateMPScript(const char* cmd_name) : TclCmd(cmd_name)
{
  auto* dir = new TclStringOption(TCL_DIRECTORY, 1);
  auto* name = new TclStringOption(TCL_NAME, 1, nullptr);
  auto* number = new TclIntOption("-number", 0);
  addOption(dir);
  addOption(name);
  addOption(number);
}

unsigned CmdGenerateMPScript::check()
{
  TclOption* dir = getOptionOrArg(TCL_DIRECTORY);
  LOG_FATAL_IF(!dir);

  TclOption* name = getOptionOrArg(TCL_NAME);
  LOG_FATAL_IF(!name);

  auto* number = new TclIntOption("-number", 0);
  LOG_FATAL_IF(!number);
  return 1;
}
/*
example script : aimp_random -dir ./result/mp -name ariane_macro_loc -number 100
*/
unsigned CmdGenerateMPScript::exec()
{
  if (!check()) {
    return 0;
  }

  TclOption* dir = getOptionOrArg(TCL_DIRECTORY);
  TclOption* name = getOptionOrArg(TCL_NAME);
  TclOption* number = getOptionOrArg("-number");
  auto str_dir = dir->getStringVal();
  auto str_name = name->getStringVal();
  auto int_number = number->getIntVal();
  if (str_dir != nullptr && str_name != nullptr) {
    dmInst->place_macro_generate_tcl(str_dir, str_name, int_number);
    return 1;
  }

  return 1;
}

}  // namespace tcl
