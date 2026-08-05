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
 * @file Logger.hh
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-07-29
 * @brief iRT-style runtime logger for iCTS.
 */

#pragma once

#include <cstdlib>
#include <experimental/source_location>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "LogLevel.hh"

namespace icts {

using Loc = std::experimental::source_location;

#define CTSLOG (icts::Logger::getInst())

class Logger final
{
  struct ConstructionKey
  {
  };

 public:
  explicit Logger([[maybe_unused]] ConstructionKey construction_key) {}

  static void initInst();
  static auto getInst() -> Logger&;
  static void destroyInst();

  void openLogFileStream(const std::string& log_file_path);
  void closeLogFileStream();
  void printLogFilePath();

  template <typename T, typename... Args>
  void info(Loc location, T&& value, Args&&... args)
  {
    printLog(LogLevel::kInfo, location, std::forward<T>(value), std::forward<Args>(args)...);
  }

  template <typename T, typename... Args>
  void warn(Loc location, T&& value, Args&&... args)
  {
    printLog(LogLevel::kWarn, location, std::forward<T>(value), std::forward<Args>(args)...);
  }

  template <typename T, typename... Args>
  [[noreturn]] void error(Loc location, T&& value, Args&&... args)
  {
    printLog(LogLevel::kError, location, std::forward<T>(value), std::forward<Args>(args)...);
    closeLogFileStream();
    std::exit(EXIT_FAILURE);
  }

  Logger(const Logger& other) = delete;
  Logger(Logger&& other) = delete;
  auto operator=(const Logger& other) -> Logger& = delete;
  auto operator=(Logger&& other) -> Logger& = delete;

 private:
  friend struct std::default_delete<Logger>;

  ~Logger();

  template <typename T, typename... Args>
  void printLog(LogLevel log_level, Loc location, T&& value, Args&&... args)
  {
    writeLog(log_level, location, buildString(std::forward<T>(value), std::forward<Args>(args)...));
  }

  template <typename... Args>
  static auto buildString(Args&&... args) -> std::string
  {
    std::ostringstream stream;
    (stream << ... << std::forward<Args>(args));
    return stream.str();
  }

  void writeLog(LogLevel log_level, Loc location, const std::string& message);

  static std::unique_ptr<Logger> _instance;
  static std::mutex _instance_mutex;

  std::mutex _state_mutex;
  std::string _log_file_path;
  std::ofstream _log_file;
  std::vector<std::string> _buffered_logs;
};

}  // namespace icts
