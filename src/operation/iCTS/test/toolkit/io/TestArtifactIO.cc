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
 * @file TestArtifactIO.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-07-30
 * @brief Test artifact path resolution, sanitization, and report emission.
 */

#include "toolkit/io/TestArtifactIO.hh"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <system_error>

#include "Logger.hh"
#include "toolkit/io/InfoReport.hh"

namespace icts_test::toolkit::io {
namespace {

auto ResolveExecutableDir() -> std::filesystem::path
{
  std::error_code error_code;
  const auto executable_path = std::filesystem::canonical("/proc/self/exe", error_code);
  return error_code || executable_path.empty() ? std::filesystem::path{} : executable_path.parent_path();
}

}  // namespace

auto WriteTextArtifact(const std::filesystem::path& path, const std::string& content) -> bool
{
  std::error_code error_code;
  if (!path.parent_path().empty()) {
    std::filesystem::create_directories(path.parent_path(), error_code);
    if (error_code) {
      return false;
    }
  }

  std::ofstream output_stream(path);
  if (!output_stream.is_open()) {
    return false;
  }
  output_stream << content;
  return output_stream.good();
}

auto EmitInfoReport(const InfoReport& report) -> void
{
  CTSLOG.info(icts::Loc::current(), report.title.empty() ? "CTS test result" : report.title, ":\n", report.content);
}

auto SanitizeOutputName(const std::string& raw_name) -> std::string
{
  std::string sanitized;
  sanitized.reserve(raw_name.size());
  bool previous_was_separator = false;
  for (const char value : raw_name) {
    const auto character = static_cast<unsigned char>(value);
    if (std::isalnum(character) != 0) {
      sanitized.push_back(static_cast<char>(std::tolower(character)));
      previous_was_separator = false;
    } else if (!previous_was_separator && !sanitized.empty()) {
      sanitized.push_back('_');
      previous_was_separator = true;
    }
  }
  while (!sanitized.empty() && sanitized.back() == '_') {
    sanitized.pop_back();
  }
  return sanitized.empty() ? "unnamed" : sanitized;
}

auto PrepareCleanOutputDir(const std::filesystem::path& path) -> std::filesystem::path
{
  std::error_code error_code;
  std::filesystem::remove_all(path, error_code);
  if (error_code) {
    return {};
  }
  std::filesystem::create_directories(path, error_code);
  return error_code ? std::filesystem::path{} : path;
}

auto ResolveOutputDir() -> std::filesystem::path
{
  const char* env_dir = std::getenv("ICTS_TEST_OUTPUT_DIR");
  if (env_dir != nullptr && *env_dir != '\0') {
    return {env_dir};
  }
  const auto executable_dir = ResolveExecutableDir();
  return executable_dir.empty() ? std::filesystem::path{"icts_test_output"} : executable_dir / "icts_test_output";
}

auto ResolveTopologyGenOutputDir() -> std::filesystem::path
{
  return ResolveOutputDir() / "topology_gen";
}

auto ResolveClusteringOutputDir() -> std::filesystem::path
{
  return ResolveOutputDir() / "clustering";
}

}  // namespace icts_test::toolkit::io
