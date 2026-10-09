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
// MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
// See the Mulan PSL v2 for more details.
// ***************************************************************************************

#include "DefFlattener.hpp"

#include "DFSourceReader.hpp"
#include "IdbBlockages.h"
#include "IdbCellMaster.h"
#include "IdbDesign.h"
#include "IdbDie.h"
#include "IdbInstance.h"
#include "IdbLayer.h"
#include "IdbLayerShape.h"
#include "IdbLayout.h"
#include "IdbNet.h"
#include "IdbPins.h"
#include "IdbRegion.h"
#include "IdbRegularWire.h"
#include "IdbSpecialNet.h"
#include "IdbSpecialWire.h"
#include "IdbUnits.h"
#include "IdbViaMaster.h"
#include "IdbViaRule.h"
#include "IdbVias.h"
#include "idm.h"
#include "lef_service.h"
#include "defiPinCap.hpp"
#include "defiSite.hpp"
#include "defrReader.hpp"
#include "defzlib.hpp"

namespace imj {

// public

void DefFlattener::initInst()
{
  if (_df_instance == nullptr) {
    _df_instance = new DefFlattener();
  }
}

DefFlattener& DefFlattener::getInst()
{
  if (_df_instance == nullptr) {
    MJLOG.error(Loc::current(), "The instance not initialized!");
  }
  return *_df_instance;
}

void DefFlattener::destroyInst()
{
  if (_df_instance != nullptr) {
    delete _df_instance;
    _df_instance = nullptr;
  }
}

// function

void DefFlattener::flatten(std::map<std::string, std::any> config_map)
{
  Monitor monitor;
  MJLOG.info(Loc::current(), "Starting...");

  DFModel df_model;
  if (!buildDFModel(df_model, config_map)) {
    return;
  }
  if (!validateDFModel(df_model)) {
    return;
  }

  idb::IdbDesign* output_design = dmInst->get_idb_design();
  DFNetBinding root_net_binding;
  buildRootNetBinding(output_design, root_net_binding);

  std::vector<idb::IdbInstance*> top_instance_list = output_design->get_instance_list()->get_instance_list();
  int32_t flattened_instance_num = 0;
  for (idb::IdbInstance* top_instance : top_instance_list) {
    if (top_instance == nullptr || top_instance->get_cell_master() == nullptr) {
      continue;
    }
    if (!df_model.has_df_source(top_instance->get_cell_master()->get_name())) {
      continue;
    }
    flattenInstance(df_model, output_design, output_design, top_instance, "", DFTransform(), root_net_binding, true);
    flattened_instance_num++;
  }
  if (!connectSpecialPinList(df_model, output_design)) {
    return;
  }

  MJLOG.info(Loc::current(), "Flattened ", flattened_instance_num, " hierarchy instances");
  MJLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

bool DefFlattener::connectSpecialPinList(DFModel& df_model, idb::IdbDesign* output_design)
{
  std::map<std::string, std::vector<idb::IdbPin*>> output_special_net_name_to_pin_list_map;
  for (std::pair<const std::string, std::vector<idb::IdbPin*>>& source_pair : df_model.get_special_net_name_to_pin_list_map()) {
    std::string output_special_net_name = df_model.get_special_net_union().get_root_name(source_pair.first);
    std::vector<idb::IdbPin*>& output_pin_list = output_special_net_name_to_pin_list_map[output_special_net_name];
    output_pin_list.insert(output_pin_list.end(), source_pair.second.begin(), source_pair.second.end());
  }

  for (std::pair<const std::string, std::vector<idb::IdbPin*>>& output_pair : output_special_net_name_to_pin_list_map) {
    idb::IdbSpecialNet* output_special_net = output_design->get_special_net_list()->find_net(output_pair.first);
    if (output_special_net == nullptr || !output_design->connectPinsToSpecialNet(output_pair.second, output_special_net)) {
      MJLOG.error(Loc::current(), "Cannot connect flattened special net pins: ", output_pair.first);
      return false;
    }
  }
  return true;
}

#if 1  // build

bool DefFlattener::buildDFModel(DFModel& df_model, std::map<std::string, std::any>& config_map)
{
  if (!buildDFConfig(df_model, config_map)) {
    return false;
  }
  if (!buildDFSourceMap(df_model)) {
    return false;
  }
  if (!buildDFHierarchy(df_model)) {
    return false;
  }
  return true;
}

bool DefFlattener::buildDFConfig(DFModel& df_model, std::map<std::string, std::any>& config_map)
{
  std::map<std::string, std::any>::iterator config_iter = config_map.find("-hierarchy");
  if (config_iter == config_map.end()) {
    MJLOG.error(Loc::current(), "The -hierarchy option is required.");
    return false;
  }
  std::string* hierarchy_def_path_list_ptr = std::any_cast<std::string>(&config_iter->second);
  if (hierarchy_def_path_list_ptr == nullptr || hierarchy_def_path_list_ptr->empty()) {
    MJLOG.error(Loc::current(), "The -hierarchy option is empty.");
    return false;
  }

  DFConfig df_config;
  std::istringstream hierarchy_def_path_stream(*hierarchy_def_path_list_ptr);
  std::string hierarchy_def_path;
  while (hierarchy_def_path_stream >> hierarchy_def_path) {
    std::filesystem::path def_path = std::filesystem::absolute(hierarchy_def_path).lexically_normal();
    if (!std::filesystem::exists(def_path)) {
      MJLOG.error(Loc::current(), "Cannot find hierarchy DEF file: ", def_path.string());
      return false;
    }
    df_config.get_hierarchy_def_path_list().push_back(def_path.string());
  }
  if (df_config.get_hierarchy_def_path_list().empty()) {
    MJLOG.error(Loc::current(), "The -hierarchy option must contain at least one DEF path.");
    return false;
  }

  config_iter = config_map.find("-pg_connect");
  if (config_iter != config_map.end()) {
    std::string* pg_connect_list_string_ptr = std::any_cast<std::string>(&config_iter->second);
    if (pg_connect_list_string_ptr == nullptr || !buildDFPGConnectList(df_config, *pg_connect_list_string_ptr)) {
      return false;
    }
  }

  df_model.set_df_config(df_config);
  return true;
}

bool DefFlattener::buildDFPGConnectList(DFConfig& df_config, std::string pg_connect_list_string)
{
  std::map<std::string, std::string> child_net_name_to_top_net_name_map;
  int32_t string_size = static_cast<int32_t>(pg_connect_list_string.size());
  int32_t begin_idx = 0;
  int32_t end_idx = string_size - 1;
  while (begin_idx < string_size && std::isspace(static_cast<unsigned char>(pg_connect_list_string[begin_idx]))) {
    begin_idx++;
  }
  while (end_idx >= begin_idx && std::isspace(static_cast<unsigned char>(pg_connect_list_string[end_idx]))) {
    end_idx--;
  }
  if (begin_idx > end_idx) {
    return true;
  }
  if (pg_connect_list_string[begin_idx] == '{') {
    int32_t brace_depth = 0;
    int32_t outer_end_idx = -1;
    for (int32_t idx = begin_idx; idx <= end_idx; idx++) {
      if (pg_connect_list_string[idx] == '{') {
        brace_depth++;
      } else if (pg_connect_list_string[idx] == '}') {
        brace_depth--;
        if (brace_depth == 0) {
          outer_end_idx = idx;
          break;
        }
      }
      if (brace_depth < 0) {
        MJLOG.error(Loc::current(), "The -pg_connect option has unmatched braces.");
        return false;
      }
    }
    if (brace_depth != 0) {
      MJLOG.error(Loc::current(), "The -pg_connect option has unmatched braces.");
      return false;
    }
    if (outer_end_idx == end_idx) {
      pg_connect_list_string = pg_connect_list_string.substr(begin_idx + 1, end_idx - begin_idx - 1);
      string_size = static_cast<int32_t>(pg_connect_list_string.size());
    }
  }

  int32_t string_idx = 0;
  while (string_idx < string_size) {
    while (string_idx < string_size && std::isspace(static_cast<unsigned char>(pg_connect_list_string[string_idx]))) {
      string_idx++;
    }
    if (string_idx == string_size) {
      break;
    }

    std::string pg_connect_string;
    if (pg_connect_list_string[string_idx] == '{') {
      int32_t begin_idx = ++string_idx;
      int32_t brace_depth = 1;
      while (string_idx < string_size && brace_depth > 0) {
        if (pg_connect_list_string[string_idx] == '{') {
          brace_depth++;
        } else if (pg_connect_list_string[string_idx] == '}') {
          brace_depth--;
        }
        string_idx++;
      }
      if (brace_depth != 0) {
        MJLOG.error(Loc::current(), "The -pg_connect option has unmatched braces.");
        return false;
      }
      pg_connect_string = pg_connect_list_string.substr(begin_idx, string_idx - begin_idx - 1);
    } else {
      pg_connect_string = pg_connect_list_string.substr(string_idx);
      string_idx = string_size;
    }

    std::istringstream pg_connect_stream(pg_connect_string);
    std::vector<std::string> pg_net_name_list;
    std::string pg_net_name;
    while (pg_connect_stream >> pg_net_name) {
      if (pg_net_name.find('{') != std::string::npos || pg_net_name.find('}') != std::string::npos) {
        MJLOG.error(Loc::current(), "The -pg_connect option has invalid nested braces.");
        return false;
      }
      pg_net_name_list.push_back(pg_net_name);
    }
    if (pg_net_name_list.size() < 2) {
      MJLOG.error(Loc::current(), "Each -pg_connect group must contain one top net and at least one child net.");
      return false;
    }

    DFPGConnect pg_connect;
    pg_connect.set_top_net_name(pg_net_name_list.front());
    for (int32_t pg_net_idx = 1; pg_net_idx < static_cast<int32_t>(pg_net_name_list.size()); pg_net_idx++) {
      std::string& child_net_name = pg_net_name_list[pg_net_idx];
      std::map<std::string, std::string>::iterator child_net_iter = child_net_name_to_top_net_name_map.find(child_net_name);
      if (child_net_iter != child_net_name_to_top_net_name_map.end() && child_net_iter->second != pg_connect.get_top_net_name()) {
        MJLOG.error(Loc::current(), "The child PG net maps to multiple top nets: ", child_net_name);
        return false;
      }
      child_net_name_to_top_net_name_map[child_net_name] = pg_connect.get_top_net_name();
      pg_connect.get_child_net_name_list().push_back(child_net_name);
    }
    df_config.get_pg_connect_list().push_back(pg_connect);
  }
  return true;
}

bool DefFlattener::buildDFSourceMap(DFModel& df_model)
{
  idb::IdbLefService* idb_lef_service = dmInst->get_idb_lef_service();
  if (idb_lef_service == nullptr) {
    MJLOG.error(Loc::current(), "The active LEF service is null.");
    return false;
  }
  std::vector<std::string> lef_file_list = idb_lef_service->get_lef_files();
  if (lef_file_list.empty()) {
    MJLOG.error(Loc::current(), "The active LEF file list is empty.");
    return false;
  }

  DFSourceReader df_source_reader;
  std::vector<std::string>& hierarchy_def_path_list = df_model.get_df_config().get_hierarchy_def_path_list();
  for (std::string& hierarchy_def_path : hierarchy_def_path_list) {
    DFSource df_source;
    df_source.set_def_path(hierarchy_def_path);
    if (!df_source_reader.read(df_source)) {
      MJLOG.error(Loc::current(), "Cannot read hierarchy DEF metadata: ", hierarchy_def_path);
      return false;
    }
    if (df_source.get_master_name().empty() || !df_source.get_die_area().is_valid()) {
      MJLOG.error(Loc::current(), "The hierarchy DEF data is incomplete: ", hierarchy_def_path);
      return false;
    }
    if (df_model.has_df_source(df_source.get_master_name())) {
      MJLOG.error(Loc::current(), "Multiple hierarchy DEF files use DESIGN ", df_source.get_master_name(), ".");
      return false;
    }
    df_model.get_child_master_to_df_source_map().emplace(df_source.get_master_name(), std::move(df_source));
  }

  if (!buildDFSourceMasterList(df_model, dmInst->get_idb_layout())) {
    return false;
  }

  bool is_root_design_loaded = false;
  idb::IdbDesign* root_design = dmInst->get_idb_design();
  if (root_design != nullptr && root_design->get_instance_list() != nullptr) {
    for (idb::IdbInstance* root_instance : root_design->get_instance_list()->get_instance_list()) {
      if (root_instance != nullptr && root_instance->get_cell_master() != nullptr
          && df_model.has_df_source(root_instance->get_cell_master()->get_name())) {
        is_root_design_loaded = true;
        break;
      }
    }
  }
  if (!is_root_design_loaded) {
    std::string root_def_path = dmInst->get_config().get_def_path();
    idb::IdbDefService* root_def_service = dmInst->get_idb_def_service();
    if (root_def_path.empty() && root_def_service != nullptr) {
      root_def_path = root_def_service->get_def_file();
    }
    if (root_def_path.empty() || !dmInst->readDef(root_def_path)) {
      MJLOG.error(Loc::current(), "Cannot read top DEF file: ", root_def_path);
      return false;
    }
  }

  for (std::pair<const std::string, DFSource>& source_pair : df_model.get_child_master_to_df_source_map()) {
    DFSource& df_source = source_pair.second;
    std::unique_ptr<idb::IdbBuilder> idb_builder = std::make_unique<idb::IdbBuilder>();
    std::vector<std::string> child_lef_file_list = lef_file_list;
    idb::IdbLefService* child_lef_service = idb_builder->buildLef(child_lef_file_list);
    if (child_lef_service == nullptr || !buildDFSourceMasterList(df_model, child_lef_service->get_layout())
        || idb_builder->buildDef(df_source.get_def_path()) == nullptr) {
      MJLOG.error(Loc::current(), "Cannot read child DEF file: ", df_source.get_def_path());
      return false;
    }

    idb::IdbDefService* child_def_service = idb_builder->get_def_service();
    idb::IdbDesign* child_design = child_def_service == nullptr ? nullptr : child_def_service->get_design();
    idb::IdbLayout* child_layout = child_def_service == nullptr ? nullptr : child_def_service->get_layout();
    if (child_design == nullptr || child_layout == nullptr || child_layout->get_die() == nullptr
        || child_design->get_design_name() != df_source.get_master_name()) {
      MJLOG.error(Loc::current(), "The child DEF data is incomplete: ", df_source.get_def_path());
      return false;
    }
    child_design->materializeAllSpecialNetWildcardPins();
    child_layout->get_die()->set_bounding_box();
    if (child_layout->get_die()->get_llx() != df_source.get_die_area().get_ll_x()
        || child_layout->get_die()->get_lly() != df_source.get_die_area().get_ll_y()
        || child_layout->get_die()->get_urx() != df_source.get_die_area().get_ur_x()
        || child_layout->get_die()->get_ury() != df_source.get_die_area().get_ur_y()) {
      MJLOG.error(Loc::current(), "The child DEF DIEAREA changed while reading: ", df_source.get_def_path());
      return false;
    }
    df_source.set_idb_builder(std::move(idb_builder));
  }

  if (!buildDFSourceViaList(df_model)) {
    return false;
  }
  return true;
}

bool DefFlattener::buildDFSourceMasterList(DFModel& df_model, idb::IdbLayout* layout)
{
  if (layout == nullptr || layout->get_cell_master_list() == nullptr) {
    MJLOG.error(Loc::current(), "Cannot build hierarchy masters without LEF layout data.");
    return false;
  }

  for (std::pair<const std::string, DFSource>& source_pair : df_model.get_child_master_to_df_source_map()) {
    DFSource& df_source = source_pair.second;
    idb::IdbCellMaster* cell_master = layout->get_cell_master_list()->find_cell_master(df_source.get_master_name());
    if (cell_master != nullptr) {
      continue;
    }
    cell_master = layout->get_cell_master_list()->set_cell_master(df_source.get_master_name());
    if (cell_master == nullptr) {
      MJLOG.error(Loc::current(), "Cannot create hierarchy master: ", df_source.get_master_name());
      return false;
    }
    cell_master->set_type(idb::CellMasterType::kBlock);
    cell_master->set_origin_x(0);
    cell_master->set_origin_y(0);
    cell_master->set_width(static_cast<uint32_t>(df_source.get_die_area().get_width()));
    cell_master->set_height(static_cast<uint32_t>(df_source.get_die_area().get_height()));
    for (std::string& pin_name : df_source.get_pin_name_list()) {
      if (cell_master->add_term(pin_name) == nullptr) {
        MJLOG.error(Loc::current(), "Cannot create hierarchy master pin: ", df_source.get_master_name(), "/", pin_name);
        return false;
      }
    }
  }
  return true;
}

bool DefFlattener::buildDFSourceViaList(DFModel& df_model)
{
  for (std::pair<const std::string, DFSource>& source_pair : df_model.get_child_master_to_df_source_map()) {
    idb::IdbDesign* source_design = source_pair.second.get_design();
    if (source_design == nullptr || source_design->get_via_list() == nullptr) {
      MJLOG.error(Loc::current(), "The child DEF via data is incomplete: ", source_pair.first);
      return false;
    }
    for (idb::IdbVia* source_via : source_design->get_via_list()->get_via_list()) {
      if (!buildDFVia(source_via)) {
        return false;
      }
    }
  }
  return true;
}

bool DefFlattener::buildDFVia(idb::IdbVia* source_via)
{
  idb::IdbDesign* output_design = dmInst->get_idb_design();
  if (source_via == nullptr || output_design == nullptr || output_design->get_via_list() == nullptr || source_via->get_name().empty()) {
    MJLOG.error(Loc::current(), "Cannot build an invalid child via.");
    return false;
  }
  if (output_design->get_via_list()->find_via(source_via->get_name()) != nullptr
      || output_design->get_layout()->get_via_list()->find_via(source_via->get_name()) != nullptr) {
    return true;
  }

  idb::IdbViaMaster* source_via_master = source_via->get_instance();
  if (source_via_master == nullptr) {
    MJLOG.error(Loc::current(), "Cannot find child via master: ", source_via->get_name());
    return false;
  }
  idb::IdbVia* output_via = output_design->get_via_list()->add_via(source_via->get_name());
  idb::IdbViaMaster* output_via_master = source_via_master->clone();
  output_via->set_instance(output_via_master);
  output_via->set_coordinate(source_via->get_coordinate());
  return buildDFViaMaster(source_via_master, output_via_master);
}

bool DefFlattener::buildDFViaMaster(idb::IdbViaMaster* source_via_master, idb::IdbViaMaster* output_via_master)
{
  idb::IdbLayout* output_layout = dmInst->get_idb_layout();
  if (source_via_master == nullptr || output_via_master == nullptr || output_layout == nullptr || output_layout->get_layers() == nullptr
      || output_layout->get_via_rule_list() == nullptr) {
    MJLOG.error(Loc::current(), "Cannot build an incomplete child via master.");
    return false;
  }

  if (source_via_master->is_generate()) {
    idb::IdbViaMasterGenerate* source_generate = source_via_master->get_master_generate();
    idb::IdbViaMasterGenerate* output_generate = output_via_master->get_master_generate();
    if (source_generate == nullptr || output_generate == nullptr || source_generate->get_layer_bottom() == nullptr
        || source_generate->get_layer_cut() == nullptr || source_generate->get_layer_top() == nullptr) {
      MJLOG.error(Loc::current(), "The child generated via data is incomplete: ", source_via_master->get_name());
      return false;
    }
    idb::IdbLayerRouting* output_layer_bottom
        = dynamic_cast<idb::IdbLayerRouting*>(output_layout->get_layers()->find_layer(source_generate->get_layer_bottom()->get_name()));
    idb::IdbLayerCut* output_layer_cut
        = dynamic_cast<idb::IdbLayerCut*>(output_layout->get_layers()->find_layer(source_generate->get_layer_cut()->get_name()));
    idb::IdbLayerRouting* output_layer_top
        = dynamic_cast<idb::IdbLayerRouting*>(output_layout->get_layers()->find_layer(source_generate->get_layer_top()->get_name()));
    idb::IdbViaRuleGenerate* output_via_rule
        = output_layout->get_via_rule_list()->find_via_rule_generate(source_generate->get_rule_name());
    if (output_layer_bottom == nullptr || output_layer_cut == nullptr || output_layer_top == nullptr || output_via_rule == nullptr) {
      MJLOG.error(Loc::current(), "The child generated via uses data missing from the active LEF: ", source_via_master->get_name());
      return false;
    }
    output_generate->set_rule_generate(output_via_rule);
    output_generate->set_layer_bottom(output_layer_bottom);
    output_generate->set_layer_cut(output_layer_cut);
    output_generate->set_layer_top(output_layer_top);
  } else if (source_via_master->is_fix()) {
    for (idb::IdbViaMasterFixed* output_fixed : output_via_master->get_master_fixed_list()) {
      if (output_fixed == nullptr || output_fixed->get_layer() == nullptr) {
        MJLOG.error(Loc::current(), "The child fixed via data is incomplete: ", source_via_master->get_name());
        return false;
      }
      idb::IdbLayer* output_layer = output_layout->get_layers()->find_layer(output_fixed->get_layer()->get_name());
      if (output_layer == nullptr) {
        MJLOG.error(Loc::current(), "The child fixed via uses a layer missing from the active LEF: ", source_via_master->get_name());
        return false;
      }
      output_fixed->set_layer(output_layer);
    }
  } else {
    MJLOG.error(Loc::current(), "The child via type is invalid: ", source_via_master->get_name());
    return false;
  }

  output_via_master->set_via_shape();
  return true;
}

bool DFSourceReader::read(DFSource& df_source)
{
  _df_source = &df_source;
  _df_source->set_master_name("");
  _df_source->set_die_area(DFDieArea());
  _df_source->clear_pin_name_list();
  bool is_success = _df_source->get_def_path().ends_with(".gz") ? readGzipDef() : readDef();
  _df_source = nullptr;
  return is_success;
}

bool DFSourceReader::readDef()
{
  FILE* file = std::fopen(_df_source->get_def_path().c_str(), "r");
  if (file == nullptr) {
    return false;
  }

  defrInit();
  defrReset();
  defrInitSession();
  defrSetDesignCbk(readDesign);
  defrSetDieAreaCbk(readDieArea);
  defrSetPinCbk(readPin);
  int32_t result = defrRead(file, _df_source->get_def_path().c_str(), static_cast<defiUserData>(this), 1);
  defrUnsetCallbacks();
  defrClear();
  std::fclose(file);
  return result == 0;
}

bool DFSourceReader::readGzipDef()
{
  defGZFile file = defrGZipOpen(_df_source->get_def_path().c_str(), "r");
  if (file == nullptr) {
    return false;
  }

  defrInit();
  defrReset();
  defrInitSession();
  defrSetGZipReadFunction();
  defrSetDesignCbk(readDesign);
  defrSetDieAreaCbk(readDieArea);
  defrSetPinCbk(readPin);
  int32_t result = defrReadGZip(file, _df_source->get_def_path().c_str(), static_cast<defiUserData>(this));
  defrUnsetCallbacks();
  defrClear();
  defrGZipClose(file);
  return result == 0;
}

int32_t DFSourceReader::readDesign(defrCallbackType_e, const char* design_name, defiUserData data)
{
  DFSourceReader* df_source_reader = static_cast<DFSourceReader*>(data);
  if (df_source_reader == nullptr || df_source_reader->_df_source == nullptr || design_name == nullptr) {
    return 1;
  }
  std::string master_name = design_name;
  std::erase(master_name, '\\');
  df_source_reader->_df_source->set_master_name(master_name);
  return 0;
}

int32_t DFSourceReader::readDieArea(defrCallbackType_e, defiBox* die_area, defiUserData data)
{
  DFSourceReader* df_source_reader = static_cast<DFSourceReader*>(data);
  if (df_source_reader == nullptr || df_source_reader->_df_source == nullptr || die_area == nullptr) {
    return 1;
  }
  defiPoints point_list = die_area->getPoint();
  if (point_list.numPoints <= 0) {
    return 1;
  }
  int32_t ll_x = INT32_MAX;
  int32_t ll_y = INT32_MAX;
  int32_t ur_x = INT32_MIN;
  int32_t ur_y = INT32_MIN;
  for (int32_t point_idx = 0; point_idx < point_list.numPoints; point_idx++) {
    ll_x = std::min(ll_x, point_list.x[point_idx]);
    ll_y = std::min(ll_y, point_list.y[point_idx]);
    ur_x = std::max(ur_x, point_list.x[point_idx]);
    ur_y = std::max(ur_y, point_list.y[point_idx]);
  }
  df_source_reader->_df_source->set_die_area(DFDieArea(ll_x, ll_y, ur_x, ur_y));
  return 0;
}

int32_t DFSourceReader::readPin(defrCallbackType_e, defiPin* pin, defiUserData data)
{
  DFSourceReader* df_source_reader = static_cast<DFSourceReader*>(data);
  if (df_source_reader == nullptr || df_source_reader->_df_source == nullptr || pin == nullptr || pin->pinName() == nullptr) {
    return 1;
  }
  std::string pin_name = pin->pinName();
  std::erase(pin_name, '\\');
  std::vector<std::string>& pin_name_list = df_source_reader->_df_source->get_pin_name_list();
  if (std::find(pin_name_list.begin(), pin_name_list.end(), pin_name) != pin_name_list.end()) {
    return 1;
  }
  df_source_reader->_df_source->add_pin_name(pin_name);
  return 0;
}

bool DefFlattener::buildDFHierarchy(DFModel& df_model)
{
  idb::IdbDesign* root_design = dmInst->get_idb_design();
  if (root_design == nullptr || root_design->get_design_name().empty()) {
    MJLOG.error(Loc::current(), "The active top DEF design is incomplete.");
    return false;
  }
  if (df_model.has_df_source(root_design->get_design_name())) {
    MJLOG.error(Loc::current(), "The top DEF DESIGN cannot also be a hierarchy child: ", root_design->get_design_name());
    return false;
  }

  DFHierarchy& df_hierarchy = df_model.get_df_hierarchy();
  df_hierarchy.set_root_master_name(root_design->get_design_name());
  std::vector<std::string> master_name_stack;
  std::set<std::string> visited_master_name_set;
  if (!buildDFHierarchyNode(df_model, root_design, df_hierarchy.get_root_master_name(), master_name_stack,
                            visited_master_name_set)) {
    return false;
  }
  for (std::pair<const std::string, DFSource>& source_pair : df_model.get_child_master_to_df_source_map()) {
    if (visited_master_name_set.find(source_pair.first) == visited_master_name_set.end()) {
      MJLOG.error(Loc::current(), "The hierarchy DEF is not reachable from the top DEF: ", source_pair.first);
      return false;
    }
  }
  return true;
}

bool DefFlattener::buildDFHierarchyNode(DFModel& df_model, idb::IdbDesign* parent_design, std::string parent_master_name,
                                        std::vector<std::string>& master_name_stack,
                                        std::set<std::string>& visited_master_name_set)
{
  if (parent_design == nullptr || parent_design->get_instance_list() == nullptr) {
    MJLOG.error(Loc::current(), "The hierarchy parent design is incomplete: ", parent_master_name);
    return false;
  }
  for (idb::IdbInstance* source_instance : parent_design->get_instance_list()->get_instance_list()) {
    if (source_instance == nullptr || source_instance->get_cell_master() == nullptr) {
      continue;
    }
    std::string child_master_name = source_instance->get_cell_master()->get_name();
    DFHierarchy& df_hierarchy = df_model.get_df_hierarchy();
    if (child_master_name == df_hierarchy.get_root_master_name()) {
      MJLOG.error(Loc::current(), "Hierarchy cycle detected at top master: ", child_master_name);
      return false;
    }
    if (!df_model.has_df_source(child_master_name)) {
      continue;
    }
    if (std::find(master_name_stack.begin(), master_name_stack.end(), child_master_name) != master_name_stack.end()) {
      MJLOG.error(Loc::current(), "Hierarchy cycle detected at master: ", child_master_name);
      return false;
    }

    if (!df_hierarchy.add_parent_master_name(child_master_name, parent_master_name)) {
      MJLOG.error(Loc::current(), "Hierarchy graph detected: master ", child_master_name, " has parents ",
                  df_hierarchy.get_parent_master_name(child_master_name), " and ", parent_master_name, ".");
      return false;
    }
    if (visited_master_name_set.find(child_master_name) != visited_master_name_set.end()) {
      continue;
    }

    DFSource* child_source = df_model.get_df_source(child_master_name);
    if (child_source == nullptr || child_source->get_design() == nullptr) {
      MJLOG.error(Loc::current(), "Cannot find the hierarchy child DEF source: ", child_master_name);
      return false;
    }
    master_name_stack.push_back(child_master_name);
    if (!buildDFHierarchyNode(df_model, child_source->get_design(), child_master_name, master_name_stack,
                              visited_master_name_set)) {
      return false;
    }
    master_name_stack.pop_back();
    visited_master_name_set.insert(child_master_name);
    df_hierarchy.add_bottom_up_master_name(child_master_name);
  }
  return true;
}

#endif

#if 1  // check

bool DefFlattener::validateDFModel(DFModel& df_model)
{
  idb::IdbDesign* output_design = dmInst->get_idb_design();
  idb::IdbLayout* output_layout = dmInst->get_idb_layout();
  if (output_design == nullptr || output_layout == nullptr || output_layout->get_cell_master_list() == nullptr) {
    MJLOG.error(Loc::current(), "The active IDB design data is incomplete.");
    return false;
  }
  if (!validateDFPGConnectList(df_model)) {
    return false;
  }

  std::vector<std::string>& bottom_up_master_name_list = df_model.get_df_hierarchy().get_bottom_up_master_name_list();
  for (std::string& master_name : bottom_up_master_name_list) {
    if (!validateDFSource(df_model, master_name)) {
      return false;
    }
    DFSource* df_source = df_model.get_df_source(master_name);
    for (idb::IdbInstance* source_instance : df_source->get_design()->get_instance_list()->get_instance_list()) {
      if (source_instance == nullptr || source_instance->get_cell_master() == nullptr
          || !df_model.has_df_source(source_instance->get_cell_master()->get_name())) {
        continue;
      }
      if (!validateDFInstance(df_model, df_source->get_design(), source_instance)) {
        return false;
      }
    }
  }
  for (idb::IdbInstance* output_instance : output_design->get_instance_list()->get_instance_list()) {
    if (output_instance == nullptr || output_instance->get_cell_master() == nullptr) {
      continue;
    }
    if (!df_model.has_df_source(output_instance->get_cell_master()->get_name())) {
      continue;
    }
    if (!validateDFInstance(df_model, output_design, output_instance)) {
      return false;
    }
  }
  return true;
}

bool DefFlattener::validateDFPGConnectList(DFModel& df_model)
{
  idb::IdbDesign* output_design = dmInst->get_idb_design();
  if (output_design == nullptr || output_design->get_special_net_list() == nullptr) {
    MJLOG.error(Loc::current(), "Cannot validate PG connections without top special nets.");
    return false;
  }
  for (DFPGConnect& pg_connect : df_model.get_df_config().get_pg_connect_list()) {
    if (output_design->get_special_net_list()->find_net(pg_connect.get_top_net_name()) == nullptr) {
      MJLOG.error(Loc::current(), "Cannot find the top PG net: ", pg_connect.get_top_net_name());
      return false;
    }
  }
  return true;
}

bool DefFlattener::validateDFSource(DFModel& df_model, std::string master_name)
{
  DFSource* df_source = df_model.get_df_source(master_name);
  idb::IdbDesign* output_design = dmInst->get_idb_design();
  idb::IdbLayout* output_layout = dmInst->get_idb_layout();
  if (df_source == nullptr || output_design == nullptr || output_layout == nullptr) {
    MJLOG.error(Loc::current(), "Cannot validate an incomplete DEF source.");
    return false;
  }
  idb::IdbDesign* child_design = df_source->get_design();
  if (child_design == nullptr || child_design->get_units() == nullptr || output_design->get_units() == nullptr) {
    MJLOG.error(Loc::current(), "The child DEF design data is incomplete: ", master_name);
    return false;
  }
  if (child_design->get_design_name() != master_name) {
    MJLOG.error(Loc::current(), "Child DEF DESIGN does not match master ", master_name, ": ", child_design->get_design_name());
    return false;
  }
  if (child_design->get_units()->get_micron_dbu() != output_design->get_units()->get_micron_dbu()) {
    MJLOG.error(Loc::current(), "Child DEF DBU does not match the active design: ", master_name);
    return false;
  }
  idb::IdbCellMaster* output_master = output_layout->get_cell_master_list()->find_cell_master(master_name);
  if (output_master == nullptr) {
    MJLOG.error(Loc::current(), "Cannot find child master in the active LEF data: ", master_name);
    return false;
  }
  if (!df_source->get_die_area().is_valid()) {
    MJLOG.error(Loc::current(), "The child DEF DIEAREA is invalid: ", master_name);
    return false;
  }
  if (df_source->get_die_area().get_width() != static_cast<int32_t>(output_master->get_width())
      || df_source->get_die_area().get_height() != static_cast<int32_t>(output_master->get_height())) {
    MJLOG.error(Loc::current(), "Child DEF DIEAREA does not match the LEF master size: ", master_name);
    return false;
  }
  for (std::string& pin_name : df_source->get_pin_name_list()) {
    if (output_master->findTerm(pin_name) == nullptr) {
      MJLOG.error(Loc::current(), "Child DEF pin does not match the master: ", master_name, "/", pin_name);
      return false;
    }
  }
  return validateDFDesignData(df_model, child_design);
}

bool DefFlattener::validateDFDesignData(DFModel&, idb::IdbDesign* source_design)
{
  idb::IdbLayout* output_layout = dmInst->get_idb_layout();
  if (source_design == nullptr || output_layout == nullptr || output_layout->get_cell_master_list() == nullptr || output_layout->get_layers() == nullptr) {
    MJLOG.error(Loc::current(), "Cannot validate incomplete child DEF data.");
    return false;
  }
  for (idb::IdbInstance* source_instance : source_design->get_instance_list()->get_instance_list()) {
    if (source_instance == nullptr || source_instance->get_cell_master() == nullptr
        || output_layout->get_cell_master_list()->find_cell_master(source_instance->get_cell_master()->get_name()) == nullptr) {
      MJLOG.error(Loc::current(), "A child instance master cannot be found in the active LEF data.");
      return false;
    }
  }
  for (idb::IdbPin* source_pin : source_design->get_io_pin_list()->get_pin_list()) {
    if (source_pin == nullptr) {
      continue;
    }
    for (idb::IdbLayerShape* layer_shape : source_pin->get_port_box_list()) {
      if (layer_shape == nullptr || layer_shape->get_layer() == nullptr
          || output_layout->get_layers()->find_layer(layer_shape->get_layer()->get_name()) == nullptr) {
        MJLOG.error(Loc::current(), "A child boundary pin uses a layer missing from the active LEF data.");
        return false;
      }
    }
    for (idb::IdbVia* source_via : source_pin->get_via_list()) {
      if (!validateDFVia(source_via)) {
        return false;
      }
    }
  }
  for (idb::IdbNet* source_net : source_design->get_net_list()->get_net_list()) {
    if (source_net == nullptr) {
      continue;
    }
    for (idb::IdbRegularWire* source_wire : source_net->get_wire_list()->get_wire_list()) {
      if (!validateDFRegularWire(source_wire)) {
        return false;
      }
    }
  }
  for (idb::IdbSpecialNet* source_net : source_design->get_special_net_list()->get_net_list()) {
    if (source_net == nullptr) {
      continue;
    }
    for (idb::IdbSpecialWire* source_wire : source_net->get_wire_list()->get_wire_list()) {
      if (!validateDFSpecialWire(source_wire)) {
        return false;
      }
    }
  }
  for (idb::IdbBlockage* source_blockage : source_design->get_blockage_list()->get_blockage_list()) {
    if (source_blockage == nullptr || !source_blockage->is_routing_blockage()) {
      continue;
    }
    idb::IdbRoutingBlockage* routing_blockage = static_cast<idb::IdbRoutingBlockage*>(source_blockage);
    if (output_layout->get_layers()->find_layer(routing_blockage->get_layer_name()) == nullptr) {
      MJLOG.error(Loc::current(), "A child routing blockage uses a layer missing from the active LEF data.");
      return false;
    }
  }
  return true;
}

bool DefFlattener::validateDFInstance(DFModel& df_model, idb::IdbDesign* source_design, idb::IdbInstance* source_instance)
{
  if (source_design == nullptr || source_instance == nullptr || source_instance->get_cell_master() == nullptr) {
    MJLOG.error(Loc::current(), "Cannot validate an incomplete hierarchy instance.");
    return false;
  }
  std::string child_master_name = source_instance->get_cell_master()->get_name();
  DFSource* child_source = df_model.get_df_source(child_master_name);
  if (child_source == nullptr || child_source->get_design() == nullptr) {
    MJLOG.error(Loc::current(), "Cannot find child DEF source for instance: ", source_instance->get_name());
    return false;
  }
  if (!source_instance->is_fixed()) {
    MJLOG.error(Loc::current(), "A hierarchy instance must be FIXED before flattening: ", source_instance->get_name());
    return false;
  }

  for (idb::IdbPin* parent_pin : source_instance->get_pin_list()->get_pin_list()) {
    if (parent_pin == nullptr) {
      continue;
    }
    std::string boundary_pin_name = parent_pin->get_term() == nullptr ? parent_pin->get_pin_name() : parent_pin->get_term_name();
    idb::IdbPin* child_pin = child_source->get_design()->get_io_pin_list()->find_pin(boundary_pin_name);
    if (child_pin == nullptr) {
      MJLOG.error(Loc::current(), "Cannot find child boundary pin ", boundary_pin_name, " for instance ", source_instance->get_name());
      return false;
    }

    idb::IdbNet* parent_regular_net = parent_pin->get_net();
    idb::IdbSpecialNet* parent_special_net = getSpecialNet(source_design, parent_pin);
    idb::IdbNet* child_regular_net = child_pin->get_net();
    idb::IdbSpecialNet* child_special_net = getSpecialNet(child_source->get_design(), child_pin);
    if (parent_special_net != nullptr) {
      parent_regular_net = nullptr;
    }
    if (child_special_net != nullptr) {
      child_regular_net = nullptr;
    }
    std::string parent_top_pg_net_name;
    if (parent_regular_net != nullptr) {
      parent_top_pg_net_name = df_model.get_df_config().get_top_pg_net_name(parent_regular_net->get_net_name());
    }
    std::string child_top_pg_net_name;
    if (child_regular_net != nullptr) {
      child_top_pg_net_name = df_model.get_df_config().get_top_pg_net_name(child_regular_net->get_net_name());
    }
    std::string parent_special_output_net_name;
    if (parent_special_net != nullptr) {
      parent_special_output_net_name = df_model.get_df_config().get_top_pg_net_name(parent_special_net->get_net_name());
      if (parent_special_output_net_name.empty()) {
        parent_special_output_net_name = parent_special_net->get_net_name();
      }
    }
    idb::IdbSpecialNet* related_parent_special_net = getRelatedSpecialNet(source_design, parent_regular_net);
    if (parent_regular_net != nullptr && child_regular_net == nullptr && child_special_net == nullptr) {
      MJLOG.error(Loc::current(), "A regular parent net has no matching child regular net: ", boundary_pin_name);
      return false;
    }
    if (parent_special_net != nullptr && child_special_net == nullptr && child_top_pg_net_name.empty()) {
      MJLOG.error(Loc::current(), "A special parent net has no matching child special net: ", boundary_pin_name);
      return false;
    }
    if (parent_regular_net != nullptr && child_special_net != nullptr && related_parent_special_net == nullptr
        && parent_top_pg_net_name.empty()) {
      MJLOG.error(Loc::current(), "A regular hierarchy net has no matching parent special net: ", boundary_pin_name);
      return false;
    }
    if (parent_special_net != nullptr && child_regular_net != nullptr
        && child_top_pg_net_name != parent_special_output_net_name) {
      MJLOG.error(Loc::current(), "Special and regular hierarchy nets cannot be merged at pin: ", boundary_pin_name);
      return false;
    }
    if (parent_regular_net != nullptr && child_regular_net != nullptr && !parent_top_pg_net_name.empty()
        && !child_top_pg_net_name.empty() && parent_top_pg_net_name != child_top_pg_net_name) {
      MJLOG.error(Loc::current(), "PG hierarchy nets map to different top nets: ", boundary_pin_name);
      return false;
    }
  }
  return true;
}

bool DefFlattener::validateDFRegularWire(idb::IdbRegularWire* source_wire)
{
  idb::IdbLayout* output_layout = dmInst->get_idb_layout();
  if (source_wire == nullptr || output_layout == nullptr || output_layout->get_layers() == nullptr) {
    MJLOG.error(Loc::current(), "Cannot validate an incomplete child regular wire.");
    return false;
  }
  for (idb::IdbRegularWireSegment* source_segment : source_wire->get_segment_list()) {
    if (source_segment == nullptr || source_segment->get_layer() == nullptr
        || output_layout->get_layers()->find_layer(source_segment->get_layer()->get_name()) == nullptr) {
      MJLOG.error(Loc::current(), "A child regular wire uses a layer missing from the active LEF data.");
      return false;
    }
    for (idb::IdbVia* source_via : source_segment->get_via_list()) {
      if (!validateDFVia(source_via)) {
        return false;
      }
    }
  }
  return true;
}

bool DefFlattener::validateDFSpecialWire(idb::IdbSpecialWire* source_wire)
{
  idb::IdbLayout* output_layout = dmInst->get_idb_layout();
  if (source_wire == nullptr || output_layout == nullptr || output_layout->get_layers() == nullptr) {
    MJLOG.error(Loc::current(), "Cannot validate an incomplete child special wire.");
    return false;
  }
  for (idb::IdbSpecialWireSegment* source_segment : source_wire->get_segment_list()) {
    if (source_segment == nullptr || source_segment->get_layer() == nullptr
        || output_layout->get_layers()->find_layer(source_segment->get_layer()->get_name()) == nullptr) {
      MJLOG.error(Loc::current(), "A child special wire uses a layer missing from the active LEF data.");
      return false;
    }
    if (source_segment->is_via() && !validateDFVia(source_segment->get_via())) {
      return false;
    }
  }
  return true;
}

bool DefFlattener::validateDFVia(idb::IdbVia* source_via)
{
  idb::IdbDesign* output_design = dmInst->get_idb_design();
  idb::IdbLayout* output_layout = dmInst->get_idb_layout();
  if (source_via == nullptr || output_design == nullptr || output_layout == nullptr || source_via->get_name().empty()) {
    MJLOG.error(Loc::current(), "A child DEF contains an invalid via.");
    return false;
  }
  if (output_design->get_via_list()->find_via(source_via->get_name()) == nullptr
      && output_layout->get_via_list()->find_via(source_via->get_name()) == nullptr) {
    MJLOG.error(Loc::current(), "Child via is not available in the active design or LEF data: ", source_via->get_name());
    return false;
  }
  return true;
}

#endif

#if 1  // flatten

void DefFlattener::buildRootNetBinding(idb::IdbDesign* output_design, DFNetBinding& root_net_binding)
{
  for (idb::IdbNet* output_net : output_design->get_net_list()->get_net_list()) {
    if (output_net != nullptr) {
      root_net_binding.set_regular_net_name(output_net, output_net->get_net_name());
    }
  }
  for (idb::IdbSpecialNet* output_net : output_design->get_special_net_list()->get_net_list()) {
    if (output_net != nullptr) {
      root_net_binding.set_special_net_name(output_net, output_net->get_net_name());
    }
  }
}

void DefFlattener::flattenInstance(DFModel& df_model, idb::IdbDesign* output_design, idb::IdbDesign* parent_design,
                                   idb::IdbInstance* parent_instance, std::string parent_hierarchy_name,
                                   DFTransform parent_transform, DFNetBinding& parent_net_binding, bool remove_parent_instance)
{
  std::string child_master_name = parent_instance->get_cell_master()->get_name();
  DFSource* child_source = df_model.get_df_source(child_master_name);
  std::string child_hierarchy_name = getHierarchyName(parent_hierarchy_name, parent_instance->get_name());

  DFNetBinding child_net_binding;
  if (!buildChildNetBinding(df_model, output_design, parent_design, parent_instance, child_source->get_design(), parent_hierarchy_name,
                            parent_net_binding, child_net_binding)) {
    return;
  }

  idb::IdbCoordinate<int32_t>* parent_coordinate = parent_instance->get_coordinate();
  idb::IdbOrient parent_orient = parent_instance->get_orient() == idb::IdbOrient::kNone ? idb::IdbOrient::kN_R0 : parent_instance->get_orient();
  DFTransform instance_transform;
  instance_transform.set_instance_transform(parent_orient, parent_coordinate->get_x(), parent_coordinate->get_y(),
                                            static_cast<int32_t>(parent_instance->get_cell_master()->get_width()),
                                            static_cast<int32_t>(parent_instance->get_cell_master()->get_height()),
                                            child_source->get_die_area().get_ll_x(), child_source->get_die_area().get_ll_y());
  DFTransform child_transform = parent_transform.get_composed(instance_transform);

  DFRegionNameMap child_region_name_map;
  flattenDesign(df_model, output_design, child_source->get_design(), child_hierarchy_name, child_transform, child_net_binding,
                child_region_name_map);

  if (remove_parent_instance) {
    output_design->removeInstanceSafe(parent_instance->get_name());
  }
}

bool DefFlattener::buildChildNetBinding(DFModel& df_model, idb::IdbDesign* output_design, idb::IdbDesign* parent_design,
                                        idb::IdbInstance* parent_instance, idb::IdbDesign* child_design,
                                        std::string parent_hierarchy_name, DFNetBinding& parent_net_binding,
                                        DFNetBinding& child_net_binding)
{
  for (idb::IdbPin* parent_pin : parent_instance->get_pin_list()->get_pin_list()) {
    if (parent_pin == nullptr) {
      continue;
    }
    std::string boundary_pin_name = parent_pin->get_term() == nullptr ? parent_pin->get_pin_name() : parent_pin->get_term_name();
    idb::IdbPin* child_pin = child_design->get_io_pin_list()->find_pin(boundary_pin_name);
    idb::IdbNet* parent_regular_net = parent_pin->get_net();
    idb::IdbSpecialNet* parent_special_net = getSpecialNet(parent_design, parent_pin);
    idb::IdbNet* child_regular_net = child_pin->get_net();
    idb::IdbSpecialNet* child_special_net = getSpecialNet(child_design, child_pin);
    if (parent_special_net != nullptr) {
      parent_regular_net = nullptr;
    }
    if (child_special_net != nullptr) {
      child_regular_net = nullptr;
    }
    idb::IdbSpecialNet* parent_pg_net = getOutputPGNet(df_model, output_design, parent_regular_net);
    idb::IdbSpecialNet* child_pg_net = getOutputPGNet(df_model, output_design, child_regular_net);
    idb::IdbSpecialNet* related_parent_special_net = getRelatedSpecialNet(parent_design, parent_regular_net);

    if (parent_regular_net != nullptr && child_special_net != nullptr) {
      if (parent_pg_net != nullptr) {
        if (!bindChildSpecialNet(df_model, output_design, child_net_binding, child_special_net, parent_pg_net->get_net_name())) {
          MJLOG.error(Loc::current(), "A child special net connects to incompatible parent nets: ", child_special_net->get_net_name());
          return false;
        }
      } else if (related_parent_special_net == nullptr) {
        MJLOG.error(Loc::current(), "A regular hierarchy net has no matching parent special net: ", boundary_pin_name);
        return false;
      } else {
        idb::IdbSpecialNet* output_special_net
            = getOutputSpecialNet(df_model, output_design, related_parent_special_net, parent_hierarchy_name, parent_net_binding);
        if (!bindChildSpecialNet(df_model, output_design, child_net_binding, child_special_net, output_special_net->get_net_name())) {
          MJLOG.error(Loc::current(), "A child special net connects to incompatible parent nets: ", child_special_net->get_net_name());
          return false;
        }
      }
    } else if (parent_regular_net != nullptr) {
      if (parent_pg_net != nullptr && child_pg_net != nullptr) {
        if (parent_pg_net->get_net_name() != child_pg_net->get_net_name()) {
          MJLOG.error(Loc::current(), "PG hierarchy nets map to different top nets: ", boundary_pin_name);
          return false;
        }
      } else {
        idb::IdbNet* output_regular_net = getOutputRegularNet(df_model, output_design, parent_regular_net, parent_hierarchy_name,
                                                              parent_net_binding);
        if (!bindChildRegularNet(df_model, output_design, child_net_binding, child_regular_net, output_regular_net->get_net_name())) {
          MJLOG.error(Loc::current(), "A child regular net connects to incompatible parent nets: ", child_regular_net->get_net_name());
          return false;
        }
      }
    }
    if (parent_special_net != nullptr) {
      idb::IdbSpecialNet* output_special_net = getOutputSpecialNet(df_model, output_design, parent_special_net,
                                                                    parent_hierarchy_name, parent_net_binding);
      if (child_special_net != nullptr
          && !bindChildSpecialNet(df_model, output_design, child_net_binding, child_special_net, output_special_net->get_net_name())) {
        MJLOG.error(Loc::current(), "A child special net connects to incompatible parent nets: ", child_special_net->get_net_name());
        return false;
      }
      if (child_special_net == nullptr && (child_pg_net == nullptr || child_pg_net->get_net_name() != output_special_net->get_net_name())) {
        MJLOG.error(Loc::current(), "Special and regular hierarchy nets cannot be merged at pin: ", boundary_pin_name);
        return false;
      }
    }
  }
  return true;
}

bool DefFlattener::bindChildRegularNet(DFModel& df_model, idb::IdbDesign* output_design, DFNetBinding& child_net_binding,
                                       idb::IdbNet* child_net, std::string output_net_name)
{
  if (child_net == nullptr) {
    return false;
  }
  std::string child_output_net_name = child_net_binding.get_regular_net_name(child_net);
  if (child_output_net_name.empty()) {
    return child_net_binding.set_regular_net_name(child_net, output_net_name);
  }
  return mergeOutputRegularNet(df_model, output_design, child_output_net_name, output_net_name);
}

bool DefFlattener::bindChildSpecialNet(DFModel& df_model, idb::IdbDesign* output_design, DFNetBinding& child_net_binding,
                                       idb::IdbSpecialNet* child_net, std::string output_net_name)
{
  if (child_net == nullptr) {
    return false;
  }
  std::string top_pg_net_name = df_model.get_df_config().get_top_pg_net_name(child_net->get_net_name());
  if (!top_pg_net_name.empty() && top_pg_net_name != output_net_name) {
    MJLOG.error(Loc::current(), "The child PG net conflicts with its top PG net: ", child_net->get_net_name());
    return false;
  }
  std::string child_output_net_name = child_net_binding.get_special_net_name(child_net);
  if (child_output_net_name.empty()) {
    return child_net_binding.set_special_net_name(child_net, output_net_name);
  }
  return mergeOutputSpecialNet(df_model, output_design, child_output_net_name, output_net_name);
}

idb::IdbSpecialNet* DefFlattener::getOutputPGNet(DFModel& df_model, idb::IdbDesign* output_design, idb::IdbNet* source_net)
{
  if (source_net == nullptr) {
    return nullptr;
  }
  std::string top_pg_net_name = df_model.get_df_config().get_top_pg_net_name(source_net->get_net_name());
  if (top_pg_net_name.empty()) {
    return nullptr;
  }
  return output_design->get_special_net_list()->find_net(top_pg_net_name);
}

void DefFlattener::flattenDesign(DFModel& df_model, idb::IdbDesign* output_design, idb::IdbDesign* source_design,
                                 std::string hierarchy_name, DFTransform transform, DFNetBinding& net_binding,
                                 DFRegionNameMap& region_name_map)
{
  copyRegionList(output_design, source_design, hierarchy_name, transform, region_name_map);
  copyBlockageList(output_design, source_design, hierarchy_name, transform);
  copyRegularNetWireList(df_model, output_design, source_design, hierarchy_name, transform, net_binding);
  copySpecialNetWireList(df_model, output_design, source_design, hierarchy_name, transform, net_binding);
  copyBoundaryPinGeometry(df_model, output_design, source_design, hierarchy_name, transform, net_binding);

  for (idb::IdbInstance* source_instance : source_design->get_instance_list()->get_instance_list()) {
    if (source_instance == nullptr || source_instance->get_cell_master() == nullptr) {
      continue;
    }
    if (df_model.has_df_source(source_instance->get_cell_master()->get_name())) {
      flattenInstance(df_model, output_design, source_design, source_instance, hierarchy_name, transform, net_binding, false);
    } else {
      copyLeafInstance(df_model, output_design, source_instance, hierarchy_name, transform, net_binding, region_name_map);
    }
  }
}

#endif

#if 1  // merge

bool DefFlattener::mergeOutputRegularNet(DFModel& df_model, idb::IdbDesign* output_design, std::string target_net_name,
                                         std::string source_net_name)
{
  std::string target_root_name = df_model.get_regular_net_union().get_root_name(target_net_name);
  std::string source_root_name = df_model.get_regular_net_union().get_root_name(source_net_name);
  if (target_root_name == source_root_name) {
    return true;
  }
  if (!output_design->mergeNetInto(target_root_name, source_root_name, true)) {
    MJLOG.error(Loc::current(), "Cannot merge hierarchy regular nets: ", target_root_name, " and ", source_root_name);
    return false;
  }
  df_model.get_regular_net_union().merge_net_name(target_root_name, source_root_name);
  return true;
}

bool DefFlattener::mergeOutputSpecialNet(DFModel& df_model, idb::IdbDesign* output_design, std::string target_net_name,
                                         std::string source_net_name)
{
  std::string target_root_name = df_model.get_special_net_union().get_root_name(target_net_name);
  std::string source_root_name = df_model.get_special_net_union().get_root_name(source_net_name);
  if (target_root_name == source_root_name) {
    return true;
  }
  idb::IdbSpecialNet* target_net = output_design->get_special_net_list()->find_net(target_root_name);
  idb::IdbSpecialNet* source_net = output_design->get_special_net_list()->find_net(source_root_name);
  if (target_net == nullptr || source_net == nullptr) {
    MJLOG.error(Loc::current(), "Cannot merge hierarchy special nets: ", target_root_name, " and ", source_root_name);
    return false;
  }
  if (target_net->get_connect_type() == idb::IdbConnectType::kNone) {
    target_net->set_connect_type(source_net->get_connect_type());
  }

  std::vector<idb::IdbPin*> source_pin_list = source_net->get_io_pin_list()->get_pin_list();
  std::vector<idb::IdbPin*> source_instance_pin_list = source_net->get_instance_pin_list()->get_pin_list();
  source_pin_list.insert(source_pin_list.end(), source_instance_pin_list.begin(), source_instance_pin_list.end());
  if (!output_design->connectPinsToSpecialNet(source_pin_list, target_net)) {
    MJLOG.error(Loc::current(), "Cannot connect merged hierarchy special net pins: ", target_root_name);
    return false;
  }
  for (std::string& source_pin_name : source_net->get_pin_string_list()) {
    target_net->add_wildcard_instance_pin(source_pin_name);
  }

  std::vector<idb::IdbSpecialWire*>& target_wire_list = target_net->get_wire_list()->get_wire_list();
  std::vector<idb::IdbSpecialWire*>& source_wire_list = source_net->get_wire_list()->get_wire_list();
  target_wire_list.insert(target_wire_list.end(), source_wire_list.begin(), source_wire_list.end());
  source_wire_list.clear();
  if (!output_design->removeSpecialNetSafe(source_root_name)) {
    MJLOG.error(Loc::current(), "Cannot remove merged hierarchy special net: ", source_root_name);
    return false;
  }
  df_model.get_special_net_union().merge_net_name(target_root_name, source_root_name);
  return true;
}

#endif

#if 1  // copy

void DefFlattener::copyRegionList(idb::IdbDesign* output_design, idb::IdbDesign* source_design, std::string hierarchy_name,
                                  DFTransform transform, DFRegionNameMap& region_name_map)
{
  for (idb::IdbRegion* source_region : source_design->get_region_list()->get_region_list()) {
    if (source_region == nullptr) {
      continue;
    }
    std::string source_region_name = source_region->get_name();
    if (source_region_name.empty()) {
      source_region_name = "region";
    }
    std::string output_region_name = getUniqueRegionName(output_design, getHierarchyName(hierarchy_name, source_region_name));
    idb::IdbRegion* output_region = output_design->get_region_list()->add_region(output_region_name);
    output_region->set_type(source_region->get_type());
    for (idb::IdbRect* source_rect : source_region->get_boundary()) {
      if (source_rect == nullptr) {
        continue;
      }
      idb::IdbRect output_rect = transform.get_transformed_rect(*source_rect);
      output_region->add_boundary(output_rect.get_low_x(), output_rect.get_low_y(), output_rect.get_high_x(), output_rect.get_high_y());
    }
    region_name_map.set_output_name(source_region->get_name(), output_region_name);
  }
}

void DefFlattener::copyBlockageList(idb::IdbDesign* output_design, idb::IdbDesign* source_design, std::string hierarchy_name,
                                    DFTransform transform)
{
  idb::IdbLayout* output_layout = output_design->get_layout();
  for (idb::IdbBlockage* source_blockage : source_design->get_blockage_list()->get_blockage_list()) {
    if (source_blockage == nullptr) {
      continue;
    }
    idb::IdbBlockage* output_blockage = nullptr;
    if (source_blockage->is_routing_blockage()) {
      idb::IdbRoutingBlockage* source_routing_blockage = static_cast<idb::IdbRoutingBlockage*>(source_blockage);
      idb::IdbRoutingBlockage* output_routing_blockage
          = output_design->get_blockage_list()->add_blockage_routing(source_routing_blockage->get_layer_name());
      output_routing_blockage->set_layer(output_layout->get_layers()->find_layer(source_routing_blockage->get_layer_name()));
      output_routing_blockage->set_slots(source_routing_blockage->is_slots());
      output_routing_blockage->set_fills(source_routing_blockage->is_fills());
      output_routing_blockage->set_except_pgnet(source_routing_blockage->is_except_pgnet());
      output_routing_blockage->set_min_spacing(source_routing_blockage->get_min_spacing());
      output_routing_blockage->set_effective_width(source_routing_blockage->get_effective_width());
      output_blockage = output_routing_blockage;
    } else if (source_blockage->is_palcement_blockage()) {
      idb::IdbPlacementBlockage* source_placement_blockage = static_cast<idb::IdbPlacementBlockage*>(source_blockage);
      idb::IdbPlacementBlockage* output_placement_blockage = output_design->get_blockage_list()->add_blockage_placement();
      output_placement_blockage->set_soft(source_placement_blockage->is_soft());
      output_placement_blockage->set_fills(source_placement_blockage->is_partial());
      output_placement_blockage->set_max_density(source_placement_blockage->get_max_density());
      output_blockage = output_placement_blockage;
    }
    if (output_blockage == nullptr) {
      continue;
    }
    if (!source_blockage->get_instance_name().empty()) {
      output_blockage->set_instance_name(getHierarchyName(hierarchy_name, source_blockage->get_instance_name()));
    }
    output_blockage->set_pushdown(source_blockage->is_pushdown());
    for (idb::IdbRect* source_rect : source_blockage->get_rect_list()) {
      if (source_rect == nullptr) {
        continue;
      }
      idb::IdbRect output_rect = transform.get_transformed_rect(*source_rect);
      output_blockage->add_rect(output_rect.get_low_x(), output_rect.get_low_y(), output_rect.get_high_x(), output_rect.get_high_y());
    }
  }
}

void DefFlattener::copyRegularNetWireList(DFModel& df_model, idb::IdbDesign* output_design, idb::IdbDesign* source_design,
                                          std::string hierarchy_name, DFTransform transform, DFNetBinding& net_binding)
{
  for (idb::IdbNet* source_net : source_design->get_net_list()->get_net_list()) {
    if (source_net == nullptr) {
      continue;
    }
    idb::IdbSpecialNet* output_pg_net = getOutputPGNet(df_model, output_design, source_net);
    if (output_pg_net != nullptr) {
      for (idb::IdbRegularWire* source_wire : source_net->get_wire_list()->get_wire_list()) {
        copyRegularWire(output_design, output_pg_net, source_wire, transform);
      }
      continue;
    }
    idb::IdbNet* output_net = getOutputRegularNet(df_model, output_design, source_net, hierarchy_name, net_binding);
    for (idb::IdbRegularWire* source_wire : source_net->get_wire_list()->get_wire_list()) {
      copyRegularWire(output_design, output_net, source_wire, transform);
    }
  }
}

void DefFlattener::copyRegularWire(idb::IdbDesign* output_design, idb::IdbNet* output_net, idb::IdbRegularWire* source_wire,
                                   DFTransform transform)
{
  idb::IdbRegularWire* output_wire = output_net->get_wire_list()->add_wire();
  output_wire->set_wire_state(source_wire->get_wire_statement());
  output_wire->set_shield_name(source_wire->get_shiled_name());
  for (idb::IdbRegularWireSegment* source_segment : source_wire->get_segment_list()) {
    idb::IdbRegularWireSegment* output_segment = output_wire->add_segment();
    copyRegularWireSegment(output_design, output_segment, source_segment, transform);
  }
}

void DefFlattener::copyRegularWire(idb::IdbDesign* output_design, idb::IdbSpecialNet* output_net,
                                   idb::IdbRegularWire* source_wire, DFTransform transform)
{
  idb::IdbSpecialWire* output_wire = output_net->get_wire_list()->add_wire();
  output_wire->set_wire_state(source_wire->get_wire_statement());
  output_wire->set_shield_name(source_wire->get_shiled_name());
  for (idb::IdbRegularWireSegment* source_segment : source_wire->get_segment_list()) {
    idb::IdbSpecialWireSegment* output_segment = output_wire->add_segment();
    copyRegularWireSegment(output_design, output_segment, source_segment, transform);
  }
}

void DefFlattener::copyRegularWireSegment(idb::IdbDesign* output_design, idb::IdbRegularWireSegment* output_segment,
                                          idb::IdbRegularWireSegment* source_segment, DFTransform transform)
{
  idb::IdbLayer* output_layer = output_design->get_layout()->get_layers()->find_layer(source_segment->get_layer()->get_name());
  output_segment->set_layer_name(output_layer->get_name());
  output_segment->set_layer(output_layer);
  output_segment->set_layer_status(source_segment->is_new_layer());
  for (idb::IdbCoordinate<int32_t>* source_point : source_segment->get_point_list()) {
    idb::IdbCoordinate<int32_t> output_point = transform.get_transformed_coordinate(source_point->get_x(), source_point->get_y());
    std::optional<int32_t> point_ext = source_segment->get_point_ext(source_point);
    if (point_ext.has_value()) {
      output_segment->add_flush_point(output_point.get_x(), output_point.get_y(), point_ext.value());
    } else if (source_segment->is_virtual(source_point)) {
      output_segment->add_virtual_point(output_point.get_x(), output_point.get_y());
    } else {
      output_segment->add_point(output_point.get_x(), output_point.get_y());
    }
  }
  if (source_segment->is_rect()) {
    idb::IdbRect output_rect = transform.get_transformed_rect(source_segment->get_segment_rect());
    idb::IdbCoordinate<int32_t> output_start(output_rect.get_low_x(), output_rect.get_low_y());
    if (source_segment->get_point_start() != nullptr) {
      output_start = transform.get_transformed_coordinate(source_segment->get_point_start()->get_x(), source_segment->get_point_start()->get_y());
    }
    if (output_segment->get_point_start() == nullptr) {
      output_segment->add_point(output_start.get_x(), output_start.get_y());
    }
    output_segment->set_is_rect(true);
    output_segment->set_delta_rect(output_rect.get_low_x() - output_start.get_x(), output_rect.get_low_y() - output_start.get_y(),
                                   output_rect.get_high_x() - output_start.get_x(), output_rect.get_high_y() - output_start.get_y());
  }
  if (source_segment->is_via()) {
    output_segment->set_is_via(true);
    for (idb::IdbVia* source_via : source_segment->get_via_list()) {
      idb::IdbVia* output_via = getOutputVia(output_design, source_via);
      idb::IdbVia* output_via_copy = output_segment->copy_via(output_via);
      idb::IdbCoordinate<int32_t>* source_coordinate = source_via->get_coordinate();
      idb::IdbCoordinate<int32_t> output_coordinate
          = transform.get_transformed_coordinate(source_coordinate->get_x(), source_coordinate->get_y());
      output_via_copy->set_coordinate(output_coordinate.get_x(), output_coordinate.get_y());
    }
  }
}

void DefFlattener::copyRegularWireSegment(idb::IdbDesign* output_design, idb::IdbSpecialWireSegment* output_segment,
                                          idb::IdbRegularWireSegment* source_segment, DFTransform transform)
{
  idb::IdbLayer* output_layer = output_design->get_layout()->get_layers()->find_layer(source_segment->get_layer()->get_name());
  output_segment->set_layer(output_layer);
  output_segment->set_layer_status(source_segment->is_new_layer());
  output_segment->set_route_width(static_cast<idb::IdbLayerRouting*>(output_layer)->get_width());
  for (idb::IdbCoordinate<int32_t>* source_point : source_segment->get_point_list()) {
    idb::IdbCoordinate<int32_t> output_point = transform.get_transformed_coordinate(source_point->get_x(), source_point->get_y());
    std::optional<int32_t> point_ext = source_segment->get_point_ext(source_point);
    if (point_ext.has_value()) {
      output_segment->add_flush_point(output_point.get_x(), output_point.get_y(), point_ext.value());
    } else {
      output_segment->add_point(output_point.get_x(), output_point.get_y());
    }
  }
  if (source_segment->is_rect()) {
    idb::IdbRect output_rect = transform.get_transformed_rect(source_segment->get_segment_rect());
    output_segment->set_is_rect(true);
    output_segment->set_delta_rect(output_rect.get_low_x(), output_rect.get_low_y(), output_rect.get_high_x(), output_rect.get_high_y());
  }
  if (source_segment->is_via() && !source_segment->get_via_list().empty()) {
    output_segment->set_is_via(true);
    idb::IdbVia* output_via = getOutputVia(output_design, source_segment->get_via_list().front());
    idb::IdbVia* output_via_copy = output_segment->copy_via(output_via);
    idb::IdbCoordinate<int32_t>* source_coordinate = source_segment->get_via_list().front()->get_coordinate();
    idb::IdbCoordinate<int32_t> output_coordinate
        = transform.get_transformed_coordinate(source_coordinate->get_x(), source_coordinate->get_y());
    output_via_copy->set_coordinate(output_coordinate.get_x(), output_coordinate.get_y());
  }
  output_segment->set_bounding_box();
}

void DefFlattener::copySpecialNetWireList(DFModel& df_model, idb::IdbDesign* output_design, idb::IdbDesign* source_design,
                                          std::string hierarchy_name, DFTransform transform, DFNetBinding& net_binding)
{
  for (idb::IdbSpecialNet* source_net : source_design->get_special_net_list()->get_net_list()) {
    if (source_net == nullptr) {
      continue;
    }
    idb::IdbSpecialNet* output_net = getOutputSpecialNet(df_model, output_design, source_net, hierarchy_name, net_binding);
    for (idb::IdbSpecialWire* source_wire : source_net->get_wire_list()->get_wire_list()) {
      copySpecialWire(output_design, output_net, source_wire, transform);
    }
  }
}

void DefFlattener::copySpecialWire(idb::IdbDesign* output_design, idb::IdbSpecialNet* output_net, idb::IdbSpecialWire* source_wire,
                                   DFTransform transform)
{
  idb::IdbSpecialWire* output_wire = output_net->get_wire_list()->add_wire();
  output_wire->set_wire_state(source_wire->get_wire_state());
  output_wire->set_shield_name(source_wire->get_shiled_name());
  for (idb::IdbSpecialWireSegment* source_segment : source_wire->get_segment_list()) {
    idb::IdbSpecialWireSegment* output_segment = output_wire->add_segment();
    copySpecialWireSegment(output_design, output_segment, source_segment, transform);
  }
}

void DefFlattener::copySpecialWireSegment(idb::IdbDesign* output_design, idb::IdbSpecialWireSegment* output_segment,
                                          idb::IdbSpecialWireSegment* source_segment, DFTransform transform)
{
  idb::IdbLayer* output_layer = output_design->get_layout()->get_layers()->find_layer(source_segment->get_layer()->get_name());
  output_segment->set_layer(output_layer);
  output_segment->set_layer_status(source_segment->is_new_layer());
  output_segment->set_route_width(source_segment->get_route_width());
  output_segment->set_shape_type(source_segment->get_shape_type());
  output_segment->set_style(source_segment->get_style());
  for (idb::IdbCoordinate<int32_t>* source_point : source_segment->get_point_list()) {
    idb::IdbCoordinate<int32_t> output_point = transform.get_transformed_coordinate(source_point->get_x(), source_point->get_y());
    std::optional<int32_t> point_ext = source_segment->get_point_ext(source_point);
    if (point_ext.has_value()) {
      output_segment->add_flush_point(output_point.get_x(), output_point.get_y(), point_ext.value());
    } else {
      output_segment->add_point(output_point.get_x(), output_point.get_y());
    }
  }
  if (source_segment->is_rect()) {
    idb::IdbRect output_rect = transform.get_transformed_rect(*source_segment->get_delta_rect());
    output_segment->set_is_rect(true);
    output_segment->set_delta_rect(output_rect.get_low_x(), output_rect.get_low_y(), output_rect.get_high_x(), output_rect.get_high_y());
  }
  if (source_segment->is_via()) {
    output_segment->set_is_via(true);
    idb::IdbVia* output_via = getOutputVia(output_design, source_segment->get_via());
    idb::IdbVia* output_via_copy = output_segment->copy_via(output_via);
    idb::IdbCoordinate<int32_t>* source_coordinate = source_segment->get_via()->get_coordinate();
    idb::IdbCoordinate<int32_t> output_coordinate
        = transform.get_transformed_coordinate(source_coordinate->get_x(), source_coordinate->get_y());
    output_via_copy->set_coordinate(output_coordinate.get_x(), output_coordinate.get_y());
  }
  output_segment->set_bounding_box();
}

void DefFlattener::copyBoundaryPinGeometry(DFModel& df_model, idb::IdbDesign* output_design, idb::IdbDesign* source_design,
                                           std::string hierarchy_name, DFTransform transform, DFNetBinding& net_binding)
{
  for (idb::IdbPin* source_pin : source_design->get_io_pin_list()->get_pin_list()) {
    if (source_pin == nullptr) {
      continue;
    }
    idb::IdbSpecialNet* source_special_net = getSpecialNet(source_design, source_pin);
    idb::IdbSpecialNet* output_pg_net = getOutputPGNet(df_model, output_design, source_pin->get_net());
    if (source_special_net != nullptr) {
      idb::IdbSpecialNet* output_net
          = getOutputSpecialNet(df_model, output_design, source_special_net, hierarchy_name, net_binding);
      copyBoundaryPinShape(output_design, source_pin, output_net, transform);
      copyBoundaryPinVia(output_design, source_pin, output_net, transform);
    } else if (output_pg_net != nullptr) {
      copyBoundaryPinShape(output_design, source_pin, output_pg_net, transform);
      copyBoundaryPinVia(output_design, source_pin, output_pg_net, transform);
    } else if (source_pin->get_net() != nullptr) {
      idb::IdbNet* output_net = getOutputRegularNet(df_model, output_design, source_pin->get_net(), hierarchy_name, net_binding);
      copyBoundaryPinShape(output_design, source_pin, output_net, transform);
      copyBoundaryPinVia(output_design, source_pin, output_net, transform);
    }
  }
}

void DefFlattener::copyBoundaryPinShape(idb::IdbDesign* output_design, idb::IdbPin* source_pin, idb::IdbNet* output_net,
                                        DFTransform transform)
{
  for (idb::IdbLayerShape* source_layer_shape : source_pin->get_port_box_list()) {
    if (source_layer_shape == nullptr || source_layer_shape->get_layer() == nullptr) {
      continue;
    }
    idb::IdbLayer* output_layer = output_design->get_layout()->get_layers()->find_layer(source_layer_shape->get_layer()->get_name());
    for (idb::IdbRect* source_rect : source_layer_shape->get_rect_list()) {
      if (source_rect == nullptr) {
        continue;
      }
      idb::IdbRect output_rect = transform.get_transformed_rect(*source_rect);
      idb::IdbRegularWire* output_wire = output_net->get_wire_list()->add_wire();
      output_wire->set_wire_state(idb::IdbWiringStatement::kRouted);
      idb::IdbRegularWireSegment* output_segment = output_wire->add_segment();
      output_segment->set_layer_name(output_layer->get_name());
      output_segment->set_layer(output_layer);
      output_segment->add_point(output_rect.get_low_x(), output_rect.get_low_y());
      output_segment->set_is_rect(true);
      output_segment->set_delta_rect(0, 0, output_rect.get_width(), output_rect.get_height());
    }
  }
}

void DefFlattener::copyBoundaryPinShape(idb::IdbDesign* output_design, idb::IdbPin* source_pin, idb::IdbSpecialNet* output_net,
                                        DFTransform transform)
{
  for (idb::IdbLayerShape* source_layer_shape : source_pin->get_port_box_list()) {
    if (source_layer_shape == nullptr || source_layer_shape->get_layer() == nullptr) {
      continue;
    }
    idb::IdbLayer* output_layer = output_design->get_layout()->get_layers()->find_layer(source_layer_shape->get_layer()->get_name());
    for (idb::IdbRect* source_rect : source_layer_shape->get_rect_list()) {
      if (source_rect == nullptr) {
        continue;
      }
      idb::IdbRect output_rect = transform.get_transformed_rect(*source_rect);
      idb::IdbSpecialWire* output_wire = output_net->get_wire_list()->add_wire();
      output_wire->set_wire_state(idb::IdbWiringStatement::kRouted);
      idb::IdbSpecialWireSegment* output_segment = output_wire->add_segment();
      output_segment->set_layer(output_layer);
      output_segment->set_is_rect(true);
      output_segment->set_delta_rect(output_rect.get_low_x(), output_rect.get_low_y(), output_rect.get_high_x(), output_rect.get_high_y());
      output_segment->set_bounding_box();
    }
  }
}

void DefFlattener::copyBoundaryPinVia(idb::IdbDesign* output_design, idb::IdbPin* source_pin, idb::IdbNet* output_net,
                                      DFTransform transform)
{
  for (idb::IdbVia* source_via : source_pin->get_via_list()) {
    idb::IdbLayerShape source_layer_shape = source_via->get_bottom_layer_shape();
    if (source_layer_shape.get_layer() == nullptr) {
      continue;
    }
    idb::IdbLayer* output_layer = output_design->get_layout()->get_layers()->find_layer(source_layer_shape.get_layer()->get_name());
    idb::IdbCoordinate<int32_t>* source_coordinate = source_via->get_coordinate();
    idb::IdbCoordinate<int32_t> output_coordinate
        = transform.get_transformed_coordinate(source_coordinate->get_x(), source_coordinate->get_y());
    idb::IdbRegularWire* output_wire = output_net->get_wire_list()->add_wire();
    output_wire->set_wire_state(idb::IdbWiringStatement::kRouted);
    idb::IdbRegularWireSegment* output_segment = output_wire->add_segment();
    output_segment->set_layer_name(output_layer->get_name());
    output_segment->set_layer(output_layer);
    output_segment->add_point(output_coordinate.get_x(), output_coordinate.get_y());
    output_segment->set_is_via(true);
    idb::IdbVia* output_via_copy = output_segment->copy_via(getOutputVia(output_design, source_via));
    output_via_copy->set_coordinate(output_coordinate.get_x(), output_coordinate.get_y());
  }
}

void DefFlattener::copyBoundaryPinVia(idb::IdbDesign* output_design, idb::IdbPin* source_pin, idb::IdbSpecialNet* output_net,
                                      DFTransform transform)
{
  for (idb::IdbVia* source_via : source_pin->get_via_list()) {
    idb::IdbLayerShape source_layer_shape = source_via->get_bottom_layer_shape();
    if (source_layer_shape.get_layer() == nullptr) {
      continue;
    }
    idb::IdbLayer* output_layer = output_design->get_layout()->get_layers()->find_layer(source_layer_shape.get_layer()->get_name());
    idb::IdbCoordinate<int32_t>* source_coordinate = source_via->get_coordinate();
    idb::IdbCoordinate<int32_t> output_coordinate
        = transform.get_transformed_coordinate(source_coordinate->get_x(), source_coordinate->get_y());
    idb::IdbSpecialWire* output_wire = output_net->get_wire_list()->add_wire();
    output_wire->set_wire_state(idb::IdbWiringStatement::kRouted);
    idb::IdbSpecialWireSegment* output_segment = output_wire->add_segment();
    output_segment->set_layer(output_layer);
    output_segment->add_point(output_coordinate.get_x(), output_coordinate.get_y());
    output_segment->set_is_via(true);
    idb::IdbVia* output_via_copy = output_segment->copy_via(getOutputVia(output_design, source_via));
    output_via_copy->set_coordinate(output_coordinate.get_x(), output_coordinate.get_y());
    output_segment->set_bounding_box();
  }
}

void DefFlattener::copyLeafInstance(DFModel& df_model, idb::IdbDesign* output_design, idb::IdbInstance* source_instance,
                                    std::string hierarchy_name, DFTransform transform, DFNetBinding& net_binding,
                                    DFRegionNameMap& region_name_map)
{
  idb::IdbCoordinate<int32_t>* source_coordinate = source_instance->get_coordinate();
  idb::IdbCoordinate<int32_t> output_coordinate
      = transform.get_transformed_coordinate(source_coordinate->get_x(), source_coordinate->get_y());
  idb::IdbOrient source_orient = source_instance->get_orient() == idb::IdbOrient::kNone ? idb::IdbOrient::kN_R0 : source_instance->get_orient();
  idb::IdbOrient output_orient = transform.get_composed_orient(source_orient);
  std::string output_instance_name = output_design->makeUniqueInstanceName(getHierarchyName(hierarchy_name, source_instance->get_name()));
  idb::IdbInstance* output_instance
      = output_design->createInstance(output_instance_name, source_instance->get_cell_master()->get_name(), source_instance->get_type(),
                                      source_instance->get_status(), output_orient, output_coordinate.get_x(), output_coordinate.get_y(),
                                      idb::IdbCreatePolicy::kErrorIfExists);

  for (idb::IdbPin* source_pin : source_instance->get_pin_list()->get_pin_list()) {
    if (source_pin == nullptr) {
      continue;
    }
    std::string pin_name = source_pin->get_term() == nullptr ? source_pin->get_pin_name() : source_pin->get_term_name();
    idb::IdbPin* output_pin = output_instance->get_pin_by_term(pin_name);
    if (output_pin == nullptr) {
      output_pin = output_instance->get_pin(source_pin->get_pin_name());
    }
    idb::IdbSpecialNet* source_special_net = getSpecialNet(nullptr, source_pin);
    idb::IdbSpecialNet* output_pg_net = getOutputPGNet(df_model, output_design, source_pin->get_net());
    if (source_special_net != nullptr) {
      idb::IdbSpecialNet* output_net
          = getOutputSpecialNet(df_model, output_design, source_special_net, hierarchy_name, net_binding);
      df_model.add_special_net_pin(output_net->get_net_name(), output_pin);
    } else if (output_pg_net != nullptr) {
      df_model.add_special_net_pin(output_pg_net->get_net_name(), output_pin);
    } else if (source_pin->get_net() != nullptr) {
      idb::IdbNet* output_net = getOutputRegularNet(df_model, output_design, source_pin->get_net(), hierarchy_name, net_binding);
      output_design->connectPinToNet(output_pin, output_net);
    }
  }

  idb::IdbRegion* source_region = source_instance->get_region();
  if (source_region != nullptr) {
    std::string output_region_name = region_name_map.get_output_name(source_region->get_name());
    idb::IdbRegion* output_region = output_design->get_region_list()->find_region(output_region_name);
    if (output_region != nullptr) {
      output_region->add_instance(output_instance);
      output_instance->set_region(output_region);
    }
  }
}

#endif

#if 1  // get

idb::IdbNet* DefFlattener::getOutputRegularNet(DFModel& df_model, idb::IdbDesign* output_design, idb::IdbNet* source_net,
                                                std::string hierarchy_name, DFNetBinding& net_binding)
{
  std::string output_net_name = net_binding.get_regular_net_name(source_net);
  if (output_net_name.empty()) {
    output_net_name = output_design->makeUniqueNetName(getHierarchyName(hierarchy_name, source_net->get_net_name()));
    net_binding.set_regular_net_name(source_net, output_net_name);
  }
  output_net_name = df_model.get_regular_net_union().get_root_name(output_net_name);
  return output_design->createOrFindNet(output_net_name, source_net->get_connect_type());
}

idb::IdbSpecialNet* DefFlattener::getOutputSpecialNet(DFModel& df_model, idb::IdbDesign* output_design,
                                                       idb::IdbSpecialNet* source_net, std::string hierarchy_name,
                                                       DFNetBinding& net_binding)
{
  std::string output_net_name = net_binding.get_special_net_name(source_net);
  if (output_net_name.empty()) {
    output_net_name = df_model.get_df_config().get_top_pg_net_name(source_net->get_net_name());
    if (output_net_name.empty()) {
      output_net_name = getHierarchyName(hierarchy_name, source_net->get_net_name());
    }
    net_binding.set_special_net_name(source_net, output_net_name);
  }
  output_net_name = df_model.get_special_net_union().get_root_name(output_net_name);
  return output_design->createOrFindSpecialNet(output_net_name, source_net->get_connect_type());
}

idb::IdbSpecialNet* DefFlattener::getSpecialNet(idb::IdbDesign* design, idb::IdbPin* pin)
{
  if (pin == nullptr) {
    return nullptr;
  }
  if (pin->get_special_net() != nullptr) {
    return pin->get_special_net();
  }
  return design == nullptr ? nullptr : design->findSpecialNetForInstancePin(pin);
}

idb::IdbSpecialNet* DefFlattener::getRelatedSpecialNet(idb::IdbDesign* design, idb::IdbNet* regular_net)
{
  if (design == nullptr || regular_net == nullptr || design->get_special_net_list() == nullptr) {
    return nullptr;
  }
  return design->get_special_net_list()->find_net(regular_net->get_net_name());
}

idb::IdbVia* DefFlattener::getOutputVia(idb::IdbDesign* output_design, idb::IdbVia* source_via)
{
  idb::IdbVia* output_via = output_design->get_via_list()->find_via(source_via->get_name());
  if (output_via == nullptr) {
    output_via = output_design->get_layout()->get_via_list()->find_via(source_via->get_name());
  }
  return output_via;
}

std::string DefFlattener::getHierarchyName(std::string hierarchy_name, std::string name)
{
  return hierarchy_name.empty() ? name : hierarchy_name + "__" + name;
}

std::string DefFlattener::getUniqueRegionName(idb::IdbDesign* output_design, std::string name)
{
  if (output_design->get_region_list()->find_region(name) == nullptr) {
    return name;
  }
  int32_t name_idx = 0;
  while (output_design->get_region_list()->find_region(name + "__" + std::to_string(name_idx)) != nullptr) {
    name_idx++;
  }
  return name + "__" + std::to_string(name_idx);
}

#endif

DefFlattener* DefFlattener::_df_instance = nullptr;

}  // namespace imj
