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
 * @file LogTableTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-07-30
 * @brief Tests for canonical ASCII table rendering and Logger emission.
 */

#include <gtest/gtest.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "LogTable.hh"
#include "Logger.hh"

namespace icts {
namespace {

auto readLogTableFile(const std::filesystem::path& path) -> std::string
{
  std::ifstream stream(path);
  return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

TEST(LogTableTest, RendersOneCanonicalTitleHeaderAndAlignedRows)
{
  const auto text = RenderLogTable("RC Summary", {"Item", "Value"}, {{"resistance", "0.914 Ohm/um"}, {"status", "ok"}});

  EXPECT_EQ(text.find("RC Summary"), text.rfind("RC Summary"));
  EXPECT_NE(text.find("| Item       | Value        |"), std::string::npos);
  EXPECT_NE(text.find("| resistance | 0.914 Ohm/um |"), std::string::npos);
  EXPECT_NE(text.find("| status     | ok           |"), std::string::npos);
  EXPECT_EQ(text.back(), '+');
}

TEST(LogTableTest, EmitsEveryCanonicalLineThroughTheSingleLogger)
{
  const auto output_dir = std::filesystem::temp_directory_path() / ("icts_log_table_test_" + std::to_string(getpid()));
  std::filesystem::remove_all(output_dir);
  std::filesystem::create_directories(output_dir);
  const auto log_path = output_dir / "cts.log";

  Logger::destroyInst();
  Logger::initInst();
  CTSLOG.openLogFileStream(log_path.string());
  EmitLogTable(Loc::current(), "Clock Summary", {"Clock", "Sinks"}, {{"core_clock", "8751"}});
  CTSLOG.closeLogFileStream();
  Logger::destroyInst();

  const auto file_text = readLogTableFile(log_path);
  EXPECT_EQ(file_text.find("Clock Summary"), file_text.rfind("Clock Summary"));
  EXPECT_NE(file_text.find("| Clock      | Sinks |"), std::string::npos);
  EXPECT_NE(file_text.find("| core_clock | 8751  |"), std::string::npos);
  EXPECT_EQ(file_text.find("\033["), std::string::npos);

  std::filesystem::remove_all(output_dir);
}

}  // namespace
}  // namespace icts
