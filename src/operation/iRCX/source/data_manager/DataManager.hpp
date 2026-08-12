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
#pragma once

#include "Config.hpp"
#include "Database.hpp"

namespace ircx {

#define RCXDM (ircx::DataManager::getInst())

class DataManager
{
 public:
  static void initInst();
  static DataManager& getInst();
  static void destroyInst();
  // function
  void input(std::map<std::string, std::any>& config_map);
  void output();
  // getter
  Config& getConfig() { return _config; }
  Database& getDatabase() { return _database; }

 private:
  static DataManager* _dm_instance;
  // config & database
  Config _config;
  Database _database;

  DataManager() = default;
  DataManager(const DataManager& other) = delete;
  DataManager(DataManager&& other) = delete;
  ~DataManager() = default;
  DataManager& operator=(const DataManager& other) = delete;
  DataManager& operator=(DataManager&& other) = delete;

#if 1  // build
  void buildConfig();
  void buildDatabase();
  void buildCornerDataList();
  void buildCornerData(Corner& corner, double tmpr);
  std::string getTmprCornerName(std::string corner_name, double tmpr);
  void buildProcessCorner(CornerData& corner_data, std::string itf_file_path);
  void buildProcessConductor(CornerData& corner_data, std::vector<std::string>& itf_token_list, int32_t start_idx, int32_t end_idx,
                             std::string conductor_name);
  void buildProcessVia(CornerData& corner_data, std::vector<std::string>& itf_token_list, int32_t start_idx, int32_t end_idx,
                       std::string via_name);
  void registerProcessLayer(std::string& process_layer_name);
  void getITFTokenList(std::string& itf_text, std::vector<std::string>& itf_token_list);
  void appendITFToken(std::string& token, std::vector<std::string>& itf_token_list);
  int32_t getITFBlockStart(std::vector<std::string>& itf_token_list, int32_t start_idx);
  int32_t getITFBlockEnd(std::vector<std::string>& itf_token_list, int32_t block_start_idx);
  bool getITFAssignmentNumber(std::vector<std::string>& itf_token_list, int32_t property_idx, double& property_value);
  bool getITFAssignmentString(std::vector<std::string>& itf_token_list, int32_t property_idx, std::string& property_value);
  void getITFNumberList(std::vector<std::string>& itf_token_list, int32_t start_idx, int32_t end_idx, std::vector<double>& number_list);
  ProcessEffectType getITFEffectType(std::vector<std::string>& itf_token_list, int32_t start_idx, int32_t end_idx);
  void getITFTableValueList(std::vector<std::string>& itf_token_list, int32_t start_idx, int32_t end_idx, std::string row_name,
                            std::string column_name, std::string value_name, std::vector<double>& row_list,
                            std::vector<double>& column_list, std::vector<double>& value_list);
  void buildCapTable(CornerData& corner_data, std::string captab_file_path);
  void buildCapTableConfig(CornerData& corner_data, const std::string& header, const std::vector<std::string>& data_line_list);
  void buildLayerMapping();
  void printConfig();
  void printDatabase();
#endif
};

}  // namespace ircx
