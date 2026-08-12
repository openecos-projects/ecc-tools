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
 * @file TopologyRealTechArtifactAssertions.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-24
 * @brief Real-tech Topology artifact assertions.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "TopologyRealTechScenario.hh"
#include "module/synthesis/TopologyArtifactWriter.hh"
#include "synthesis/topology/Topology.hh"

namespace icts {
class Pin;
}  // namespace icts

namespace icts_test::synthesis_realtech_smoke {
namespace {

auto ReadTextFile(const std::filesystem::path& path) -> std::string
{
  std::ifstream input_stream(path);
  std::ostringstream buffer;
  buffer << input_stream.rdbuf();
  return buffer.str();
}

auto AssertLoggerContract(const std::string& cts_log_content) -> void
{
  EXPECT_NE(cts_log_content.find("[CTS "), std::string::npos);
  EXPECT_NE(cts_log_content.find("Topology smoke scenario:"), std::string::npos);
}

auto AssertReportContract(const std::string& report_content, bool sink_clustering_enabled) -> void
{
  EXPECT_NE(report_content.find("success=true\n"), std::string::npos);
  EXPECT_NE(report_content.find(std::string{"sink_clustering_enabled="} + (sink_clustering_enabled ? "true\n" : "false\n")), std::string::npos);
  EXPECT_TRUE(std::regex_search(report_content, std::regex(R"((^|\n)input_sink_count=[1-9][0-9]*(\n|$))")));
  EXPECT_TRUE(std::regex_search(report_content, std::regex(R"((^|\n)htree_node_count=[1-9][0-9]*(\n|$))")));
  EXPECT_NE(report_content.find("artifacts=cts.log,synthesis_topology.svg,synthesis_report.txt\n"), std::string::npos);
}

}  // namespace

auto WriteAndAssertSynthesisArtifacts(const std::string& case_name, const std::string& scenario_name, const std::string& clock_name,
                                      const synthesis::TopologyArtifactPaths& artifact_paths, icts::Pin* source, const std::vector<icts::Pin*>& sinks,
                                      const icts::Topology::Build& result) -> synthesis::TopologyArtifactPaths
{
  if (artifact_paths.output_dir.empty()) {
    ADD_FAILURE() << "Failed to prepare synthesis artifact output dir for case " << case_name;
    return artifact_paths;
  }
  EXPECT_TRUE(synthesis::WriteTopologyArtifacts(artifact_paths, scenario_name, clock_name, source, sinks, result));
  EXPECT_TRUE(std::filesystem::exists(artifact_paths.cts_log));
  EXPECT_TRUE(std::filesystem::exists(artifact_paths.synthesis_svg));
  EXPECT_TRUE(std::filesystem::exists(artifact_paths.report_path));

  std::vector<std::string> artifact_names;
  for (const auto& entry : std::filesystem::directory_iterator(artifact_paths.output_dir)) {
    if (entry.is_regular_file()) {
      artifact_names.push_back(entry.path().filename().string());
    }
  }
  std::ranges::sort(artifact_names);
  EXPECT_EQ(artifact_names, (std::vector<std::string>{"cts.log", "synthesis_report.txt", "synthesis_topology.svg"}));
  return artifact_paths;
}

auto AssertClusteredArtifacts(const synthesis::TopologyArtifactPaths& artifact_paths) -> void
{
  const auto cts_log_content = ReadTextFile(artifact_paths.cts_log);
  const auto report_content = ReadTextFile(artifact_paths.report_path);
  ASSERT_FALSE(cts_log_content.empty());
  ASSERT_FALSE(report_content.empty());
  AssertLoggerContract(cts_log_content);
  AssertReportContract(report_content, true);
  EXPECT_TRUE(std::regex_search(report_content, std::regex(R"((^|\n)cluster_buffer_count=[1-9][0-9]*(\n|$))")));

  const auto svg_content = ReadTextFile(artifact_paths.synthesis_svg);
  ASSERT_FALSE(svg_content.empty());
  EXPECT_EQ(svg_content.find("cts_clock_source_to_htree_root"), std::string::npos);
  EXPECT_NE(svg_content.find("sink-level net"), std::string::npos);
  EXPECT_TRUE(std::regex_search(svg_content, std::regex(R"(<line [^>]*stroke="#2ca25f"[^>]*><title>net cts_htree_net_)")));
  EXPECT_TRUE(std::regex_search(svg_content, std::regex(R"(<line [^>]*stroke="#0f766e"[^>]*><title>sink-level net )")));
}

auto AssertNonClusteredArtifacts(const synthesis::TopologyArtifactPaths& artifact_paths) -> void
{
  const auto cts_log_content = ReadTextFile(artifact_paths.cts_log);
  const auto report_content = ReadTextFile(artifact_paths.report_path);
  ASSERT_FALSE(cts_log_content.empty());
  ASSERT_FALSE(report_content.empty());
  AssertLoggerContract(cts_log_content);
  AssertReportContract(report_content, false);
  EXPECT_NE(report_content.find("cluster_buffer_count=0\n"), std::string::npos);

  const auto svg_content = ReadTextFile(artifact_paths.synthesis_svg);
  ASSERT_FALSE(svg_content.empty());
  EXPECT_EQ(svg_content.find("cts_clock_source_to_htree_root"), std::string::npos);
  EXPECT_EQ(svg_content.find("sink-level net"), std::string::npos);
  EXPECT_TRUE(std::regex_search(svg_content, std::regex(R"(<line [^>]*stroke="#2ca25f"[^>]*><title>net cts_htree_net_)")));
}

}  // namespace icts_test::synthesis_realtech_smoke
