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
#include "RCXConfig.hh"

#include <exception>
#include <filesystem>
#include <fstream>
#include <string_view>

#include "PathUtils.hh"
#include "json/json.hpp"
#include "log/Log.hh"

namespace ircx {

namespace fs = std::filesystem;

namespace {

bool parse_corner_config(const nlohmann::json& corner_json,
                         const fs::path& config_dir,
                         std::string_view field_name,
                         RCXConfig::CornerConfig& corner_config)
{
  if (!corner_json.is_object()) {
    LOG_ERROR << "RCX config field must be an object: " << field_name;
    return false;
  }

  const std::string field_name_str(field_name);
  const std::string name_field = field_name_str + ".name";
  const std::string itf_field = field_name_str + ".itf_file";
  const std::string captab_field = field_name_str + ".captab_file";

  if (!corner_json.contains("name") || !corner_json["name"].is_string()) {
    LOG_ERROR << "RCX config missing string field: " << name_field;
    return false;
  }
  if (!corner_json.contains("itf_file") || !corner_json["itf_file"].is_string()) {
    LOG_ERROR << "RCX config missing string field: " << itf_field;
    return false;
  }
  if (!corner_json.contains("captab_file") || !corner_json["captab_file"].is_string()) {
    LOG_ERROR << "RCX config missing string field: " << captab_field;
    return false;
  }

  corner_config.name = trimCopy(corner_json["name"].get<std::string>());
  corner_config.itf_file = resolvePath(config_dir, corner_json["itf_file"].get<std::string>());
  corner_config.captab_file = resolvePath(config_dir, corner_json["captab_file"].get<std::string>());

  bool valid = true;
  valid &= ensureNonEmpty(corner_config.name, name_field);
  valid &= ensureFileExists(corner_config.itf_file, itf_field);
  valid &= ensureFileExists(corner_config.captab_file, captab_field);

  return valid;
}

}  // namespace

bool RCXConfig::loadFromFile(const std::string& config_path)
{
  if (config_path.empty()) {
    LOG_ERROR << "RCX config path is empty.";
    return false;
  }

  _config_path.clear();
  _thread_num = 64U;
  _operating_temperature = 25.0;
  _output_dir.clear();
  _mapping_file.clear();
  _corners.clear();

  std::ifstream config_stream(config_path);
  if (!config_stream.is_open()) {
    LOG_ERROR << "Cannot open RCX config file: " << config_path;
    return false;
  }

  try {
    nlohmann::json json;
    config_stream >> json;

    const fs::path absolute_config_path = fs::absolute(config_path).lexically_normal();
    const fs::path config_dir = absolute_config_path.parent_path();

    if (!json.contains("thread_num") || !json["thread_num"].is_number_integer()) {
      LOG_ERROR << "RCX config missing integer field: thread_num";
      return false;
    }
    const int thread_num = json["thread_num"].get<int>();
    _thread_num = thread_num <= 0 ? 1U : static_cast<unsigned>(thread_num);

    const char* temperature_field = nullptr;
    if (json.contains("temperature")) {
      temperature_field = "temperature";
    } else if (json.contains("operating_temperature")) {
      temperature_field = "operating_temperature";
    }

    if (temperature_field != nullptr) {
      if (!json[temperature_field].is_number()) {
        LOG_ERROR << "RCX config field must be a number: " << temperature_field;
        return false;
      }
      _operating_temperature = json[temperature_field].get<F64>();
    }

    if (json.contains("output") && json["output"].is_string()) {
      _output_dir = resolvePath(config_dir, json["output"].get<std::string>());
    } else {
      _output_dir.clear();
    }

    if (!json.contains("mapping_file") || !json["mapping_file"].is_string()) {
      LOG_ERROR << "RCX config missing string field: mapping_file";
      return false;
    }
    _mapping_file = resolvePath(config_dir, json["mapping_file"].get<std::string>());

    if (!json.contains("corners")) {
      LOG_ERROR << "RCX config missing field: corners";
      return false;
    }

    const auto& corners_json = json["corners"];
    bool valid = true;

    if (corners_json.is_array()) {
      if (corners_json.empty()) {
        LOG_ERROR << "RCX config field corners must not be empty.";
        return false;
      }

      _corners.reserve(corners_json.size());
      for (size_t idx = 0; idx < corners_json.size(); ++idx) {
        CornerConfig corner_config;
        valid &= parse_corner_config(corners_json[idx], config_dir,
                                     "corner[" + std::to_string(idx) + "]", corner_config);
        _corners.emplace_back(std::move(corner_config));
      }
    } else if (corners_json.is_object()) {
      CornerConfig corner_config;
      valid &= parse_corner_config(corners_json, config_dir, "corner", corner_config);
      _corners.emplace_back(std::move(corner_config));
    } else {
      LOG_ERROR << "RCX config field corners must be an object or array.";
      return false;
    }

    _config_path = absolute_config_path.string();

    valid &= ensureFileExists(_mapping_file, "mapping_file");

    return valid;
  } catch (const std::exception& e) {
    LOG_ERROR << "Failed to parse RCX config " << config_path << ": " << e.what();
    return false;
  }
}

}  // namespace ircx
