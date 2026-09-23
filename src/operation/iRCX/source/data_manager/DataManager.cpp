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
#include "DataManager.hpp"

#include "CapTableConfig.hpp"
#include "CapTableEntry.hpp"
#include "Corner.hpp"
#include "CornerData.hpp"
#include "Logger.hpp"
#include "Monitor.hpp"
#include "ProcessConductor.hpp"
#include "ProcessEtchTable.hpp"
#include "ProcessVia.hpp"
#include "ProcessViaEtchTable.hpp"
#include "RCXHeader.hpp"
#include "RCXInterface.hpp"
#include "Utility.hpp"

namespace ircx {

// public

void DataManager::initInst()
{
  if (_dm_instance == nullptr) {
    _dm_instance = new DataManager();
  }
}

DataManager& DataManager::getInst()
{
  if (_dm_instance == nullptr) {
    RCXLOG.error(Loc::current(), "The instance not initialized!");
  }
  return *_dm_instance;
}

void DataManager::destroyInst()
{
  if (_dm_instance != nullptr) {
    delete _dm_instance;
    _dm_instance = nullptr;
  }
}

// function

void DataManager::input(std::map<std::string, std::any>& config_map)
{
  Monitor monitor;
  RCXLOG.info(Loc::current(), "Starting...");
  RCXI.input(config_map);
  buildConfig();
  buildDatabase();
  printConfig();
  printDatabase();
  RCXLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

void DataManager::output()
{
  Monitor monitor;
  RCXLOG.info(Loc::current(), "Starting...");
  RCXI.output();
  RCXLOG.info(Loc::current(), "Completed", monitor.getStatsInfo());
}

// private

DataManager* DataManager::_dm_instance = nullptr;

#if 1  // build

void DataManager::buildConfig()
{
  /////////////////////////////////////////////
  // **********        RCX        ********** //
  _config.temp_directory_path = std::filesystem::absolute(_config.temp_directory_path);
  _config.temp_directory_path += "/";
  _config.log_file_path = _config.temp_directory_path + "rcx.log";
  // **********    DataManager    ********** //
  _config.dm_temp_directory_path = _config.temp_directory_path + "data_manager/";
  // ********** TopoBuilder  ********** //
  _config.tb_temp_directory_path = _config.temp_directory_path + "topo_builder/";
  // ********** EnvBuilder ******** //
  _config.eb_temp_directory_path = _config.temp_directory_path + "env_builder/";
  // ********** VarProcessor ******** //
  _config.vp_temp_directory_path = _config.temp_directory_path + "var_processor/";
  // ********** ResExtractor ****** //
  _config.re_temp_directory_path = _config.temp_directory_path + "res_extractor/";
  // ********** CapExtractor **** //
  _config.ce_temp_directory_path = _config.temp_directory_path + "cap_extractor/";
  // **********     SPEFWriter    ********** //
  _config.sw_temp_directory_path = _config.temp_directory_path + "spef_writer/";
  /////////////////////////////////////////////
  // **********        RCX        ********** //
  RCXUTIL.removeDir(_config.temp_directory_path);
  RCXUTIL.createDir(_config.temp_directory_path);
  RCXUTIL.createDirByFile(_config.log_file_path);
  // **********    DataManager    ********** //
  RCXUTIL.createDir(_config.dm_temp_directory_path);
  // ********** TopoBuilder  ********** //
  RCXUTIL.createDir(_config.tb_temp_directory_path);
  // ********** EnvBuilder ******** //
  RCXUTIL.createDir(_config.eb_temp_directory_path);
  // ********** VarProcessor ******** //
  RCXUTIL.createDir(_config.vp_temp_directory_path);
  // ********** ResExtractor ****** //
  RCXUTIL.createDir(_config.re_temp_directory_path);
  // ********** CapExtractor **** //
  RCXUTIL.createDir(_config.ce_temp_directory_path);
  // **********     SPEFWriter    ********** //
  RCXUTIL.createDir(_config.sw_temp_directory_path);
  /////////////////////////////////////////////
  RCXLOG.openLogFileStream(_config.log_file_path);
}

void DataManager::buildDatabase()
{
  if (_database.get_layout_data().get_is_empty()) {
    return;
  }

  buildCornerDataList();
  buildLayerMapping();
}

void DataManager::buildCornerDataList()
{
  for (Corner& corner : _config.corner_list) {
    for (double tmpr : corner.get_tmpr_list()) {
      buildCornerData(corner, tmpr);
    }
  }
}

void DataManager::buildCornerData(Corner& corner, double tmpr)
{
  CornerData corner_data;
  corner_data.set_corner_name(getTmprCornerName(corner.get_corner_name(), tmpr));
  corner_data.set_tmpr(tmpr);
  corner_data.set_global_tmpr(25.0);
  corner_data.set_half_node_scale_factor(1.0);
  buildProcessCorner(corner_data, corner.get_itf_file_path());
  buildCapTable(corner_data, corner.get_captab_file_path());
  _database.get_corner_data_list().push_back(std::move(corner_data));
}

std::string DataManager::getTmprCornerName(std::string corner_name, double tmpr)
{
  std::ostringstream tmpr_stream;
  tmpr_stream << std::setprecision(12) << tmpr;
  std::string tmpr_name = tmpr_stream.str();
  for (char& tmpr_char : tmpr_name) {
    if (tmpr_char == '.') {
      tmpr_char = 'p';
    } else if (tmpr_char == '-') {
      tmpr_char = 'm';
    }
  }
  return RCXUTIL.getString(corner_name, "_", tmpr_name, "C");
}

void DataManager::buildProcessCorner(CornerData& corner_data, std::string itf_file_path)
{
  std::ifstream* itf_file_stream = RCXUTIL.getInputFileStream(itf_file_path);
  if (!itf_file_stream->is_open()) {
    RCXUTIL.closeFileStream(itf_file_stream);
    return;
  }

  std::stringstream itf_stream;
  itf_stream << itf_file_stream->rdbuf();
  RCXUTIL.closeFileStream(itf_file_stream);
  std::string itf_text = itf_stream.str();

  std::vector<std::string> itf_token_list;
  getITFTokenList(itf_text, itf_token_list);

  for (int32_t token_idx = 0; token_idx < static_cast<int32_t>(itf_token_list.size());) {
    std::string token_name = RCXUTIL.getUpperString(itf_token_list[token_idx]);
    double property_value = 0.0;
    if (token_name == "GLOBAL_TEMPERATURE" && getITFAssignmentNumber(itf_token_list, token_idx, property_value)) {
      corner_data.set_global_tmpr(property_value);
      token_idx += 3;
      continue;
    }
    if (token_name == "HALF_NODE_SCALE_FACTOR" && getITFAssignmentNumber(itf_token_list, token_idx, property_value)) {
      corner_data.set_half_node_scale_factor(property_value);
      token_idx += 3;
      continue;
    }
    if ((token_name == "CONDUCTOR" || token_name == "VIA") && token_idx + 2 < static_cast<int32_t>(itf_token_list.size())) {
      int32_t block_start_idx = getITFBlockStart(itf_token_list, token_idx + 2);
      if (block_start_idx == -1) {
        token_idx++;
        continue;
      }
      int32_t block_end_idx = getITFBlockEnd(itf_token_list, block_start_idx);
      if (block_end_idx == -1) {
        token_idx++;
        continue;
      }
      if (token_name == "CONDUCTOR") {
        buildProcessConductor(corner_data, itf_token_list, block_start_idx + 1, block_end_idx, itf_token_list[token_idx + 1]);
      } else {
        buildProcessVia(corner_data, itf_token_list, block_start_idx + 1, block_end_idx, itf_token_list[token_idx + 1]);
      }
      token_idx = block_end_idx + 1;
      continue;
    }
    token_idx++;
  }
}

void DataManager::buildProcessConductor(CornerData& corner_data, std::vector<std::string>& itf_token_list, int32_t start_idx, int32_t end_idx,
                                        std::string conductor_name)
{
  ProcessConductor conductor;
  conductor.set_layer_name(conductor_name);
  conductor.set_thickness(0.0);
  conductor.set_sheet_resistance(0.0);
  conductor.set_resistivity(0.0);

  for (int32_t token_idx = start_idx; token_idx < end_idx;) {
    std::string property_name = RCXUTIL.getUpperString(itf_token_list[token_idx]);
    double property_value = 0.0;
    if (property_name == "THICKNESS" && getITFAssignmentNumber(itf_token_list, token_idx, property_value)) {
      conductor.set_thickness(property_value);
      token_idx += 3;
      continue;
    }
    if (property_name == "RPSQ" && getITFAssignmentNumber(itf_token_list, token_idx, property_value)) {
      conductor.set_sheet_resistance(property_value);
      token_idx += 3;
      continue;
    }
    if (property_name == "RHO" && getITFAssignmentNumber(itf_token_list, token_idx, property_value)) {
      conductor.set_resistivity(property_value);
      token_idx += 3;
      continue;
    }
    if (property_name == "T0" && getITFAssignmentNumber(itf_token_list, token_idx, property_value)) {
      conductor.set_nominal_tmpr(property_value);
      token_idx += 3;
      continue;
    }
    if (property_name == "CRT1" && getITFAssignmentNumber(itf_token_list, token_idx, property_value)) {
      conductor.set_tmpr_coefficient1(property_value);
      token_idx += 3;
      continue;
    }
    if (property_name == "CRT2" && getITFAssignmentNumber(itf_token_list, token_idx, property_value)) {
      conductor.set_tmpr_coefficient2(property_value);
      token_idx += 3;
      continue;
    }
    if (property_name == "ETCH" && getITFAssignmentNumber(itf_token_list, token_idx, property_value)) {
      conductor.set_etch(property_value);
      token_idx += 3;
      continue;
    }
    if (property_name == "RESISTIVE_ONLY_ETCH" && getITFAssignmentNumber(itf_token_list, token_idx, property_value)) {
      conductor.set_resistive_only_etch(property_value);
      token_idx += 3;
      continue;
    }
    if (property_name == "CAPACITIVE_ONLY_ETCH" && getITFAssignmentNumber(itf_token_list, token_idx, property_value)) {
      conductor.set_capacitive_only_etch(property_value);
      token_idx += 3;
      continue;
    }

    int32_t block_start_idx = getITFBlockStart(itf_token_list, token_idx + 1);
    if (block_start_idx == -1) {
      token_idx++;
      continue;
    }
    int32_t block_end_idx = getITFBlockEnd(itf_token_list, block_start_idx);
    if (block_end_idx == -1 || block_end_idx > end_idx) {
      token_idx++;
      continue;
    }

    std::vector<double> row_list;
    std::vector<double> column_list;
    std::vector<double> value_list;
    if (property_name == "RPSQ_VS_SI_WIDTH") {
      getITFNumberList(itf_token_list, block_start_idx + 1, block_end_idx, value_list);
      for (int32_t value_idx = 0; value_idx + 1 < static_cast<int32_t>(value_list.size()); value_idx += 2) {
        conductor.get_sheet_resistance_by_width_table().add_entry(value_list[value_idx], value_list[value_idx + 1]);
      }
    } else if (property_name == "CRT_VS_SI_WIDTH") {
      getITFNumberList(itf_token_list, block_start_idx + 1, block_end_idx, value_list);
      for (int32_t value_idx = 0; value_idx + 2 < static_cast<int32_t>(value_list.size()); value_idx += 3) {
        conductor.get_tmpr_coefficient1_by_width_table().add_entry(value_list[value_idx], value_list[value_idx + 1]);
        conductor.get_tmpr_coefficient2_by_width_table().add_entry(value_list[value_idx], value_list[value_idx + 2]);
      }
    } else if (property_name == "RPSQ_VS_WIDTH_AND_SPACING" || property_name == "RHO_VS_WIDTH_AND_SPACING" || property_name == "ETCH_VS_WIDTH_AND_SPACING"
               || property_name == "THICKNESS_VS_WIDTH_AND_SPACING") {
      getITFTableValueList(itf_token_list, block_start_idx + 1, block_end_idx, "WIDTHS", "SPACINGS", "VALUES", row_list, column_list, value_list);
      ProcessTable2D table;
      table.set_row_list(row_list);
      table.set_column_list(column_list);
      table.set_value_list(value_list);
      if (property_name == "RPSQ_VS_WIDTH_AND_SPACING") {
        conductor.get_sheet_resistance_by_width_spacing_table() = table;
      } else if (property_name == "RHO_VS_WIDTH_AND_SPACING") {
        conductor.get_resistivity_by_width_spacing_table() = table;
      } else if (!table.get_is_empty()) {
        ProcessEtchTable process_table;
        process_table.set_effect_type(getITFEffectType(itf_token_list, token_idx + 1, block_start_idx));
        process_table.set_table(table);
        if (property_name == "ETCH_VS_WIDTH_AND_SPACING") {
          conductor.get_etch_table_list().push_back(std::move(process_table));
        } else {
          conductor.get_thickness_change_table_list().push_back(std::move(process_table));
        }
      }
    } else if (property_name == "RHO_VS_SI_WIDTH_AND_THICKNESS") {
      getITFTableValueList(itf_token_list, block_start_idx + 1, block_end_idx, "THICKNESS", "WIDTH", "VALUES", row_list, column_list, value_list);
      conductor.get_resistivity_by_width_thickness_table().set_row_list(row_list);
      conductor.get_resistivity_by_width_thickness_table().set_column_list(column_list);
      conductor.get_resistivity_by_width_thickness_table().set_value_list(value_list);
    }
    token_idx = block_end_idx + 1;
  }

  registerProcessLayer(conductor.get_layer_name());
  corner_data.get_process_conductor_list().push_back(std::move(conductor));
}

void DataManager::buildProcessVia(CornerData& corner_data, std::vector<std::string>& itf_token_list, int32_t start_idx, int32_t end_idx, std::string via_name)
{
  ProcessVia via;
  via.set_layer_name(via_name);
  via.set_area(0.0);
  via.set_resistance(0.0);
  via.set_resistivity(0.0);

  for (int32_t token_idx = start_idx; token_idx < end_idx;) {
    std::string property_name = RCXUTIL.getUpperString(itf_token_list[token_idx]);
    double property_value = 0.0;
    std::string property_string;
    if (property_name == "FROM" && getITFAssignmentString(itf_token_list, token_idx, property_string)) {
      via.set_from_layer_name(property_string);
      token_idx += 3;
      continue;
    }
    if (property_name == "TO" && getITFAssignmentString(itf_token_list, token_idx, property_string)) {
      via.set_to_layer_name(property_string);
      token_idx += 3;
      continue;
    }
    if (property_name == "AREA" && getITFAssignmentNumber(itf_token_list, token_idx, property_value)) {
      via.set_area(property_value);
      token_idx += 3;
      continue;
    }
    if (property_name == "RPV" && getITFAssignmentNumber(itf_token_list, token_idx, property_value)) {
      via.set_resistance(property_value);
      token_idx += 3;
      continue;
    }
    if (property_name == "RHO" && getITFAssignmentNumber(itf_token_list, token_idx, property_value)) {
      via.set_resistivity(property_value);
      token_idx += 3;
      continue;
    }
    if (property_name == "T0" && getITFAssignmentNumber(itf_token_list, token_idx, property_value)) {
      via.set_nominal_tmpr(property_value);
      token_idx += 3;
      continue;
    }
    if (property_name == "CRT1" && getITFAssignmentNumber(itf_token_list, token_idx, property_value)) {
      via.set_tmpr_coefficient1(property_value);
      token_idx += 3;
      continue;
    }
    if (property_name == "CRT2" && getITFAssignmentNumber(itf_token_list, token_idx, property_value)) {
      via.set_tmpr_coefficient2(property_value);
      token_idx += 3;
      continue;
    }

    int32_t block_start_idx = getITFBlockStart(itf_token_list, token_idx + 1);
    if (block_start_idx == -1) {
      token_idx++;
      continue;
    }
    int32_t block_end_idx = getITFBlockEnd(itf_token_list, block_start_idx);
    if (block_end_idx == -1 || block_end_idx > end_idx) {
      token_idx++;
      continue;
    }

    std::vector<double> row_list;
    std::vector<double> column_list;
    std::vector<double> value_list;
    if (property_name == "RPV_VS_AREA") {
      getITFNumberList(itf_token_list, block_start_idx + 1, block_end_idx, value_list);
      for (int32_t value_idx = 0; value_idx + 1 < static_cast<int32_t>(value_list.size()); value_idx += 2) {
        via.get_resistance_by_area_table().add_entry(value_list[value_idx], value_list[value_idx + 1]);
      }
    } else if (property_name == "CRT_VS_AREA") {
      getITFNumberList(itf_token_list, block_start_idx + 1, block_end_idx, value_list);
      for (int32_t value_idx = 0; value_idx + 2 < static_cast<int32_t>(value_list.size()); value_idx += 3) {
        via.get_tmpr_coefficient1_by_area_table().add_entry(value_list[value_idx], value_list[value_idx + 1]);
        via.get_tmpr_coefficient2_by_area_table().add_entry(value_list[value_idx], value_list[value_idx + 2]);
      }
    } else if (property_name == "ETCH_VS_WIDTH_AND_LENGTH") {
      getITFTableValueList(itf_token_list, block_start_idx + 1, block_end_idx, "WIDTHS", "LENGTHS", "VALUES", row_list, column_list, value_list);
      int32_t table_value_num = static_cast<int32_t>(row_list.size()) * static_cast<int32_t>(column_list.size());
      if (table_value_num > 0 && static_cast<int32_t>(value_list.size()) >= 2 * table_value_num) {
        std::vector<double> length_etch_list;
        std::vector<double> width_etch_list;
        length_etch_list.reserve(table_value_num);
        width_etch_list.reserve(table_value_num);
        for (int32_t value_idx = 0; value_idx < table_value_num; ++value_idx) {
          length_etch_list.push_back(value_list[2 * value_idx]);
          width_etch_list.push_back(value_list[2 * value_idx + 1]);
        }
        ProcessTable2D length_table;
        length_table.set_row_list(row_list);
        length_table.set_column_list(column_list);
        length_table.set_value_list(length_etch_list);
        ProcessTable2D width_table;
        width_table.set_row_list(row_list);
        width_table.set_column_list(column_list);
        width_table.set_value_list(width_etch_list);
        ProcessViaEtchTable etch_table;
        etch_table.set_effect_type(getITFEffectType(itf_token_list, token_idx + 1, block_start_idx));
        etch_table.set_length_table(length_table);
        etch_table.set_width_table(width_table);
        via.get_etch_table_list().push_back(std::move(etch_table));
      }
    }
    token_idx = block_end_idx + 1;
  }

  registerProcessLayer(via.get_layer_name());
  corner_data.get_process_via_list().push_back(std::move(via));
}

void DataManager::registerProcessLayer(std::string& process_layer_name)
{
  LayerTable& layer_table = _database.get_layer_table();
  if (layer_table.get_process_name_to_idx_map().count(process_layer_name) != 0) {
    return;
  }
  int32_t process_layer_idx = static_cast<int32_t>(layer_table.get_process_name_to_idx_map().size());
  layer_table.register_process_layer(process_layer_idx, process_layer_name);
}

void DataManager::getITFTokenList(std::string& itf_text, std::vector<std::string>& itf_token_list)
{
  itf_token_list.clear();
  std::string token;

  for (int32_t char_idx = 0; char_idx < static_cast<int32_t>(itf_text.size());) {
    char current_char = itf_text[char_idx];
    if (current_char == '#') {
      appendITFToken(token, itf_token_list);
      while (char_idx < static_cast<int32_t>(itf_text.size()) && itf_text[char_idx] != '\n') {
        char_idx++;
      }
      continue;
    }
    if (current_char == '/' && char_idx + 1 < static_cast<int32_t>(itf_text.size()) && itf_text[char_idx + 1] == '/') {
      appendITFToken(token, itf_token_list);
      char_idx += 2;
      while (char_idx < static_cast<int32_t>(itf_text.size()) && itf_text[char_idx] != '\n') {
        char_idx++;
      }
      continue;
    }
    if (std::isspace(static_cast<unsigned char>(current_char))) {
      appendITFToken(token, itf_token_list);
      char_idx++;
      continue;
    }
    if (current_char == '{' || current_char == '}' || current_char == '=' || current_char == '(' || current_char == ')' || current_char == ',') {
      appendITFToken(token, itf_token_list);
      itf_token_list.emplace_back(1, current_char);
      char_idx++;
      continue;
    }
    if (current_char == '\"') {
      appendITFToken(token, itf_token_list);
      char_idx++;
      while (char_idx < static_cast<int32_t>(itf_text.size()) && itf_text[char_idx] != '\"') {
        token.push_back(itf_text[char_idx]);
        char_idx++;
      }
      appendITFToken(token, itf_token_list);
      if (char_idx < static_cast<int32_t>(itf_text.size())) {
        char_idx++;
      }
      continue;
    }
    token.push_back(current_char);
    char_idx++;
  }
  appendITFToken(token, itf_token_list);
}

void DataManager::appendITFToken(std::string& token, std::vector<std::string>& itf_token_list)
{
  if (!token.empty()) {
    itf_token_list.push_back(token);
    token.clear();
  }
}

int32_t DataManager::getITFBlockStart(std::vector<std::string>& itf_token_list, int32_t start_idx)
{
  for (int32_t token_idx = start_idx; token_idx < static_cast<int32_t>(itf_token_list.size()); ++token_idx) {
    if (itf_token_list[token_idx] == "{") {
      return token_idx;
    }
    if (itf_token_list[token_idx] == "}") {
      return -1;
    }
  }
  return -1;
}

int32_t DataManager::getITFBlockEnd(std::vector<std::string>& itf_token_list, int32_t block_start_idx)
{
  if (block_start_idx >= static_cast<int32_t>(itf_token_list.size()) || itf_token_list[block_start_idx] != "{") {
    return -1;
  }
  int32_t depth = 0;
  for (int32_t token_idx = block_start_idx; token_idx < static_cast<int32_t>(itf_token_list.size()); ++token_idx) {
    if (itf_token_list[token_idx] == "{") {
      depth++;
    } else if (itf_token_list[token_idx] == "}") {
      depth--;
      if (depth == 0) {
        return token_idx;
      }
    }
  }
  return -1;
}

bool DataManager::getITFAssignmentNumber(std::vector<std::string>& itf_token_list, int32_t property_idx, double& property_value)
{
  if (property_idx + 2 >= static_cast<int32_t>(itf_token_list.size()) || itf_token_list[property_idx + 1] != "=") {
    return false;
  }
  return RCXUTIL.getDouble(itf_token_list[property_idx + 2], property_value);
}

bool DataManager::getITFAssignmentString(std::vector<std::string>& itf_token_list, int32_t property_idx, std::string& property_value)
{
  if (property_idx + 2 >= static_cast<int32_t>(itf_token_list.size()) || itf_token_list[property_idx + 1] != "=") {
    return false;
  }
  property_value = itf_token_list[property_idx + 2];
  return true;
}

void DataManager::getITFNumberList(std::vector<std::string>& itf_token_list, int32_t start_idx, int32_t end_idx, std::vector<double>& number_list)
{
  number_list.clear();
  for (int32_t token_idx = start_idx; token_idx < end_idx; ++token_idx) {
    double number = 0.0;
    if (RCXUTIL.getDouble(itf_token_list[token_idx], number)) {
      number_list.push_back(number);
    }
  }
}

ProcessEffectType DataManager::getITFEffectType(std::vector<std::string>& itf_token_list, int32_t start_idx, int32_t end_idx)
{
  for (int32_t token_idx = start_idx; token_idx < end_idx; ++token_idx) {
    ProcessEffectType effect_type = GetProcessEffectType()(RCXUTIL.getUpperString(itf_token_list[token_idx]));
    if (effect_type != ProcessEffectType::kNone) {
      return effect_type;
    }
  }
  return ProcessEffectType::kBoth;
}

void DataManager::getITFTableValueList(std::vector<std::string>& itf_token_list, int32_t start_idx, int32_t end_idx, std::string row_name,
                                       std::string column_name, std::string value_name, std::vector<double>& row_list, std::vector<double>& column_list,
                                       std::vector<double>& value_list)
{
  row_list.clear();
  column_list.clear();
  value_list.clear();
  row_name = RCXUTIL.getUpperString(row_name);
  column_name = RCXUTIL.getUpperString(column_name);
  value_name = RCXUTIL.getUpperString(value_name);

  for (int32_t token_idx = start_idx; token_idx < end_idx;) {
    std::string property_name = RCXUTIL.getUpperString(itf_token_list[token_idx]);
    int32_t block_start_idx = getITFBlockStart(itf_token_list, token_idx + 1);
    if (block_start_idx == -1 || block_start_idx >= end_idx) {
      token_idx++;
      continue;
    }
    int32_t block_end_idx = getITFBlockEnd(itf_token_list, block_start_idx);
    if (block_end_idx == -1 || block_end_idx > end_idx) {
      token_idx++;
      continue;
    }
    if (property_name == row_name) {
      getITFNumberList(itf_token_list, block_start_idx + 1, block_end_idx, row_list);
    } else if (property_name == column_name) {
      getITFNumberList(itf_token_list, block_start_idx + 1, block_end_idx, column_list);
    } else if (property_name == value_name) {
      getITFNumberList(itf_token_list, block_start_idx + 1, block_end_idx, value_list);
    }
    token_idx = block_end_idx + 1;
  }
}

void DataManager::buildCapTable(CornerData& corner_data, std::string captab_file_path)
{
  std::ifstream* captab_file_stream = RCXUTIL.getInputFileStream(captab_file_path);
  if (!captab_file_stream->is_open()) {
    RCXUTIL.closeFileStream(captab_file_stream);
    return;
  }

  std::string header;
  std::vector<std::string> data_line_list;
  std::string line;
  while (std::getline(*captab_file_stream, line)) {
    line = RCXUTIL.getTrimmedString(line);
    if (line.empty() || line[0] == '#') {
      continue;
    }

    bool is_header = line.size() > 2 && (line[0] == 'A' || line[0] == 'B') && line[1] == ' ' && line.find("OVER") != std::string::npos;
    if (is_header) {
      if (!header.empty()) {
        buildCapTableConfig(corner_data, header, data_line_list);
      }
      header = line;
      data_line_list.clear();
    } else if (!header.empty()) {
      data_line_list.push_back(line);
    }
  }
  if (!header.empty()) {
    buildCapTableConfig(corner_data, header, data_line_list);
  }
  RCXUTIL.closeFileStream(captab_file_stream);
}

void DataManager::buildCapTableConfig(CornerData& corner_data, const std::string& header, const std::vector<std::string>& data_line_list)
{
  std::istringstream header_stream(header);
  std::string type_name;
  std::string layer_name;
  std::string over_keyword;
  std::string over_layer_name;
  if (!(header_stream >> type_name >> layer_name >> over_keyword >> over_layer_name)) {
    return;
  }

  CapTableConfig cap_table_config;
  cap_table_config.set_type(GetCapTableType()(type_name));
  cap_table_config.set_layer_name(layer_name);
  cap_table_config.set_over_layer_name(over_layer_name);

  std::string under_keyword;
  std::string under_layer_name;
  if (header_stream >> under_keyword >> under_layer_name && under_keyword == "UNDER") {
    cap_table_config.set_under_layer_name(under_layer_name);
  }

  for (const std::string& data_line : data_line_list) {
    std::istringstream data_stream(data_line);
    double spacing = 0.0;
    double coupling_capacitance = 0.0;
    double ground_capacitance = 0.0;
    if (!(data_stream >> spacing >> coupling_capacitance >> ground_capacitance)) {
      continue;
    }

    CapTableEntry cap_table_entry;
    cap_table_entry.set_spacing(spacing);
    cap_table_entry.set_coupling_capacitance(coupling_capacitance);
    cap_table_entry.set_ground_capacitance(ground_capacitance);
    cap_table_config.get_entry_list().push_back(std::move(cap_table_entry));
  }
  if (!cap_table_config.get_entry_list().empty()) {
    corner_data.get_cap_table_config_list().push_back(std::move(cap_table_config));
  }
}

void DataManager::buildLayerMapping()
{
  std::ifstream* mapping_file_stream = RCXUTIL.getInputFileStream(_config.mapping_file_path);
  if (!mapping_file_stream->is_open()) {
    RCXUTIL.closeFileStream(mapping_file_stream);
    return;
  }

  std::string line;
  while (std::getline(*mapping_file_stream, line)) {
    std::istringstream mapping_stream(line);
    std::string design_layer_name;
    std::string process_layer_name;
    if (!(mapping_stream >> design_layer_name >> process_layer_name)) {
      continue;
    }
    _database.get_layer_table().register_mapping(design_layer_name, process_layer_name);
  }
  RCXUTIL.closeFileStream(mapping_file_stream);
}

void DataManager::printConfig()
{
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(0), "RCX_CONFIG_INPUT");
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(1), "config_file_path");
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(2), _config.config_file_path);
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(1), "thread_number");
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(2), _config.thread_number);
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(1), "output_directory_path");
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(2), _config.output_directory_path);
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(1), "report_geometry");
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(2), _config.report_geometry);
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(1), "mapping_file_path");
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(2), _config.mapping_file_path);
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(1), "corner_list");
  for (Corner& corner : _config.corner_list) {
    RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(2), "corner_name");
    RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(3), corner.get_corner_name());
    RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(2), "tmpr_list");
    for (double tmpr : corner.get_tmpr_list()) {
      RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(3), tmpr);
    }
    RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(2), "itf_file_path");
    RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(3), corner.get_itf_file_path());
    RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(2), "captab_file_path");
    RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(3), corner.get_captab_file_path());
  }
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(0), "RCX_CONFIG_BUILD");
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(1), "temp_directory_path");
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(2), _config.temp_directory_path);
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(1), "log_file_path");
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(2), _config.log_file_path);
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(1), "DataManager");
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(2), "dm_temp_directory_path");
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(3), _config.dm_temp_directory_path);
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(1), "TopoBuilder");
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(2), "tb_temp_directory_path");
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(3), _config.tb_temp_directory_path);
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(1), "EnvBuilder");
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(2), "eb_temp_directory_path");
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(3), _config.eb_temp_directory_path);
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(1), "VarProcessor");
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(2), "vp_temp_directory_path");
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(3), _config.vp_temp_directory_path);
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(1), "ResExtractor");
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(2), "re_temp_directory_path");
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(3), _config.re_temp_directory_path);
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(1), "CapExtractor");
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(2), "ce_temp_directory_path");
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(3), _config.ce_temp_directory_path);
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(1), "SPEFWriter");
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(2), "sw_temp_directory_path");
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(3), _config.sw_temp_directory_path);
}

void DataManager::printDatabase()
{
  RCXLOG.info(Loc::current(), "Database");
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(1), "design_name");
  RCXLOG.info(Loc::current(), RCXUTIL.getSpaceByTabNum(2), _database.get_design_name());
}

#endif

}  // namespace ircx
