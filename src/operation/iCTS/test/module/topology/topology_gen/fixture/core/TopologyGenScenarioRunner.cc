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
 * @file TopologyGenScenarioRunner.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-11
 * @brief Thin scenario runner for topology generation tests.
 */
#include <gtest/gtest.h>

#include <filesystem>
#include <ostream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "Logger.hh"
#include "module/topology/TopologyGen.hh"
#include "module/topology/topology_gen/fixture/TopologyGenCaseFixture.hh"
#include "module/topology/topology_gen/fixture/TopologyGenScenario.hh"
#include "toolkit/io/TestArtifactIO.hh"

namespace icts_test::topology_gen {

static_assert(std::is_same_v<decltype(TopologyCase::name), std::string>);

auto RunBuildAndVisualize(const TopologyCase& test_case) -> void
{
  const auto case_dir_name
      = toolkit::io::SanitizeOutputName(test_case.name + "_" + std::to_string(test_case.count) + "_seed_" + std::to_string(test_case.seed));
  const auto output_dir = toolkit::io::PrepareCleanOutputDir(toolkit::io::ResolveTopologyGenOutputDir() / case_dir_name);
  ASSERT_FALSE(output_dir.empty()) << "Failed to prepare topology output dir for case: " << test_case.name;

  CTSLOG.info(icts::Loc::current(), "Topology test start: ", test_case.name, ", count=", test_case.count, ", seed=", test_case.seed);

  auto data = detail::GenerateCase(test_case);
  ASSERT_EQ(data.loads.size(), test_case.count);

  const auto tree = icts::TopologyGen::build(data.loads, icts::TopologyGen::Input{}, icts::TopologyGen::Config{});
  detail::TopologyArtifacts artifacts;
  detail::AnalyzeBuiltTopology(tree, data.loads, artifacts);
  detail::LogTopologySummary(artifacts.stats);
  detail::ValidateTreeEdges(tree);
  detail::WriteArtifacts(output_dir, test_case, tree, data.loads);
}

}  // namespace icts_test::topology_gen
