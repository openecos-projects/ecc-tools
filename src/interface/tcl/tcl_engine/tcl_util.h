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
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <iomanip>

#include "ScriptEngine.hh"
#include "json.hpp"

#include "utility/logger/Logger.hpp"
using ordered_json = nlohmann::ordered_json;

namespace tcl {

using ecc::ScriptEngine;
using ecc::TclCmd;
using ecc::TclCmds;
using ecc::TclDoubleListOption;
using ecc::TclDoubleOption;
using ecc::TclIntListOption;
using ecc::TclIntOption;
using ecc::TclOption;
using ecc::TclStringListOption;
using ecc::TclStringListListOption;
using ecc::TclStringListListListOption;
using ecc::TclStringListListListListOption;
using ecc::TclStringOption;
using ecc::TclSwitchOption;

enum class ValueType
{
  kNone,
  kInt,
  kIntList,
  kDouble,
  kDoubleList,
  kString,
  kStringList,
  kStringListList,
  kStringListListList,
  kStringListListListList,
  kStringDoubleMap
};

class TclUtil : public TclCmd
{
 public:
  explicit TclUtil(const char* cmd_name) : TclCmd(cmd_name) {}
  ~TclUtil() override = default;
  unsigned check() override { return 1; };
  unsigned exec() override { return 1; };

  static void addOption(TclCmd* tcl_ptr, std::vector<std::pair<std::string, ValueType>> config_list);
  static void addOption(TclCmd* tcl_ptr, std::string config_name, ValueType type);
  static std::map<std::string, std::any> getConfigMap(TclCmd* tcl_ptr, std::vector<std::pair<std::string, ValueType>> config_list);
  static std::any getValue(TclCmd* tcl_ptr, std::string config_name, ValueType type);
  static bool alterJsonConfig(std::string json_path, std::map<std::string, std::any> config_map);

 private:
  static void modifyJson(ordered_json& j, const std::map<std::string, std::any>& config_map) {
    for (auto& [key, value] : config_map) {
      // Remove the first character '-' from the parameter list.
      std::string sub_key = key.substr(1, key.size() - 1);
      if (!modifyJsonValue(j, sub_key, value)) {
        ECCLOG.warn(ecc::Loc::current(), "The key is not found.", sub_key);
      }
    }
  }

  static bool modifyJsonValue(ordered_json& j, const std::string& key, const std::any& value) {
    bool modified = false;
    for (auto& item : j.items()) {
      if (item.key() == key) {
        try {
          if (value.type() == typeid(int)) {
            item.value() = std::any_cast<int>(value);
            modified = true;
          }else if (value.type() == typeid(std::string)){
            item.value() = std::any_cast<std::string>(value);
            modified = true;
          }else if (value.type() == typeid(double)){
            item.value() = std::any_cast<double>(value);
            modified = true;
          }else if (value.type() == typeid(std::vector<int>)){
            item.value() = std::any_cast<std::vector<int>>(value);
            modified = true;
          }else if (value.type() == typeid(std::vector<std::string>)){
            item.value() = std::any_cast<std::vector<std::string>>(value);
            modified = true;
          }else if (value.type() == typeid(std::vector<std::vector<std::string>>)){
            item.value() = std::any_cast<std::vector<std::vector<std::string>>>(value);
            modified = true;
          }
        } catch (const std::bad_any_cast& e) {
            ECCLOG.warn(ecc::Loc::current(), "Type trans error: ", e.what());
        }
        break;
      } else if (item.value().is_object()) {
        modified = modifyJsonValue(item.value(), key, value) || modified;
      }
    }
    return modified;
  }

  static std::vector<std::string> splitString(std::string a, char tok)
  {
    std::vector<std::string> result_list;
    while (true) {
      size_t pos = a.find(tok);
      if (std::string::npos == pos) {
        result_list.push_back(a);
        break;
      }
      result_list.push_back(a.substr(0, pos));
      a = a.substr(pos + 1);
    }
    return result_list;
  }
};

}  // namespace tcl
