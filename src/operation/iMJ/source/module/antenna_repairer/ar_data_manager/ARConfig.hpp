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

#include <any>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>

#include "DataManager.hpp"
#include "Logger.hpp"

namespace imj {

struct ARConfig
{
  std::string explicit_diode;
  int max_iter = 3;
  double search_radius_um = 30.0;
  std::string report_dir;
  std::string report_path;
  std::string step_json_path;
  bool valid = true;
  std::string error_message;
};

namespace detail {

inline bool readOptionalString(const std::map<std::string, std::any>& config_map, const std::string& key,
                               std::string& out, bool& has_explicit_key)
{
  auto it = config_map.find(key);
  if (it == config_map.end() || !it->second.has_value()) {
    return true;
  }
  if (const auto* s = std::any_cast<std::string>(&it->second)) {
    has_explicit_key = true;
    out = *s;
    return true;
  }
  if (const auto* cs = std::any_cast<const char*>(&it->second)) {
    if (*cs == nullptr) {
      return false;
    }
    has_explicit_key = true;
    out = *cs;
    return true;
  }
  return false;
}

inline bool readIntStrict(const std::map<std::string, std::any>& config_map, const std::string& key, int& out,
                          std::string& err)
{
  auto it = config_map.find(key);
  if (it == config_map.end() || !it->second.has_value()) {
    return true;
  }
  if (const auto* v = std::any_cast<int>(&it->second)) {
    out = *v;
    return true;
  }
  if (const auto* v64 = std::any_cast<int64_t>(&it->second)) {
    if (*v64 < std::numeric_limits<int>::min() || *v64 > std::numeric_limits<int>::max()) {
      err = key + " integer overflow";
      return false;
    }
    out = static_cast<int>(*v64);
    return true;
  }
  if (const auto* s = std::any_cast<std::string>(&it->second)) {
    if (s->empty()) {
      err = key + " cannot be empty";
      return false;
    }
    try {
      size_t pos = 0;
      int parsed = std::stoi(*s, &pos);
      if (pos != s->size()) {
        err = "Invalid integer for " + key + ": " + *s;
        return false;
      }
      out = parsed;
      return true;
    } catch (const std::exception& ex) {
      err = "Invalid integer for " + key + " (" + *s + "): " + ex.what();
      return false;
    }
  }
  err = "Unsupported value type for " + key;
  return false;
}

inline bool readDoubleStrict(const std::map<std::string, std::any>& config_map, const std::string& key, double& out,
                             std::string& err)
{
  auto it = config_map.find(key);
  if (it == config_map.end() || !it->second.has_value()) {
    return true;
  }
  if (const auto* vd = std::any_cast<double>(&it->second)) {
    out = *vd;
    return std::isfinite(out);
  }
  if (const auto* vf = std::any_cast<float>(&it->second)) {
    out = static_cast<double>(*vf);
    return std::isfinite(out);
  }
  if (const auto* vi = std::any_cast<int>(&it->second)) {
    out = static_cast<double>(*vi);
    return true;
  }
  if (const auto* v64 = std::any_cast<int64_t>(&it->second)) {
    out = static_cast<double>(*v64);
    return true;
  }
  if (const auto* s = std::any_cast<std::string>(&it->second)) {
    if (s->empty()) {
      err = key + " cannot be empty";
      return false;
    }
    try {
      size_t pos = 0;
      double parsed = std::stod(*s, &pos);
      if (pos != s->size() || !std::isfinite(parsed)) {
        err = "Invalid number for " + key + ": " + *s;
        return false;
      }
      out = parsed;
      return true;
    } catch (const std::exception& ex) {
      err = "Invalid number for " + key + " (" + *s + "): " + ex.what();
      return false;
    }
  }
  err = "Unsupported value type for " + key;
  return false;
}

}  // namespace detail

inline ARConfig parseARConfig(const std::map<std::string, std::any>& config_map)
{
  ARConfig cfg;
  bool has_diode = false;
  if (!detail::readOptionalString(config_map, "-diode", cfg.explicit_diode, has_diode)) {
    cfg.valid = false;
    cfg.error_message = "Unsupported value type for -diode";
    MJLOG.warn(Loc::current(), cfg.error_message);
    return cfg;
  }

  if (!detail::readIntStrict(config_map, "-max_iter", cfg.max_iter, cfg.error_message)) {
    cfg.valid = false;
    MJLOG.warn(Loc::current(), cfg.error_message);
    return cfg;
  }
  if (cfg.max_iter < 1) {
    cfg.valid = false;
    cfg.error_message = "-max_iter must be >= 1";
    MJLOG.warn(Loc::current(), cfg.error_message);
    return cfg;
  }

  if (!detail::readDoubleStrict(config_map, "-search_radius_um", cfg.search_radius_um, cfg.error_message)) {
    cfg.valid = false;
    if (cfg.error_message.empty()) {
      cfg.error_message = "-search_radius_um must be a finite number";
    }
    MJLOG.warn(Loc::current(), cfg.error_message);
    return cfg;
  }
  if (!(cfg.search_radius_um > 0.0)) {
    cfg.valid = false;
    cfg.error_message = "-search_radius_um must be > 0";
    MJLOG.warn(Loc::current(), cfg.error_message);
    return cfg;
  }

  bool has_report_dir = false;
  if (!detail::readOptionalString(config_map, "-report_dir", cfg.report_dir, has_report_dir)) {
    cfg.valid = false;
    cfg.error_message = "Unsupported value type for -report_dir";
    MJLOG.warn(Loc::current(), cfg.error_message);
    return cfg;
  }

  bool has_report_path = false;
  if (!detail::readOptionalString(config_map, "-report_path", cfg.report_path, has_report_path)) {
    cfg.valid = false;
    cfg.error_message = "Unsupported value type for -report_path";
    MJLOG.warn(Loc::current(), cfg.error_message);
    return cfg;
  }

  bool has_step_json = false;
  if (!detail::readOptionalString(config_map, "-step_json", cfg.step_json_path, has_step_json)) {
    cfg.valid = false;
    cfg.error_message = "Unsupported value type for -step_json";
    MJLOG.warn(Loc::current(), cfg.error_message);
    return cfg;
  }

  if (!has_report_dir && !has_report_path && DataManager::isInitialized()) {
    cfg.report_dir = MJDM.getConfig().ar_temp_directory_path;
  }
  if (!has_report_path && !cfg.report_dir.empty()) {
    cfg.report_path = cfg.report_dir + "/antenna_repair.rpt";
  }

  return cfg;
}

}  // namespace imj
