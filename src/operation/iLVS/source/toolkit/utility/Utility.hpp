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

#include "Logger.hpp"
#include "LVSHeader.hpp"

namespace ilvs {

#define LVSUTIL (ilvs::Utility::getInst())

class Utility
{
 public:
  static void initInst();
  static Utility& getInst();
  static void destroyInst();
  // function

#if 1  // 字符串和配置工具函数

  template <typename T, typename... Args>
  static std::string getString(T value, Args... args)
  {
    std::stringstream oss;
    pushStream(oss, value, args...);
    std::string string = oss.str();
    oss.clear();
    return string;
  }

  template <typename Stream, typename T, typename... Args>
  static void pushStream(Stream* stream, T t, Args... args)
  {
    pushStream(*stream, t, args...);
  }

  template <typename Stream, typename T, typename... Args>
  static void pushStream(Stream& stream, T t, Args... args)
  {
    stream << t;
    pushStream(stream, args...);
  }

  template <typename Stream, typename T>
  static void pushStream(Stream& stream, T t)
  {
    stream << t;
  }

  static std::string formatSec(double sec)
  {
    std::string sec_string;

    int32_t integer_sec = static_cast<int32_t>(std::round(sec));
    int32_t h = integer_sec / 3600;
    int32_t m = (integer_sec % 3600) / 60;
    int32_t s = (integer_sec % 3600) % 60;
    char* buffer = new char[32];
    sprintf(buffer, "%02d:%02d:%02d", h, m, s);
    sec_string = buffer;
    delete[] buffer;
    buffer = nullptr;

    return sec_string;
  }
  static std::string formatByTwoDecimalPlaces(double digit)
  {
    std::string digit_string;

    char* buffer = new char[32];
    sprintf(buffer, "%02.2f", digit);
    digit_string = buffer;
    delete[] buffer;
    buffer = nullptr;

    return digit_string;
  }
  static std::string getIOName(const std::string& pin_name) { return "PIN/" + pin_name; }
  static std::string getInstancePinName(const std::string& instance_name, const std::string& pin_name)
  {
    return instance_name + "/" + pin_name;
  }
  static bool isIOName(const std::string& terminal_name) { return terminal_name.rfind("PIN/", 0) == 0; }
  static std::string getIOPinName(const std::string& terminal_name)
  {
    return isIOName(terminal_name) ? terminal_name.substr(std::string("PIN/").size()) : terminal_name;
  }
  template <typename T>
  static T getConfigValue(std::map<std::string, std::any>& config_map, const std::string& config_name, const T& default_value)
  {
    T value;
    if (exist(config_map, config_name)) {
      value = std::any_cast<T>(config_map[config_name]);
    } else {
      LVSLOG.warn(Loc::current(), "The config '", config_name, "' uses the default value!");
      value = default_value;
    }
    return value;
  }

#endif

#if 1  // 标准数据结构工具函数

  template <typename Key>
  static bool exist(const std::vector<Key>& vector, const Key& key)
  {
    for (size_t i = 0; i < vector.size(); i++) {
      if (vector[i] == key) {
        return true;
      }
    }
    return false;
  }

  template <typename T>
  static std::vector<T> getSortedUniqueList(std::vector<T> value_list)
  {
    std::sort(value_list.begin(), value_list.end());
    value_list.erase(std::unique(value_list.begin(), value_list.end()), value_list.end());
    return value_list;
  }

  template <typename TValue>
  static std::vector<std::string> getSortedKeyNameList(const std::map<std::string, TValue>& value_map)
  {
    std::vector<std::string> name_list;
    name_list.reserve(value_map.size());
    for (const auto& [name, value] : value_map) {
      (void) value;
      name_list.push_back(name);
    }
    return getSortedUniqueList(std::move(name_list));
  }

  template <typename Key, typename Compare = std::less<Key>>
  static bool exist(const std::set<Key, Compare>& set, const Key& key)
  {
    return (set.find(key) != set.end());
  }

