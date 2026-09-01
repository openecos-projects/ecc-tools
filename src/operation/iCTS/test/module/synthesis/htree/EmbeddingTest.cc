// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of the License at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
/**
 * @file EmbeddingTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-08-26
 * @brief Unit coverage for selected sink-load-region plan materialization decisions.
 */

#include <gtest/gtest.h>

#include <vector>

#include "Pin.hh"
#include "Point.hh"
#include "synthesis/htree/embedding/Embedding.hh"
#include "synthesis/htree/region/SinkLoadRegion.hh"

namespace icts_test {
namespace {

auto MakeSplitPlan(const icts::Point<int>& anchor, const std::vector<icts::Pin*>& original_loads, bool feasible, bool required)
    -> icts::htree::SinkLoadRegionSplitPlan
{
  icts::htree::SinkLoadRegionSplitPlan plan;
  plan.anchor = anchor;
  plan.original_loads = original_loads;
  plan.feasible = feasible;
  plan.required = required;
  return plan;
}

TEST(EmbeddingTest, InfeasiblePlanFailsClosed)
{
  icts::Pin load("load", icts::PinType::kClock, icts::Point<int>(10, 20));
  const auto plan = MakeSplitPlan(icts::Point<int>(10, 20), std::vector<icts::Pin*>{&load}, false, false);

  EXPECT_EQ(icts::htree::ClassifySplitPlanMaterialization(plan, std::vector<icts::Pin*>{&load}, icts::Point<int>(10, 20)),
            icts::htree::SplitPlanMaterializationDecision::kMismatch);
}

TEST(EmbeddingTest, FeasibleNonRequiredPlanPassesThroughWithoutIdentityMatch)
{
  icts::Pin planned_load("planned_load", icts::PinType::kClock, icts::Point<int>(10, 20));
  icts::Pin terminal_load("terminal_load", icts::PinType::kClock, icts::Point<int>(30, 40));
  const auto plan = MakeSplitPlan(icts::Point<int>(10, 20), std::vector<icts::Pin*>{&planned_load}, true, false);

  EXPECT_EQ(icts::htree::ClassifySplitPlanMaterialization(plan, std::vector<icts::Pin*>{&terminal_load}, icts::Point<int>(90, 100)),
            icts::htree::SplitPlanMaterializationDecision::kPassThrough);
}

TEST(EmbeddingTest, RequiredPlanWithExactIdentityIsMaterialized)
{
  icts::Pin load_a("load_a", icts::PinType::kClock, icts::Point<int>(10, 20));
  icts::Pin load_b("load_b", icts::PinType::kClock, icts::Point<int>(30, 40));
  const icts::Point<int> anchor(50, 60);
  const auto plan = MakeSplitPlan(anchor, std::vector<icts::Pin*>{&load_a, &load_b}, true, true);

  EXPECT_EQ(icts::htree::ClassifySplitPlanMaterialization(plan, std::vector<icts::Pin*>{&load_b, &load_a}, anchor),
            icts::htree::SplitPlanMaterializationDecision::kMaterialize);
}

TEST(EmbeddingTest, RequiredPlanRejectsAnchorOrLoadMismatch)
{
  icts::Pin planned_load("planned_load", icts::PinType::kClock, icts::Point<int>(10, 20));
  icts::Pin other_load("other_load", icts::PinType::kClock, icts::Point<int>(30, 40));
  const icts::Point<int> anchor(50, 60);
  const auto plan = MakeSplitPlan(anchor, std::vector<icts::Pin*>{&planned_load}, true, true);

  EXPECT_EQ(icts::htree::ClassifySplitPlanMaterialization(plan, std::vector<icts::Pin*>{&planned_load}, icts::Point<int>(51, 60)),
            icts::htree::SplitPlanMaterializationDecision::kMismatch);
  EXPECT_EQ(icts::htree::ClassifySplitPlanMaterialization(plan, std::vector<icts::Pin*>{&other_load}, anchor),
            icts::htree::SplitPlanMaterializationDecision::kMismatch);
}

}  // namespace
}  // namespace icts_test
