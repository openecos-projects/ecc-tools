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
 * @file Logger.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-07-29
 * @brief iRT-style runtime logger implementation for iCTS.
 */

#include "Logger.hh"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <functional>
#include <iostream>
#include <string_view>
#include <system_error>
#include <thread>

namespace icts {
namespace {

constexpr const char* kColorEnd = "\033[0m";

auto getLevelName(LogLevel log_level) -> const char*
{
  switch (log_level) {
    case LogLevel::kInfo:
      return "Info";
    case LogLevel::kWarn:
      return "Warn";
    case LogLevel::kError:
      return "Error";
    case LogLevel::kNone:
      return "None";
  }
  return "None";
}

auto getLevelColor(LogLevel log_level) -> const char*
{
  switch (log_level) {
    case LogLevel::kInfo:
      return "\033[1;34m";
    case LogLevel::kWarn:
      return "\033[1;33m";
    case LogLevel::kError:
      return "\033[1;31m";
    case LogLevel::kNone:
      return "\033[1;32m";
  }
  return "\033[1;32m";
}

auto getTimestamp() -> std::string
{
  const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::tm local_time{};
  localtime_r(&now, &local_time);
  std::array<char, 32> buffer{};
  std::strftime(buffer.data(), buffer.size(), "%Y%m%d %H:%M:%S", &local_time);
  return buffer.data();
}

auto compressBase62(std::uint64_t value) -> std::string
{
  constexpr std::string_view digits = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
  if (value == 0U) {
    return "0";
  }

  std::string result;
  while (value != 0U) {
    result.push_back(digits[value % 62U]);
    value /= 62U;
  }
  return result;
}

auto getThreadId() -> std::string
{
  const auto value = static_cast<std::uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
  return compressBase62(value);
}

auto getSourceName(LogLevel log_level, Loc location) -> std::string
{
  std::filesystem::path file_path(location.file_name());
  if (log_level == LogLevel::kError) {
    std::error_code error_code;
    const auto absolute_path = std::filesystem::absolute(file_path, error_code);
    if (!error_code) {
      file_path = absolute_path;
    }
  } else {
    file_path = file_path.filename();
  }
  return file_path.string() + ":" + std::to_string(location.line());
}

}  // namespace

std::unique_ptr<Logger> Logger::_instance;
std::mutex Logger::_instance_mutex;

void Logger::initInst()
{
  std::scoped_lock lock(_instance_mutex);
  if (_instance == nullptr) {
    _instance = std::make_unique<Logger>(ConstructionKey{});
  }
}

auto Logger::getInst() -> Logger&
{
  std::scoped_lock lock(_instance_mutex);
  if (_instance == nullptr) {
    _instance = std::make_unique<Logger>(ConstructionKey{});
  }
  return *_instance;
}

void Logger::destroyInst()
{
  std::scoped_lock lock(_instance_mutex);
  _instance = nullptr;
}

Logger::~Logger()
{
  closeLogFileStream();
}

void Logger::openLogFileStream(const std::string& log_file_path)
{
  bool open_succeeded = false;
  {
    std::scoped_lock lock(_state_mutex);
    if (_log_file.is_open()) {
      _log_file.flush();
      _log_file.close();
    }
    _log_file.clear();
    _log_file_path = log_file_path;
    _log_file.open(_log_file_path, std::ios::out | std::ios::trunc);
    open_succeeded = _log_file.is_open();
    if (open_succeeded) {
      for (const auto& buffered_log : _buffered_logs) {
        _log_file << buffered_log;
      }
      _buffered_logs.clear();
      _log_file.flush();
    }
  }

  if (!open_succeeded) {
    error(Loc::current(), "Unable to open CTS log file '", log_file_path, "'.");
  }
}

void Logger::closeLogFileStream()
{
  std::scoped_lock lock(_state_mutex);
  if (_log_file.is_open()) {
    _log_file.flush();
    _log_file.close();
  }
  _log_file.clear();
}

void Logger::printLogFilePath()
{
  std::string log_file_path;
  {
    std::scoped_lock lock(_state_mutex);
    log_file_path = _log_file_path;
  }
  if (!log_file_path.empty()) {
    info(Loc::current(), "The log file path is '", log_file_path, "'!");
  }
}

void Logger::writeLog(LogLevel log_level, Loc location, const std::string& message)
{
  const auto prefix = std::string{"[CTS "} + getTimestamp() + " " + getThreadId() + " " + getSourceName(log_level, location) + " ";
  const auto suffix = std::string{" "} + location.function_name() + "] " + message + "\n";
  const auto level_name = std::string{getLevelName(log_level)};
  const auto plain_log = prefix + level_name + suffix;
  const auto color_log = prefix + getLevelColor(log_level) + level_name + kColorEnd + suffix;

  std::scoped_lock lock(_state_mutex);
  if (_log_file.is_open()) {
    _log_file << plain_log;
    _log_file.flush();
  } else {
    _buffered_logs.push_back(plain_log);
  }
  std::cout << color_log;
  std::cout.flush();
}

}  // namespace icts
