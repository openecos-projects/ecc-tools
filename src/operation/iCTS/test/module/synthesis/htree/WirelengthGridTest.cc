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
 * @file WirelengthGridTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-06-12
 * @brief Unit tests for automatic and explicitly configured H-tree characterization grids.
 */

#include <gtest/gtest.h>

#include <optional>
#include <vector>

#include "module/synthesis/htree/characterization/wirelength/WirelengthGrid.hh"

namespace icts_test {
namespace {

constexpr double kRelTol = 1e-9;

// vga_lcd-like request set: 13 lengths, max 506.74 um -> auto unit
// 506.74 / 13 = 38.98 um, required covering iterations = 13.
auto makeVgaLikeRequests() -> std::vector<double>
{
  return {6.0, 8.0, 10.0, 12.0, 20.0, 30.0, 40.0, 60.0, 90.0, 140.0, 144.0, 300.0, 506.74};
}

TEST(WirelengthGridTest, AutoModeCoversRequestedLengthsIndependentlyOfConfiguredIterations)
{
  icts::CharBuilder::Config config;
  config.wirelength_iterations = 3U;

  const auto plan = icts::htree::ResolveCharacterizationGridPlan(config, makeVgaLikeRequests());
  ASSERT_TRUE(plan.adapted);
  EXPECT_EQ(plan.source, icts::htree::CharGridSource::kAutoDerived);
  EXPECT_NEAR(plan.wirelength_unit_um, 506.74 / 13.0, (506.74 / 13.0) * kRelTol);
  EXPECT_EQ(plan.required_covering_iterations, 13U);
  EXPECT_EQ(plan.wirelength_iterations, 13U);
  EXPECT_TRUE(plan.uses_primitive_characterization);
  EXPECT_EQ(plan.direct_length_indices, (std::vector<unsigned>{1U, 2U, 4U, 8U}));
}

TEST(WirelengthGridTest, AutoModeCoversRequestedRange)
{
  icts::CharBuilder::Config config;
  config.wirelength_iterations = 3U;

  const std::vector<double> requests = {20.0, 40.0, 60.0, 100.0};  // unit 25, required 4
  const auto plan = icts::htree::ResolveCharacterizationGridPlan(config, requests);
  ASSERT_TRUE(plan.adapted);
  EXPECT_EQ(plan.required_covering_iterations, 4U);
  EXPECT_EQ(plan.wirelength_iterations, 4U);
}

TEST(WirelengthGridTest, RuntimeConfiguredGridStaysUntouched)
{
  icts::CharBuilder::Config config;
  config.wirelength_unit_um = 10.0;
  config.wirelength_iterations = 3U;

  const std::vector<double> requests = {20.0, 40.0, 60.0};  // bins {2,4,6}: no collapse
  const auto plan = icts::htree::ResolveCharacterizationGridPlan(config, requests);
  EXPECT_FALSE(plan.adapted);
  EXPECT_EQ(plan.source, icts::htree::CharGridSource::kRuntimeConfig);
  EXPECT_EQ(plan.unique_level_bins, 3U);
  // The plan does not override an explicitly configured grid: CharBuilder
  // keeps consuming the runtime unit + iterations directly.
  EXPECT_EQ(plan.wirelength_iterations, 0U);
}

TEST(WirelengthGridTest, CollapsedConfiguredGridAdaptsWithAutoDerivedUnit)
{
  icts::CharBuilder::Config config;
  config.wirelength_unit_um = 1000.0;  // collapses {20,40,60} into a single bin
  config.wirelength_iterations = 3U;

  const std::vector<double> requests = {20.0, 40.0, 60.0};
  const auto plan = icts::htree::ResolveCharacterizationGridPlan(config, requests);
  ASSERT_TRUE(plan.adapted);
  EXPECT_TRUE(plan.configured_grid_collapsed);
  EXPECT_EQ(plan.source, icts::htree::CharGridSource::kAutoDerived);
  EXPECT_NEAR(plan.wirelength_unit_um, 20.0, 20.0 * kRelTol);
  EXPECT_EQ(plan.required_covering_iterations, 3U);
  EXPECT_EQ(plan.wirelength_iterations, 3U);
}

TEST(WirelengthGridTest, AutoModeSeparatesRequestedIndicesFromPrimitiveCharacterization)
{
  icts::CharBuilder::Config config;
  const auto requests = makeVgaLikeRequests();
  const auto plan = icts::htree::ResolveCharacterizationGridPlan(config, requests);
  ASSERT_TRUE(plan.adapted);
  ASSERT_GT(plan.wirelength_iterations, 0U);
  ASSERT_EQ(plan.wirelength_iterations, 13U);

  const std::vector<unsigned> expected_requested = {1U, 2U, 3U, 4U, 8U, 13U};
  EXPECT_EQ(plan.requested_length_indices, expected_requested);
  EXPECT_EQ(icts::htree::ResolveDirectCharacterizationLengthIndices(requests, plan), (std::vector<unsigned>{1U, 2U, 4U, 8U}));
}

TEST(WirelengthGridTest, CoverageOnlyLengthsExtendRangeWithoutDirectEnumeration)
{
  icts::CharBuilder::Config config;
  config.wirelength_iterations = 3U;

  const std::vector<double> topology_lengths = {221.84, 121.807, 68.392, 35.611, 19.134, 10.783};
  const std::vector<double> source_lengths = {506.74};
  const auto plan = icts::htree::ResolveCharacterizationGridPlan(config, topology_lengths, source_lengths);
  ASSERT_TRUE(plan.adapted);
  EXPECT_EQ(plan.requested_level_lengths, topology_lengths.size());
  EXPECT_NEAR(plan.wirelength_unit_um, 221.84 / 6.0, (221.84 / 6.0) * kRelTol);
  EXPECT_EQ(plan.required_covering_iterations, 14U);
  EXPECT_EQ(plan.wirelength_iterations, 14U);
  EXPECT_EQ(plan.unique_level_bins, 4U);

  const std::vector<unsigned> expected_requested = {1U, 2U, 4U, 6U};
  EXPECT_EQ(plan.requested_length_indices, expected_requested);
  EXPECT_EQ(icts::htree::ResolveDirectCharacterizationLengthIndices(topology_lengths, plan), (std::vector<unsigned>{1U, 2U, 4U, 8U}));
}

TEST(WirelengthGridTest, SingleTargetAutoModeUsesOneDirectCharacterizationPoint)
{
  icts::CharBuilder::Config config;
  config.wirelength_iterations = 3U;
  const std::vector<double> requests = {2340.174};

  const auto plan = icts::htree::ResolveCharacterizationGridPlan(config, requests);
  const auto indices = icts::htree::ResolveDirectCharacterizationLengthIndices(requests, plan);

  ASSERT_TRUE(plan.adapted);
  EXPECT_NEAR(plan.wirelength_unit_um, requests.front(), requests.front() * kRelTol);
  EXPECT_EQ(plan.required_covering_iterations, 1U);
  EXPECT_EQ(plan.wirelength_iterations, 1U);
  EXPECT_EQ(indices, (std::vector<unsigned>{1U}));
}

TEST(WirelengthGridTest, PrimitiveDirectIndexIsIndependentOfRequestedBins)
{
  icts::CharBuilder::Config config;
  const std::vector<double> requests = {10.0, 30.0, 200.0, 400.0};  // unit 100, bins {1,1,2,4}
  const auto plan = icts::htree::ResolveCharacterizationGridPlan(config, requests);
  ASSERT_TRUE(plan.adapted);
  ASSERT_EQ(plan.required_covering_iterations, 4U);
  ASSERT_EQ(plan.wirelength_iterations, 4U);

  const std::vector<unsigned> expected_requested = {1U, 2U, 4U};
  EXPECT_EQ(plan.requested_length_indices, expected_requested);
  EXPECT_EQ(icts::htree::ResolveDirectCharacterizationLengthIndices(requests, plan), (std::vector<unsigned>{1U, 2U, 4U}));
}

TEST(WirelengthGridTest, PhysicalScaleCanProduceRequestedIndexLargerThanRequestCount)
{
  icts::CharBuilder::Config config;
  const std::vector<double> direct_lengths = {600.0};
  const std::vector<double> coverage_lengths = {900.0};
  const icts::CharacterizationWirelengthUnitLimits limits{
      .physical_scale_unit_um = 37.0,
      .electrical_ceiling_um = 100.0,
  };

  const auto plan = icts::htree::ResolveCharacterizationGridPlan(config, direct_lengths, coverage_lengths, limits);

  ASSERT_TRUE(plan.adapted);
  EXPECT_TRUE(plan.unit_selected_from_physical_scale);
  EXPECT_FALSE(plan.unit_clamped_to_electrical_ceiling);
  EXPECT_DOUBLE_EQ(plan.wirelength_unit_um, 37.0);
  EXPECT_EQ(plan.requested_length_indices, (std::vector<unsigned>{17U}));
  EXPECT_EQ(plan.direct_length_indices, (std::vector<unsigned>{1U, 2U}));
  EXPECT_EQ(plan.required_covering_iterations, 25U);
}

TEST(WirelengthGridTest, PhysicalScaleDoesNotCoarsenShortWireRequests)
{
  icts::CharBuilder::Config config;
  const icts::CharacterizationWirelengthUnitLimits limits{
      .physical_scale_unit_um = 37.0,
      .electrical_ceiling_um = 100.0,
  };

  const auto plan = icts::htree::ResolveCharacterizationGridPlan(config, std::vector<double>{1.0, 2.0}, {}, limits);

  ASSERT_TRUE(plan.adapted);
  EXPECT_FALSE(plan.unit_selected_from_physical_scale);
  EXPECT_DOUBLE_EQ(plan.wirelength_unit_um, 1.0);
  EXPECT_EQ(plan.requested_length_indices, (std::vector<unsigned>{1U, 2U}));
}

TEST(WirelengthGridTest, AdaptedUnitPreservesExplicitCharacterizationIndices)
{
  icts::CharBuilder::Config config;
  config.wirelength_iterations = 9U;
  config.wirelength_indices = std::vector<unsigned>{7U, 2U, 7U, 0U};
  const icts::CharacterizationWirelengthUnitLimits limits{
      .physical_scale_unit_um = 37.0,
      .electrical_ceiling_um = 100.0,
  };

  const auto plan = icts::htree::ResolveCharacterizationGridPlan(config, std::vector<double>{600.0}, {}, limits);

  ASSERT_TRUE(plan.adapted);
  EXPECT_TRUE(plan.preserves_explicit_indices);
  EXPECT_FALSE(plan.uses_primitive_characterization);
  EXPECT_EQ(plan.direct_length_indices, (std::vector<unsigned>{2U, 7U}));
  EXPECT_EQ(icts::htree::ResolveDirectCharacterizationLengthIndices(std::vector<double>{600.0}, plan), (std::vector<unsigned>{2U, 7U}));
}

}  // namespace
}  // namespace icts_test
