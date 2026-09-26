// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// ***************************************************************************************

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "characterization/builder/CharTopologyPlanner.hh"
#include "characterization/pattern/CharPatternEnumerator.hh"

namespace icts_test {
namespace {

using icts::char_builder::detail::BuildTopologyDesc;
using icts::char_builder::detail::CharPatternEnumerator;
using icts::char_builder::detail::TopologySlotSelection;

TEST(CharPatternEnumeratorTest, EstimateMatchesSmallExhaustiveCandidateSpace)
{
  EXPECT_EQ(CharPatternEnumerator::estimatePatternCount(0U, 2U), 1U);
  EXPECT_EQ(CharPatternEnumerator::estimatePatternCount(1U, 2U), 3U);
  EXPECT_EQ(CharPatternEnumerator::estimatePatternCount(2U, 2U), 8U);
  EXPECT_EQ(CharPatternEnumerator::estimatePatternCount(3U, 2U), 20U);
  EXPECT_EQ(CharPatternEnumerator::estimatePatternCount(3U, 0U), 1U);
}

TEST(CharPatternEnumeratorTest, TopologyDescriptionDistinguishesWireLeafAndBranch)
{
  const auto wire = BuildTopologyDesc(30.0, 10.0, TopologySlotSelection{0U, 0U, 0U});
  EXPECT_FALSE(wire.has_terminal_branch_buffer);
  EXPECT_TRUE(wire.buffer_positions.empty());
  EXPECT_EQ(wire.wire_segments_um, (std::vector<double>{30.0}));

  const auto leaf = BuildTopologyDesc(30.0, 10.0, TopologySlotSelection{0U, 1U, 0U});
  EXPECT_FALSE(leaf.has_terminal_branch_buffer);
  EXPECT_EQ(leaf.buffer_positions, (std::vector<std::size_t>{1U}));
  EXPECT_EQ(leaf.wire_segments_um, (std::vector<double>{20.0, 10.0}));

  const auto branch = BuildTopologyDesc(30.0, 10.0, TopologySlotSelection{1U, 0U, 1U});
  EXPECT_TRUE(branch.has_terminal_branch_buffer);
  EXPECT_EQ(branch.buffer_positions, (std::vector<std::size_t>{0U, 2U}));
  EXPECT_EQ(branch.wire_segments_um, (std::vector<double>{10.0, 20.0, 0.0}));
}

TEST(CharPatternEnumeratorTest, TerminalSlotUsesCoveredPhysicalBoundary)
{
  const auto branch = BuildTopologyDesc(25.0, 10.0, TopologySlotSelection{0U, 0U, 1U});

  EXPECT_TRUE(branch.has_terminal_branch_buffer);
  EXPECT_EQ(branch.buffer_positions.size(), 1U);
  EXPECT_EQ(branch.wire_segments_um, (std::vector<double>{25.0, 0.0}));
}

}  // namespace
}  // namespace icts_test
