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
 * @file TestArtifactIOTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-07-30
 * @brief Tests for deterministic test artifact output handling.
 */

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

#include "toolkit/io/TestArtifactIO.hh"

namespace icts_test {
namespace {

TEST(TestArtifactIOTest, WritesExactArtifactWithoutCreatingRuntimeLog)
{
  const auto output_dir = toolkit::io::PrepareCleanOutputDir(toolkit::io::ResolveOutputDir() / "artifact_io");
  ASSERT_FALSE(output_dir.empty());
  const auto artifact_path = output_dir / "summary.txt";
  ASSERT_TRUE(toolkit::io::WriteTextArtifact(artifact_path, "clock_count=2\n"));

  std::ifstream input_stream(artifact_path);
  std::ostringstream content;
  content << input_stream.rdbuf();
  EXPECT_EQ(content.str(), "clock_count=2\n");
  std::size_t regular_file_count = 0U;
  for (const auto& entry : std::filesystem::directory_iterator(output_dir)) {
    regular_file_count += entry.is_regular_file() ? 1U : 0U;
  }
  EXPECT_EQ(regular_file_count, 1U);
}

TEST(TestArtifactIOTest, SanitizesArtifactDirectoryComponents)
{
  EXPECT_EQ(toolkit::io::SanitizeOutputName("Clock / Domain #1"), "clock_domain_1");
  EXPECT_EQ(toolkit::io::SanitizeOutputName("***"), "unnamed");
}

}  // namespace
}  // namespace icts_test
