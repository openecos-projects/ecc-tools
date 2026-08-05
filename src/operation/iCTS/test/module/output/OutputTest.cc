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
 * @file OutputTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-07-30
 * @brief Verifies that output consumes only committed evaluation state.
 */

#include <gtest/gtest.h>

#include "data_manager/DataManager.hh"
#include "module/evaluation/qor/QOREvaluation.hh"
#include "module/output/Output.hh"

namespace icts_test {
namespace {

class OutputTestInterface : public ::testing::Test
{
 protected:
  void SetUp() override { CTSDM.reset(); }
  void TearDown() override { CTSDM.reset(); }
};

TEST_F(OutputTestInterface, MissingCommittedEvaluationFailsWithoutRunningEvaluation)
{
  ASSERT_EQ(CTSDM.getState(), icts::CTSRunState::kEmpty);
  ASSERT_FALSE(CTSDM.getEvaluationState().summary.has_evaluation_result);
  ASSERT_FALSE(CTSDM.getEvaluationState().summary.final_buffer_area_um2.has_value());

  const auto summary = icts::Output::run("");

  EXPECT_FALSE(summary.success);
  EXPECT_FALSE(summary.evaluation_ready);
  EXPECT_EQ(CTSDM.getState(), icts::CTSRunState::kEmpty);
  EXPECT_FALSE(CTSDM.getEvaluationState().summary.has_evaluation_result);
  EXPECT_FALSE(CTSDM.getEvaluationState().summary.final_buffer_area_um2.has_value());
}

TEST_F(OutputTestInterface, ReadyShapedButUncommittedEvaluationIsRejectedByDataManagerContract)
{
  icts::EvaluationState ready_shaped_state;
  ready_shaped_state.summary.has_evaluation_result = true;
  ready_shaped_state.statistics.valid = true;

  ASSERT_TRUE(icts::QorEvaluation::isEvaluationReady(ready_shaped_state));
  const auto commit_status = CTSDM.commitEvaluation(std::move(ready_shaped_state));

  EXPECT_EQ(commit_status.code, icts::DataManagerStatusCode::kInvalidState);
  EXPECT_FALSE(CTSDM.hasCommittedEvaluation());
  const auto output_summary = icts::Output::run("");
  EXPECT_FALSE(output_summary.success);
  EXPECT_FALSE(output_summary.evaluation_ready);
}

}  // namespace
}  // namespace icts_test
