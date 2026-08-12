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
 * @file QORReportTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-07-30
 * @brief Verifies that canonical QoR report tables are mirrored exactly to cts.log.
 */

#include <gtest/gtest.h>
#include <unistd.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "Logger.hh"
#include "output/qor/QORReport.hh"
#include "stage/StageSummary.hh"

namespace icts {
namespace {

auto readReportTableLines(const std::filesystem::path& path) -> std::vector<std::string>
{
  std::ifstream stream(path);
  std::vector<std::string> lines;
  std::string line;
  (void) std::getline(stream, line);
  while (std::getline(stream, line)) {
    lines.push_back(line);
  }
  return lines;
}

auto readLogMessages(const std::filesystem::path& path) -> std::vector<std::string>
{
  std::ifstream stream(path);
  std::vector<std::string> messages;
  std::string line;
  while (std::getline(stream, line)) {
    const auto message_begin = line.find("] ");
    if (message_begin != std::string::npos) {
      messages.push_back(line.substr(message_begin + 2U));
    }
  }
  return messages;
}

auto expectCanonicalTableInLog(const std::vector<std::string>& report_lines, const std::vector<std::string>& log_messages, const std::string& title) -> void
{
  ASSERT_FALSE(report_lines.empty());
  const auto title_line = std::ranges::find_if(report_lines, [&title](const std::string& line) -> bool { return line.find(title) != std::string::npos; });
  ASSERT_NE(title_line, report_lines.end());
  EXPECT_EQ(std::ranges::count(log_messages, *title_line), 1U);

  const auto match = std::ranges::search(log_messages, report_lines);
  EXPECT_NE(match.begin(), log_messages.end());
}

TEST(QorReportTest, MirrorsEachCanonicalReportTableToCtsLogExactlyOnce)
{
  const auto output_dir = std::filesystem::temp_directory_path() / ("icts_qor_report_test_" + std::to_string(getpid()));
  std::filesystem::remove_all(output_dir);
  std::filesystem::create_directories(output_dir);
  const auto log_path = output_dir / "cts.log";

  EvaluationState state;
  state.statistics.valid = true;
  state.statistics.top_wirelength_um = 10.25;
  state.statistics.trunk_wirelength_um = 20.5;
  state.statistics.leaf_wirelength_um = 30.75;
  state.statistics.total_wirelength_um = 61.5;
  state.statistics.max_net_wirelength_um = 12.125;
  state.statistics.hpwl_top_wirelength_um = 9.0;
  state.statistics.hpwl_trunk_wirelength_um = 18.0;
  state.statistics.hpwl_leaf_wirelength_um = 27.0;
  state.statistics.hpwl_total_wirelength_um = 54.0;
  state.statistics.hpwl_max_net_wirelength_um = 11.0;
  state.statistics.cell_stats.emplace("buffer", QorCellStats{.count = 4U, .total_area_um2 = 8.5, .total_cap_pf = 0.42});
  state.statistics.lib_cell_dist.emplace("CLKBUF_X2", QorLibCellDistribution{.cell_type = "buffer", .count = 4U, .total_area_um2 = 8.5});

  CTSLOG.openLogFileStream(log_path.string());
  ASSERT_TRUE(QorReport::write(state, output_dir.string()));
  CTSLOG.closeLogFileStream();

  const auto log_messages = readLogMessages(log_path);
  expectCanonicalTableInLog(readReportTableLines(output_dir / "wirelength.rpt"), log_messages, "Wirelength Statistics");
  expectCanonicalTableInLog(readReportTableLines(output_dir / "cell_stats.rpt"), log_messages, "Cell Stats");
  expectCanonicalTableInLog(readReportTableLines(output_dir / "lib_cell_dist.rpt"), log_messages, "Library Cell Distribution");

  std::filesystem::remove_all(output_dir);
}

}  // namespace
}  // namespace icts
