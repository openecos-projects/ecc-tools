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
/**
 * @File Name: data_config.cpp
 * @Brief :
 * @Author : Yell (12112088@qq.com)
 * @Version : 1.0
 * @Creat Date : 2022-04-15
 *
 */

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "utility/logger/Logger.hpp"
#include "dm_config.h"

#include <stdio.h>
#include <stdlib.h>

#include <iostream>

#include "json_parser.h"

namespace idm {
/**
 * @Brief : init data config from config file in config_path
 * @param  config_path config file path
 * @return true init config success
 * @return false init config failed
 */
bool DataConfig::initConfig(string config_path)
{
  if (checkFilePath(config_path)) {
    _config_path = config_path;
    ECCLOG.info(ecc::Loc::current(), "[Data config] begin config, path = ", _config_path);

    std::ifstream& config_stream = ecc::getInputFileStream(_config_path);

    {
      nlohmann::json json;
      config_stream >> json;

      set_tech_lef_path(ecc::getJsonData(json, {"INPUT", "tech_lef_path"}));
      vector<string> lef_paths;
      for (string lef_path : ecc::getJsonData(json, {"INPUT", "lef_paths"})) {
        lef_paths.emplace_back(lef_path);
      }
      set_lef_paths(lef_paths);

      set_def_path(ecc::getJsonData(json, {"INPUT", "def_path"}));
      set_verilog_path(ecc::getJsonData(json, {"INPUT", "verilog_path"}));

      vector<string> lib_paths;
      for (std::string lib_path : ecc::getJsonData(json, {"INPUT", "lib_path"})) {
        lib_paths.emplace_back(lib_path);
      }
      set_lib_paths(lib_paths);

      set_sdc_path(ecc::getJsonData(json, {"INPUT", "sdc_path"}));
      set_spef_path(ecc::getJsonData(json, {"INPUT", "spef_path"}));
      set_vcd_path(ecc::getJsonData(json, {"INPUT", "vcd_path"}));

      set_output_path(ecc::getJsonData(json, {"OUTPUT", "output_dir_path"}));

      set_routing_layer_1st(ecc::getJsonData(json, {"LayerSettings", "routing_layer_1st"}));
    }

    ecc::closeFileStream(config_stream);

    ECCLOG.info(ecc::Loc::current(), "[Data config] end config");

    return true;
  }
  return false;
}

/**
 * @Brief : check file exist in path
 * @param  path
 * @return true
 * @return false
 */
bool DataConfig::checkFilePath(string path)
{
  FILE* file = fopen(path.c_str(), "r");
  if (file == nullptr) {
    ECCLOG.warn(ecc::Loc::current(), "[DataConfig error] : Can not open file = ", path);

    return false;
  }

  fclose(file);

  return true;
}

/**
 * @Brief : check all path
 * @return true
 * @return false
 */
bool DataConfig::checkAllFile()
{
  bool b_success = true;
  b_success &= checkFilePath(_tech_lef_path);

  for (auto& path : _lef_paths) {
    b_success &= checkFilePath(path);
  }

  b_success &= checkFilePath(_def_path);

  return b_success;
}

}  // namespace idm
