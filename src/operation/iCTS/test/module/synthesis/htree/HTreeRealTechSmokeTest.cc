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
 * @file HTreeRealTechSmokeTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-14
 * @brief Real-tech smoke coverage for HTree on DEF-derived clock loads.
 */

#include <gtest/gtest.h>

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "HTreeTopologyChar.hh"
#include "HTreeTopologyPattern.hh"
#include "Logger.hh"
#include "Net.hh"
#include "PatternId.hh"
#include "Pin.hh"
#include "data_manager/DataManager.hh"
#include "data_manager/config/Config.hh"
#include "data_manager/realtech/setup/RealTechDesignSetup.hh"
#include "module/characterization/fixture/CharacterizationRealTechFixture.hh"
#include "module/synthesis/htree/HTree.hh"
#include "module/synthesis/htree/HTreeArtifactWriter.hh"
#include "module/synthesis/htree/HTreeBuildObservation.hh"
#include "module/synthesis/htree/HTreeRealTechScenario.hh"
#include "module/synthesis/htree/diagnostic/HTreeDiagnostic.hh"

namespace icts_test {
namespace {

namespace design_realtech = data_manager::realtech;
namespace characterization_realtech = characterization::realtech;

TEST(HTreeRealTechSmokeTest, SynthesizesMaterializedHTreeFromRealClockLoads)
{
  const auto& setup_state = design_realtech::EnsureRealTechSetup();
  if (setup_state.mode != design_realtech::RealTechMode::kRealTech || !setup_state.setup_succeeded) {
    GTEST_SKIP() << setup_state.summary;
    return;
  }

  const auto selected_clock = SelectLargestRealClockLoads(kMaxRealClockLoadCount);
  if (!selected_clock.has_value()) {
    GTEST_SKIP() << "No DEF-derived clock net exposes at least two CTS sink pins.";
    return;
  }

  characterization_realtech::RealTechCharFixture char_fixture;
  if (const auto prepare_error = char_fixture.prepare("htree_smoke", std::nullopt, kHTreeSmokeMaxSlewNs, kHTreeSmokeMaxCapPf); prepare_error.has_value()) {
    GTEST_SKIP() << *prepare_error;
    return;
  }

  EXPECT_EQ(CTSDM.getConfig().get_wirelength_iterations(), characterization_realtech::kRealTechCharWirelengthIterations);
  EXPECT_EQ(CTSDM.getConfig().get_slew_steps(), characterization_realtech::kRealTechCharSlewSteps);
  EXPECT_EQ(CTSDM.getConfig().get_cap_steps(), characterization_realtech::kRealTechCharCapSteps);
  EXPECT_TRUE(CTSDM.getConfig().has_max_buf_tran());
  EXPECT_TRUE(CTSDM.getConfig().has_max_cap());
  EXPECT_DOUBLE_EQ(CTSDM.getConfig().get_max_buf_tran(), kHTreeSmokeMaxSlewNs);
  EXPECT_DOUBLE_EQ(CTSDM.getConfig().get_max_cap(), kHTreeSmokeMaxCapPf);

  const auto& real_loads = selected_clock->loads;
  ASSERT_GE(real_loads.size(), 2U);
  ASSERT_EQ(CountPinsWithRealContext(real_loads), real_loads.size())
      << "Selected clock loads do not carry complete DEF/CTS instance context: " << selected_clock->clock_name;

  std::unordered_set<icts::Pin*> original_loads(real_loads.begin(), real_loads.end());
  const auto artifact_paths = htree::PrepareHTreeArtifactPaths("realtech_smoke");
  ASSERT_FALSE(artifact_paths.output_dir.empty());
  CTSLOG.openLogFileStream(artifact_paths.cts_log.string());
  CTSLOG.info(icts::Loc::current(), "HTree smoke scenario: realtech_smoke, clock=", selected_clock->clock_name, ", loads=", real_loads.size(), ".");

  icts::Pin root_driver("htree_smoke_root_out", icts::PinType::kOut);
  icts::Net root_net("htree_smoke_root_net");
  ConnectRootNetForHTreeTest(root_net, root_driver, real_loads);

  auto result = icts::htree::BuildWithDiagnostics(MakeExplicitHTreeInput(root_net), MakeExplicitHTreeConfig());

  ASSERT_TRUE(result.summary.success);
  EXPECT_TRUE(result.summary.failure_reason.empty());
  ASSERT_TRUE(result.output.best_char.has_value());
  ASSERT_TRUE(result.output.best_pattern.has_value());
  const auto observation = htree::ObserveHTreeBuild(result);
  ASSERT_GT(observation.selected_feasible_solution_count, 0U);
  ASSERT_GT(observation.selected_feasible_frontier_entry_count, 0U);
  AssertDepthCandidateCoverage(result);
  AssertSelectedHTreeLoadDistribution(result);
  EXPECT_LE(observation.selected_feasible_frontier_entry_count, observation.selected_feasible_solution_count);
  const icts::HTreeTopologyPattern* best_pattern = nullptr;
  if (result.output.best_pattern.has_value()) {
    best_pattern = &result.output.best_pattern.value();
  }
  ASSERT_NE(best_pattern, nullptr);
  ASSERT_EQ(best_pattern->get_levels(), result.output.levels.size());
  ASSERT_EQ(best_pattern->get_level_segment_pattern_ids().size(), result.output.levels.size());
  ASSERT_EQ(result.output.root_net, &root_net);
  ASSERT_NE(result.output.root_output_pin, nullptr);
  EXPECT_EQ(result.output.root_output_pin, &root_driver);
  EXPECT_FALSE(result.output.inserted_pins.empty());
  EXPECT_FALSE(result.output.inserted_nets.empty());
  AssertNoSingleLoadExternalLeafBuffer(result);

  const auto leaf_loads = CollectLeafLoads(result.output.topology);
  EXPECT_EQ(leaf_loads.size(), original_loads.size());
  for (auto* load : real_loads) {
    ASSERT_NE(load, nullptr);
    EXPECT_TRUE(leaf_loads.contains(load));
    EXPECT_NE(load->get_inst(), nullptr);
    EXPECT_NE(load->get_net(), nullptr);
  }

  WriteAndAssertHTreeArtifacts(artifact_paths, "htree_realtech_smoke", selected_clock->clock_name, real_loads, result);

  const auto cts_log_content = ReadTextFile(artifact_paths.cts_log);
  const auto report_content = ReadTextFile(artifact_paths.report_path);
  ASSERT_FALSE(cts_log_content.empty());
  ASSERT_FALSE(report_content.empty());
  EXPECT_NE(cts_log_content.find("HTree smoke scenario: realtech_smoke"), std::string::npos);
  EXPECT_NE(report_content.find("frontier_feasible_solution_count="), std::string::npos);
  EXPECT_NE(report_content.find("feasible_frontier_entry_count="), std::string::npos);
  EXPECT_NE(report_content.find("delay_power_selection_summary"), std::string::npos);
  EXPECT_NE(report_content.find("frontier_selection_pool_size="), std::string::npos);
  EXPECT_NE(report_content.find("selection_policy=global_frontier_pareto_power_median"), std::string::npos);
  EXPECT_NE(report_content.find("pareto_power_median_index="), std::string::npos);
  EXPECT_NE(report_content.find("selected_pareto_power_order_index="), std::string::npos);
}

}  // namespace
}  // namespace icts_test