  template <typename Key, typename Hash = std::hash<Key>>
  static bool exist(const std::unordered_set<Key, Hash>& set, const Key& key)
  {
    return (set.find(key) != set.end());
  }

  template <typename Key, typename Value, typename Compare = std::less<Key>>
  static bool exist(const std::map<Key, Value, Compare>& map, const Key& key)
  {
    return (map.find(key) != map.end());
  }

  template <typename Key, typename Value, typename Hash = std::hash<Key>>
  static bool exist(const std::unordered_map<Key, Value, Hash>& map, const Key& key)
  {
    return (map.find(key) != map.end());
  }

#endif

#if 1  // 文件工具函数

  static void createDirByFile(std::string file_path) { createDir(dirname((char*) file_path.c_str())); }

  static void createDir(std::string dir_path)
  {
    if (!std::filesystem::exists(dir_path)) {
      std::error_code system_error;
      if (!std::filesystem::create_directories(dir_path, system_error)) {
        LVSLOG.error(Loc::current(), "Failed to create directory '", dir_path, "', system_error:", system_error.message());
      }
    }
  }
  static void removeDir(const std::string& dir_path)
  {
    std::error_code system_error;

    // 检查文件夹是否存在
    if (std::filesystem::exists(dir_path, system_error)) {
      // 尝试删除文件夹
      if (!std::filesystem::remove_all(dir_path, system_error)) {
        LVSLOG.error(Loc::current(), "Failed to remove directory '", dir_path, "'. Error: ", system_error.message());
      }
    }
  }

  static std::string getSpaceByTabNum(int32_t tab_num)
  {
    std::string all = "";
    for (int32_t i = 0; i < tab_num; i++) {
      all += "  ";
    }
    return all;
  }

  static std::ifstream* getInputFileStream(std::string file_path) { return getFileStream<std::ifstream>(file_path); }

  static std::ofstream* getOutputFileStream(std::string file_path) { return getFileStream<std::ofstream>(file_path); }

  template <typename T>
  static T* getFileStream(std::string file_path)
  {
    T* file = new T(file_path);
    if (!file->is_open()) {
      LVSLOG.error(Loc::current(), "Failed to open file '", file_path, "'!");
    }
    return file;
  }

  template <typename T>
  static void closeFileStream(T* t)
  {
    if (t != nullptr) {
      t->close();
      delete t;
    }
  }

#endif

#if 1  // 表格工具函数

  static void printTableList(const std::vector<fort::char_table>& table_list)
  {
    std::vector<std::vector<std::string>> print_table_list;
    for (const fort::char_table& table : table_list) {
      if (!table.is_empty()) {
        print_table_list.push_back(splitString(table.to_string(), '\n'));
      }
    }
    if (print_table_list.empty()) {
      return;
    }

    int32_t max_size = INT_MIN;
    for (std::vector<std::string>& table : print_table_list) {
      max_size = std::max(max_size, static_cast<int32_t>(table.size()));
    }
    for (std::vector<std::string>& table : print_table_list) {
      for (int32_t i = static_cast<int32_t>(table.size()); i < max_size; i++) {
        std::string table_str;
        table_str.append(table.front().length(), ' ');
        table.push_back(table_str);
      }
    }
    for (int32_t i = 0; i < max_size; i++) {
      std::string table_str;
      for (std::vector<std::string>& table : print_table_list) {
        table_str += table[i];
        table_str += " ";
      }
      LVSLOG.info(Loc::current(), table_str);
    }
  }

  static std::vector<std::string> splitString(std::string string, char token)
  {
    std::vector<std::string> string_list;
    std::stringstream stream(string);
    std::string string_token;
    while (std::getline(stream, string_token, token)) {
      if (!string_token.empty()) {
        string_list.push_back(string_token);
      }
    }
    return string_list;
  }

#endif

 private:
  static Utility* _util_instance;

  Utility() = default;
  Utility(const Utility& other) = delete;
  Utility(Utility&& other) = delete;
  ~Utility() = default;
  Utility& operator=(const Utility& other) = delete;
  Utility& operator=(Utility&& other) = delete;
  // function
};

}  // namespace ilvs
