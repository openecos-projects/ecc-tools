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
 * @file HTreeTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-04-14
 * @brief Unit coverage for HTree degenerate cases.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "BufferingPattern.hh"
#include "CharCore.hh"
#include "HTreeTopologyChar.hh"
#include "HTreeTopologyPattern.hh"
#include "Net.hh"
#include "PatternId.hh"
#include "Pin.hh"
#include "Point.hh"
#include "SegmentChar.hh"
#include "Tree.hh"
#include "data_manager/DataManager.hh"
#include "module/synthesis/htree/HTree.hh"
#include "module/synthesis/htree/HTreeBuildObservation.hh"
#include "module/synthesis/htree/constraint/Constraint.hh"
#include "module/synthesis/htree/diagnostic/HTreeDiagnostic.hh"
#include "module/synthesis/htree/plan/Plan.hh"
#include "module/synthesis/htree/segment_pruning/SegmentPruning.hh"
#include "module/synthesis/htree/topology_pruning/TopologyPruning.hh"
#include "synthesis/htree/segment_pruning/SegmentFrontierCatalog.hh"
#include "synthesis/htree/segment_pruning/SegmentPatternLibrary.hh"

namespace icts_test {
namespace {

auto ConnectRootNet(icts::Net& root_net, icts::Pin& root_driver, const std::vector<icts::Pin*>& loads) -> void
{
  root_net.set_driver(&root_driver);
  root_driver.set_net(&root_net);
  root_net.set_loads(loads);
  for (auto* load : loads) {
    if (load != nullptr) {
      load->set_net(&root_net);
    }
  }
}

auto MakeTopologyChar(unsigned pattern_id, double delay, double power) -> icts::HTreeTopologyChar
{
  return icts::HTreeTopologyChar(icts::CharCore(1U, 1U, 1U, 1U, delay, power, icts::PatternId::topology(pattern_id), 0.0), 1U);
}

auto MakeSegmentChar(unsigned pattern_id, double delay, double power) -> icts::SegmentChar
{
  return icts::SegmentChar(icts::CharCore(1U, 1U, 1U, 1U, delay, power, icts::PatternId::segment(pattern_id), 0.0), 1U);
}

auto MakeBufferedBoundaryState() -> icts::MonotonicBoundaryState
{
  return icts::MonotonicBoundaryState{
      .source = icts::BoundaryBufferState{.has_buffer = true, .strength_rank = 1U},
      .sink = icts::BoundaryBufferState{.has_buffer = true, .strength_rank = 1U},
  };
}

auto AddSingleLevelTopologyPattern(icts::htree::TopologyPatternLibrary& topology_pattern_library, icts::PatternId segment_pattern_id) -> void
{
  topology_pattern_library.addSeed(icts::PatternId::topology(0U), segment_pattern_id, icts::PatternCompositionState{});
}

struct SegmentFrontierTestData
{
  SegmentFrontierTestData() : pattern_library(CTSDM.getWrapper()) {}

