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
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#pragma once

#include "Logger.hpp"
#include "StringUtils.hh"

namespace ircx::path {

inline bool fileExists(const std::filesystem::path& file_path, std::string_view field_name)
{
  if (!string::requireNonEmpty(file_path.string(), field_name)) {
    return false;
  }
  if (std::filesystem::exists(file_path)) {
    return true;
  }
  RCXLOG.warn(Loc::current(), "RCX file not found for ", field_name, ": ", file_path.string());
  return false;
}

inline bool ensureDir(const std::filesystem::path& directory_path, std::string_view field_name)
{
  if (!string::requireNonEmpty(directory_path.string(), field_name)) {
    return false;
  }
  std::error_code error_code;
  std::filesystem::create_directories(directory_path, error_code);
  if (error_code || !std::filesystem::is_directory(directory_path)) {
    RCXLOG.warn(Loc::current(), "Failed to create RCX directory for ", field_name, ": ", directory_path.string());
    return false;
  }
  return true;
}

inline std::filesystem::path fileUnderDir(const std::filesystem::path& directory_path, std::string_view stem, std::string_view extension)
{
  std::string file_name(stem.empty() ? "output" : stem);
  if (!extension.empty() && extension.front() != '.') {
    file_name.push_back('.');
  }
  file_name.append(extension);
  return directory_path / file_name;
}

inline std::string stemOr(std::string_view file_path, std::string_view fallback)
{
  std::string stem = std::filesystem::path(std::string(file_path)).stem().string();
  return stem.empty() ? std::string(fallback) : stem;
}

}  // namespace ircx::path