  icts::htree::BufferPatternLibrary pattern_library;
  icts::htree::SegmentFrontierCatalog catalog;
};

auto BuildSegmentFrontierTestData(icts::htree::SegmentFrontierKindSet required_kinds) -> SegmentFrontierTestData
{
  SegmentFrontierTestData test_data;
  test_data.pattern_library.add(icts::BufferingPattern(1U, icts::PatternId::segment(1U), {}, {}, false));
  test_data.pattern_library.add(icts::BufferingPattern(1U, icts::PatternId::segment(2U), {}, {}, true));

  const std::vector<icts::SegmentChar> segment_chars{
      MakeSegmentChar(1U, 2.0, 2.0),
      MakeSegmentChar(2U, 1.0, 1.0),
  };
  test_data.catalog = icts::htree::SynthesizeSegmentFrontiers(segment_chars, test_data.pattern_library,
                                                              icts::htree::RequiredSegmentFrontiers{
                                                                  .required_length_indices = {2U},
                                                                  .required_kinds = required_kinds,
                                                              });
  return test_data;
}

auto BuildSegmentFrontierSignatures(const SegmentFrontierTestData& test_data, icts::htree::SegmentFrontierKind kind) -> std::vector<std::string>
{
  std::vector<std::string> signatures;
  const auto* frontier = test_data.catalog.find(2U, kind);
  if (frontier == nullptr) {
    return signatures;
  }

  signatures.reserve(frontier->size());
  for (const auto& entry : *frontier) {
    const auto* pattern = test_data.pattern_library.find(entry.get_pattern_id());
    std::string signature = std::to_string(entry.get_length_idx()) + "/" + std::to_string(entry.get_input_slew_idx()) + "/"
                            + std::to_string(entry.get_output_slew_idx()) + "/" + std::to_string(entry.get_driven_cap_idx()) + "/"
                            + std::to_string(entry.get_load_cap_idx()) + "/" + std::to_string(entry.get_delay()) + "/" + std::to_string(entry.get_power());
    if (pattern == nullptr) {
      signature += "/missing_pattern";
    } else {
      signature += pattern->hasTerminalBranchBuffer() ? "/branch" : "/leaf";
      signature += "/" + std::to_string(pattern->get_buffer_positions().size());
      signature += "/" + std::to_string(pattern->get_cell_masters().size());
    }
    signatures.push_back(signature);
  }
  std::ranges::sort(signatures);
  return signatures;
}

auto MakeBareHTreeInput(icts::Net& root_net) -> icts::HTree::Input
{
  icts::HTree::Input input;
  input.root_net = &root_net;
  return input;
}

auto MakeRuntimeHTreeInput(icts::Net& root_net) -> icts::HTree::Input
{
  auto input = MakeBareHTreeInput(root_net);
  input.design = &CTSDM.getDesign();
  input.wrapper = &CTSDM.getWrapper();
  input.characterization_input.dbu_per_um = 1000;
  input.characterization_input.wrapper = &CTSDM.getWrapper();
  input.characterization_input.fast_sta = &CTSDM.getFastSTA();
  return input;
}

TEST(HTreeTest, RequiredSegmentFrontiersBuildOnlyRequiredKinds)
{
  const auto all_only = BuildSegmentFrontierTestData(icts::htree::SegmentFrontierKindSet::allOnly());
  EXPECT_NE(all_only.catalog.find(2U, icts::htree::SegmentFrontierKind::kAll), nullptr);
  EXPECT_EQ(all_only.catalog.find(2U, icts::htree::SegmentFrontierKind::kTerminalBranchBuffered), nullptr);
  EXPECT_EQ(all_only.catalog.find(2U, icts::htree::SegmentFrontierKind::kTerminalLeafUnbuffered), nullptr);

  const auto branch = BuildSegmentFrontierTestData(icts::htree::SegmentFrontierKindSet::branchConstrained());
  EXPECT_NE(branch.catalog.find(2U, icts::htree::SegmentFrontierKind::kAll), nullptr);
  EXPECT_NE(branch.catalog.find(2U, icts::htree::SegmentFrontierKind::kTerminalBranchBuffered), nullptr);
  EXPECT_EQ(branch.catalog.find(2U, icts::htree::SegmentFrontierKind::kTerminalLeafUnbuffered), nullptr);

  const auto full = BuildSegmentFrontierTestData(icts::htree::SegmentFrontierKindSet::full());
  EXPECT_NE(full.catalog.find(2U, icts::htree::SegmentFrontierKind::kAll), nullptr);
  EXPECT_NE(full.catalog.find(2U, icts::htree::SegmentFrontierKind::kTerminalBranchBuffered), nullptr);
  EXPECT_NE(full.catalog.find(2U, icts::htree::SegmentFrontierKind::kTerminalLeafUnbuffered), nullptr);

  EXPECT_EQ(BuildSegmentFrontierSignatures(all_only, icts::htree::SegmentFrontierKind::kAll),
            BuildSegmentFrontierSignatures(full, icts::htree::SegmentFrontierKind::kAll));
  EXPECT_EQ(BuildSegmentFrontierSignatures(branch, icts::htree::SegmentFrontierKind::kAll),
            BuildSegmentFrontierSignatures(full, icts::htree::SegmentFrontierKind::kAll));
  EXPECT_EQ(BuildSegmentFrontierSignatures(branch, icts::htree::SegmentFrontierKind::kTerminalBranchBuffered),
            BuildSegmentFrontierSignatures(full, icts::htree::SegmentFrontierKind::kTerminalBranchBuffered));
}

TEST(HTreeTest, HTreeRequiredSegmentFrontiersFollowsBoundaryConstraints)
{
  const std::vector<unsigned> required_lengths{1U, 2U, 3U};

  const auto unrestricted_frontiers = icts::htree::ResolveRequiredSegmentFrontiers(required_lengths, icts::htree::BoundaryConstraints{});
  EXPECT_EQ(unrestricted_frontiers.required_length_indices, required_lengths);
  EXPECT_TRUE(unrestricted_frontiers.required_kinds.contains(icts::htree::SegmentFrontierKind::kAll));
  EXPECT_FALSE(unrestricted_frontiers.required_kinds.contains(icts::htree::SegmentFrontierKind::kTerminalBranchBuffered));
  EXPECT_FALSE(unrestricted_frontiers.required_kinds.contains(icts::htree::SegmentFrontierKind::kTerminalLeafUnbuffered));

  const auto branch_frontiers = icts::htree::ResolveRequiredSegmentFrontiers(required_lengths, icts::htree::BoundaryConstraints{
                                                                                                   .force_branch_buffer = true,
                                                                                               });
  EXPECT_EQ(branch_frontiers.required_length_indices, required_lengths);
  EXPECT_TRUE(branch_frontiers.required_kinds.contains(icts::htree::SegmentFrontierKind::kAll));
  EXPECT_TRUE(branch_frontiers.required_kinds.contains(icts::htree::SegmentFrontierKind::kTerminalBranchBuffered));
  EXPECT_FALSE(branch_frontiers.required_kinds.contains(icts::htree::SegmentFrontierKind::kTerminalLeafUnbuffered));
}

TEST(HTreeTest, EmptyLoadsReturnsEmptyResult)
{
  icts::Pin root_driver("root_out", icts::PinType::kOut, icts::Point<int>(0, 0));
  icts::Net root_net("root_net");
  ConnectRootNet(root_net, root_driver, {});

  const auto result = icts::htree::BuildWithDiagnostics(MakeBareHTreeInput(root_net), icts::HTree::Config{});
  const auto observation = htree::ObserveHTreeBuild(result);

  EXPECT_FALSE(result.summary.success);
  EXPECT_EQ(result.summary.failure_reason, "empty_root_net_loads");
  EXPECT_EQ(result.output.topology.get_size(), 0U);
  EXPECT_TRUE(result.output.levels.empty());
  EXPECT_FALSE(result.output.best_char.has_value());
  EXPECT_FALSE(result.output.best_pattern.has_value());
  EXPECT_EQ(observation.selected_candidate_solution_count, 0U);
  EXPECT_EQ(observation.selected_feasible_solution_count, 0U);
  EXPECT_TRUE(result.output.inserted_insts.empty());
  EXPECT_TRUE(result.output.inserted_nets.empty());
  EXPECT_EQ(result.output.root_net, &root_net);
  EXPECT_EQ(result.output.root_output_pin, &root_driver);
}

TEST(HTreeTest, EmptyLoadsAcceptExplicitInputConfig)
{
  icts::Pin root_driver("root_out", icts::PinType::kOut, icts::Point<int>(0, 0));
  icts::Net root_net("root_net");
  ConnectRootNet(root_net, root_driver, {});

  const auto result = icts::htree::BuildWithDiagnostics(MakeBareHTreeInput(root_net), icts::HTree::Config{
                                                                                          .force_branch_buffer = true,
                                                                                          .min_top_input_slew_ns = 0.05,
                                                                                      });
  const auto observation = htree::ObserveHTreeBuild(result);

  EXPECT_FALSE(result.summary.success);
  EXPECT_EQ(result.summary.failure_reason, "empty_root_net_loads");
  EXPECT_EQ(result.output.topology.get_size(), 0U);
  EXPECT_TRUE(result.output.levels.empty());
  EXPECT_FALSE(result.output.best_char.has_value());
  EXPECT_FALSE(result.output.best_pattern.has_value());
  EXPECT_EQ(observation.selected_candidate_solution_count, 0U);
  EXPECT_EQ(observation.selected_feasible_solution_count, 0U);
  EXPECT_TRUE(result.output.inserted_insts.empty());
  EXPECT_TRUE(result.output.inserted_nets.empty());
  EXPECT_EQ(result.output.root_net, &root_net);
  EXPECT_EQ(result.output.root_output_pin, &root_driver);
}

TEST(HTreeTest, MissingRootDriverStopsBeforeTopology)
{
  auto load = std::make_unique<icts::Pin>("load0", icts::PinType::kClock, icts::Point<int>(100, 200));
  icts::Net root_net("root_net");
  root_net.set_loads({load.get()});

  const auto result = icts::htree::BuildWithDiagnostics(MakeBareHTreeInput(root_net), icts::HTree::Config{});
  const auto observation = htree::ObserveHTreeBuild(result);

  EXPECT_FALSE(result.summary.success);
  EXPECT_EQ(result.summary.failure_reason, "missing_root_driver_pin");
  EXPECT_EQ(result.output.topology.get_size(), 0U);
  EXPECT_EQ(observation.selected_candidate_solution_count, 0U);
  EXPECT_EQ(observation.selected_feasible_solution_count, 0U);
  EXPECT_TRUE(result.output.inserted_insts.empty());
  EXPECT_TRUE(result.output.inserted_nets.empty());
  EXPECT_EQ(result.output.root_net, &root_net);
  EXPECT_EQ(result.output.root_output_pin, nullptr);
  EXPECT_EQ(load->get_net(), nullptr);
}

TEST(HTreeTest, SingleLoadBuildsTrivialTopology)
{
  icts::Pin root_driver("root_out", icts::PinType::kOut, icts::Point<int>(0, 0));
  icts::Net root_net("root_net");
  auto load = std::make_unique<icts::Pin>("load0", icts::PinType::kClock, icts::Point<int>(100, 200));
  std::vector<icts::Pin*> loads{load.get()};
  ConnectRootNet(root_net, root_driver, loads);

  const auto result = icts::htree::BuildWithDiagnostics(MakeBareHTreeInput(root_net), icts::HTree::Config{});
  const auto observation = htree::ObserveHTreeBuild(result);

  EXPECT_TRUE(result.summary.success);
  EXPECT_TRUE(result.summary.failure_reason.empty());
  EXPECT_EQ(result.output.topology.get_size(), 1U);
  EXPECT_TRUE(result.output.levels.empty());
  EXPECT_FALSE(result.output.best_char.has_value());
  EXPECT_FALSE(result.output.best_pattern.has_value());
  EXPECT_TRUE(observation.has_selected_depth);
  EXPECT_EQ(observation.selected_depth, 0U);
  EXPECT_EQ(observation.selected_candidate_solution_count, 0U);
  EXPECT_EQ(observation.selected_feasible_solution_count, 0U);
  EXPECT_TRUE(result.output.inserted_insts.empty());
  EXPECT_TRUE(result.output.inserted_nets.empty());
  EXPECT_EQ(result.output.root_net, &root_net);
  EXPECT_EQ(result.output.root_output_pin, &root_driver);
  EXPECT_EQ(root_net.get_driver(), &root_driver);
  ASSERT_EQ(root_net.get_loads().size(), 1U);
  EXPECT_EQ(root_net.get_loads().front(), load.get());
  EXPECT_EQ(load->get_net(), &root_net);
}

TEST(HTreeTest, DegenerateTopologyBuildsDirectRootLoads)
{
  icts::Pin root_driver("root_out", icts::PinType::kOut, icts::Point<int>(0, 0));
  icts::Net root_net("root_net");
  auto load0 = std::make_unique<icts::Pin>("load0", icts::PinType::kClock, icts::Point<int>(100, 200));
  auto load1 = std::make_unique<icts::Pin>("load1", icts::PinType::kClock, icts::Point<int>(300, 500));
  std::vector<icts::Pin*> loads{load0.get(), load1.get()};
  ConnectRootNet(root_net, root_driver, loads);

  const auto result = icts::htree::BuildWithDiagnostics(MakeRuntimeHTreeInput(root_net), icts::HTree::Config{
                                                                                             .max_fanout = 4U,
                                                                                         });
  const auto observation = htree::ObserveHTreeBuild(result);

  EXPECT_TRUE(result.summary.success);
  EXPECT_TRUE(result.summary.failure_reason.empty());
  EXPECT_EQ(result.output.topology.get_size(), 1U);
  ASSERT_NE(result.output.topology.get_node(result.output.topology.get_root()), nullptr);
  EXPECT_EQ(result.output.topology.get_node(result.output.topology.get_root())->get_loads(), loads);
  EXPECT_TRUE(result.output.levels.empty());
  EXPECT_FALSE(result.output.best_char.has_value());
  EXPECT_FALSE(result.output.best_pattern.has_value());
  EXPECT_TRUE(observation.has_selected_depth);
  EXPECT_EQ(observation.selected_depth, 0U);
  EXPECT_EQ(observation.selected_candidate_solution_count, 0U);
  EXPECT_EQ(observation.selected_feasible_solution_count, 0U);
  EXPECT_TRUE(result.output.inserted_insts.empty());
  EXPECT_TRUE(result.output.inserted_nets.empty());
  EXPECT_EQ(result.output.root_net, &root_net);
  EXPECT_EQ(result.output.root_output_pin, &root_driver);
  EXPECT_EQ(root_net.get_driver(), &root_driver);
  EXPECT_EQ(root_net.get_loads(), loads);
  EXPECT_EQ(load0->get_net(), &root_net);
  EXPECT_EQ(load1->get_net(), &root_net);
}

TEST(HTreeTest, GlobalSelectionPreservesDelayPowerTieMultiplicity)
{
  std::vector<icts::HTreeTopologyChar> entries;
  entries.push_back(MakeTopologyChar(1U, 10.0, 1.0));
  entries.push_back(MakeTopologyChar(2U, 10.0, 1.0));
  entries.push_back(MakeTopologyChar(3U, 5.0, 3.0));
  entries.push_back(MakeTopologyChar(4U, 3.0, 5.0));

  std::vector<icts::htree::CandidateCharRef> refs;
  refs.reserve(entries.size());
  for (const auto& entry : entries) {
    refs.push_back(icts::htree::CandidateCharRef{
        .candidate_index = 0U,
        .entry = &entry,
    });
  }

  const auto selected_ref = icts::htree::SelectBestGlobalEntry(refs);

  if (!selected_ref.has_value()) {
    ADD_FAILURE() << "Expected a selected global H-tree candidate.";
    return;
  }
  const auto* const selected_entry = selected_ref->entry;
  ASSERT_NE(selected_entry, nullptr);
  EXPECT_EQ(selected_entry->get_pattern_id().local_id, 2U);
}

TEST(HTreeTest, PerDepthParetoCompressionKeepsDepthGroupsIndependent)
{
  std::vector<icts::HTreeTopologyChar> entries;
  entries.push_back(MakeTopologyChar(1U, 4.0, 4.0));
  entries.push_back(MakeTopologyChar(2U, 5.0, 5.0));
  entries.push_back(MakeTopologyChar(3U, 3.0, 3.0));

  const std::vector<icts::htree::CandidateCharRef> refs{
      icts::htree::CandidateCharRef{
          .candidate_index = 0U,
          .entry = &entries.at(0),
      },
      icts::htree::CandidateCharRef{
          .candidate_index = 0U,
          .entry = &entries.at(1),
      },
      icts::htree::CandidateCharRef{
          .candidate_index = 1U,
          .entry = &entries.at(2),
      },
  };

  const auto compressed_refs = icts::htree::BuildPerDepthDelayPowerParetoRefs(refs);

  std::vector<unsigned> kept_pattern_ids;
  kept_pattern_ids.reserve(compressed_refs.size());
  for (const auto& ref : compressed_refs) {
    ASSERT_NE(ref.entry, nullptr);
    kept_pattern_ids.push_back(ref.entry->get_pattern_id().local_id);
  }
  std::ranges::sort(kept_pattern_ids);
  EXPECT_EQ(kept_pattern_ids, (std::vector<unsigned>{1U, 3U}));
}

TEST(HTreeTest, AutoDepthCandidateResolutionExploresFullRange)
{
  const auto depth_candidates = icts::htree::ResolveDepthCandidates(4U, icts::HTree::Config{.depth_explore_window = 0U});

  EXPECT_EQ(depth_candidates, (std::vector<unsigned>{4U, 3U, 2U, 1U}));
}

TEST(HTreeTest, ExplicitDepthCandidateWindowRemainsBounded)
{
  const auto depth_candidates = icts::htree::ResolveDepthCandidates(4U, icts::HTree::Config{.depth_explore_window = 2U});

  EXPECT_EQ(depth_candidates, (std::vector<unsigned>{4U, 3U}));
}

TEST(HTreeTest, AdaptiveGlobalSelectionChoosesTimingShapeWhenItIsNoMoreComplex)
{
  icts::htree::BufferPatternLibrary segment_pattern_library(CTSDM.getWrapper());
  segment_pattern_library.add(icts::BufferingPattern(1U, icts::PatternId::segment(0U), {}, {}, false));
  segment_pattern_library.add(icts::BufferingPattern(1U, icts::PatternId::segment(1U), {0.5}, {"BUF_X1"}, false, MakeBufferedBoundaryState()));

  std::vector<icts::htree::CandidateBuildEvaluation> evaluations(3);
  evaluations.at(0).depth = 5U;
  evaluations.at(1).depth = 4U;
  evaluations.at(2).depth = 2U;
  AddSingleLevelTopologyPattern(evaluations.at(0).topology_pattern_library, icts::PatternId::segment(1U));
  AddSingleLevelTopologyPattern(evaluations.at(1).topology_pattern_library, icts::PatternId::segment(1U));
  AddSingleLevelTopologyPattern(evaluations.at(2).topology_pattern_library, icts::PatternId::segment(0U));

  std::vector<icts::HTreeTopologyChar> entries;
  entries.push_back(MakeTopologyChar(0U, 10.0, 1.0));
  entries.push_back(MakeTopologyChar(0U, 7.0, 2.0));
  entries.push_back(MakeTopologyChar(0U, 5.0, 3.0));

  const std::vector<icts::htree::CandidateCharRef> refs{
      icts::htree::CandidateCharRef{.candidate_index = 0U, .entry = &entries.at(0)},
      icts::htree::CandidateCharRef{.candidate_index = 1U, .entry = &entries.at(1)},
      icts::htree::CandidateCharRef{.candidate_index = 2U, .entry = &entries.at(2)},
  };
  icts::htree::GlobalSelectionDecision detail;

  const auto selected_ref = icts::htree::SelectAdaptiveGlobalEntry(refs, evaluations, segment_pattern_library, &detail);

  if (!selected_ref.has_value()) {
    FAIL() << "Adaptive global selection returned no candidate.";
    return;
  }
  EXPECT_EQ(selected_ref->candidate_index, 2U);
  EXPECT_EQ(detail.policy, "adaptive_physical_shape_timing");
  EXPECT_EQ(detail.adaptive_reason, "timing_candidate_reduces_physical_depth_or_buffer_count");
  EXPECT_EQ(detail.front_size, 3U);
  EXPECT_DOUBLE_EQ(detail.front_min_delay_ns, 5.0);
}

TEST(HTreeTest, AdaptiveGlobalSelectionKeepsMedianWhenTimingShapeAddsComplexity)
{
  icts::htree::BufferPatternLibrary segment_pattern_library(CTSDM.getWrapper());
  segment_pattern_library.add(icts::BufferingPattern(1U, icts::PatternId::segment(0U), {}, {}, false));
  segment_pattern_library.add(icts::BufferingPattern(1U, icts::PatternId::segment(1U), {0.5}, {"BUF_X1"}, false, MakeBufferedBoundaryState()));

  std::vector<icts::htree::CandidateBuildEvaluation> evaluations(3);
  evaluations.at(0).depth = 1U;
  evaluations.at(1).depth = 2U;
  evaluations.at(2).depth = 4U;
  AddSingleLevelTopologyPattern(evaluations.at(0).topology_pattern_library, icts::PatternId::segment(0U));
  AddSingleLevelTopologyPattern(evaluations.at(1).topology_pattern_library, icts::PatternId::segment(0U));
  AddSingleLevelTopologyPattern(evaluations.at(2).topology_pattern_library, icts::PatternId::segment(1U));

  std::vector<icts::HTreeTopologyChar> entries;
  entries.push_back(MakeTopologyChar(0U, 10.0, 1.0));
  entries.push_back(MakeTopologyChar(0U, 7.0, 2.0));
  entries.push_back(MakeTopologyChar(0U, 5.0, 3.0));

  const std::vector<icts::htree::CandidateCharRef> refs{
      icts::htree::CandidateCharRef{.candidate_index = 0U, .entry = &entries.at(0)},
      icts::htree::CandidateCharRef{.candidate_index = 1U, .entry = &entries.at(1)},
      icts::htree::CandidateCharRef{.candidate_index = 2U, .entry = &entries.at(2)},
  };
  icts::htree::GlobalSelectionDecision detail;

  const auto selected_ref = icts::htree::SelectAdaptiveGlobalEntry(refs, evaluations, segment_pattern_library, &detail);

  if (!selected_ref.has_value()) {
    FAIL() << "Adaptive global selection returned no candidate.";
    return;
  }
  EXPECT_EQ(selected_ref->candidate_index, 1U);
  EXPECT_EQ(detail.policy, "adaptive_physical_shape_median");
  EXPECT_EQ(detail.adaptive_reason, "timing_candidate_adds_physical_depth_or_buffer_count");
  EXPECT_EQ(detail.front_size, 3U);
  EXPECT_DOUBLE_EQ(detail.front_min_delay_ns, 5.0);
}

TEST(HTreeTest, AdaptiveGlobalSelectionKeepsMedianWhenTimingAddsSplitBuffers)
{
  icts::htree::BufferPatternLibrary segment_pattern_library(CTSDM.getWrapper());
  segment_pattern_library.add(icts::BufferingPattern(1U, icts::PatternId::segment(0U), {}, {}, false));

  std::vector<icts::htree::CandidateBuildEvaluation> evaluations(3);
  evaluations.at(0).depth = 2U;
  evaluations.at(1).depth = 2U;
  evaluations.at(2).depth = 2U;
  AddSingleLevelTopologyPattern(evaluations.at(0).topology_pattern_library, icts::PatternId::segment(0U));
  AddSingleLevelTopologyPattern(evaluations.at(1).topology_pattern_library, icts::PatternId::segment(0U));
  AddSingleLevelTopologyPattern(evaluations.at(2).topology_pattern_library, icts::PatternId::segment(0U));

  std::vector<icts::HTreeTopologyChar> entries;
  entries.push_back(MakeTopologyChar(0U, 10.0, 1.0));
  entries.push_back(MakeTopologyChar(0U, 7.0, 2.0));
  entries.push_back(MakeTopologyChar(0U, 5.0, 3.0));

  const std::vector<icts::htree::CandidateCharRef> refs{
      icts::htree::CandidateCharRef{.candidate_index = 0U, .entry = &entries.at(0), .split_extra_buffer_count = 0U},
      icts::htree::CandidateCharRef{.candidate_index = 1U, .entry = &entries.at(1), .split_extra_buffer_count = 10U},
      icts::htree::CandidateCharRef{.candidate_index = 2U, .entry = &entries.at(2), .split_extra_buffer_count = 100U},
  };
  icts::htree::GlobalSelectionDecision detail;

  const auto selected_ref = icts::htree::SelectAdaptiveGlobalEntry(refs, evaluations, segment_pattern_library, &detail);

  if (!selected_ref.has_value()) {
    FAIL() << "Adaptive global selection returned no candidate.";
    return;
  }
  EXPECT_EQ(selected_ref->candidate_index, 1U);
  EXPECT_EQ(detail.policy, "adaptive_physical_shape_median");
  EXPECT_EQ(detail.adaptive_reason, "timing_candidate_adds_physical_depth_or_buffer_count");
  EXPECT_EQ(detail.front_size, 3U);
  EXPECT_DOUBLE_EQ(detail.front_min_delay_ns, 5.0);
}

}  // namespace
}  // namespace icts_test
