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
 * @file DataManagerTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-07-30
 * @brief Tests for process-wide CTS state ownership and reset behavior.
 */

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <string>

#include "data_manager/DataManager.hh"
#include "module/synthesis/Synthesis.hh"

namespace icts_test {
namespace {

auto MakeUniqueOutputDir() -> std::filesystem::path
{
  const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() / ("icts_data_manager_" + std::to_string(suffix));
}

TEST(DataManagerTest, OwnsOneProcessWideCTSState)
{
  auto* design_inst = CTSDM.getDesign().makeInst("session_one_buffer");
  ASSERT_NE(design_inst, nullptr);
  EXPECT_EQ(CTSDM.getDesign().findInst("session_one_buffer"), design_inst);

  icts::DataManager::destroyInst();
  icts::DataManager::initInst();

  EXPECT_EQ(CTSDM.getState(), icts::CTSRunState::kEmpty);
  EXPECT_EQ(CTSDM.getDesign().findInst("session_one_buffer"), nullptr);
  EXPECT_TRUE(CTSDM.getDesign().get_insts().empty());
}

TEST(DataManagerTest, ResetClearsAllStageOwnedState)
{
  ASSERT_NE(CTSDM.getDesign().makeInst("temporary_buffer"), nullptr);

  CTSDM.reset();

  EXPECT_EQ(CTSDM.getState(), icts::CTSRunState::kEmpty);
  EXPECT_TRUE(CTSDM.getDesign().get_insts().empty());
  EXPECT_FALSE(CTSDM.getEvaluationState().summary.has_evaluation_result);
  EXPECT_FALSE(CTSDM.getClockLayout().isInstantiationDone());
}

TEST(DataManagerTest, ReadyShapedEvaluationIsNotCommittedWithoutCommittedRunState)
{
  icts::EvaluationState ready_shaped_state;
  ready_shaped_state.summary.has_evaluation_result = true;
  ready_shaped_state.statistics.valid = true;

  const auto status = CTSDM.commitEvaluation(std::move(ready_shaped_state));

  EXPECT_EQ(status.code, icts::DataManagerStatusCode::kInvalidState);
  EXPECT_FALSE(CTSDM.hasCommittedEvaluation());
  EXPECT_EQ(CTSDM.getCommittedEvaluationState(), nullptr);
}

TEST(DataManagerTest, RejectsOutOfOrderStageCommitWithoutMutatingDesign)
{
  ASSERT_NE(CTSDM.getDesign().makeInst("committed_buffer"), nullptr);
  auto candidate = CTSDM.cloneDesign();
  ASSERT_NE(candidate, nullptr);
  ASSERT_NE(candidate->makeInst("candidate_buffer"), nullptr);

  icts::SynthesisTraceSummary candidate_summary;
  candidate_summary.success = true;
  candidate_summary.outcome = icts::SynthesisOutcome::kFinished;
  icts::DataManagerStatus status;
  const auto returned_summary = icts::CommitSynthesisCandidate(CTSDM, std::move(candidate), icts::ClockLayout{}, std::move(candidate_summary), status);

  EXPECT_EQ(status.code, icts::DataManagerStatusCode::kInvalidState);
  EXPECT_FALSE(returned_summary.success);
  EXPECT_EQ(returned_summary.outcome, icts::SynthesisOutcome::kFailed);
  EXPECT_EQ(returned_summary.commit_status, "rejected");
  EXPECT_EQ(returned_summary.failure_reason, status.message);
  EXPECT_EQ(CTSDM.getState(), icts::CTSRunState::kEmpty);
  EXPECT_NE(CTSDM.getDesign().findInst("committed_buffer"), nullptr);
  EXPECT_EQ(CTSDM.getDesign().findInst("candidate_buffer"), nullptr);
  EXPECT_EQ(CTSDM.getSynthesisSummary().commit_status, "not_attempted");
}

TEST(DataManagerTest, DerivesSingleLogPathFromWorkDirectory)
{
  const auto output_dir = MakeUniqueOutputDir();
  const auto missing_config = output_dir / "missing_config.json";

  const auto status = CTSDM.input(icts::DataManagerInput{.config_file = missing_config.string(), .work_dir = output_dir.string()});

  EXPECT_EQ(status.code, icts::DataManagerStatusCode::kConfigError);
  EXPECT_EQ(CTSDM.getState(), icts::CTSRunState::kFailed);
  EXPECT_EQ(CTSDM.getLogPath(), (output_dir / "cts.log").string());
  EXPECT_TRUE(std::filesystem::exists(output_dir / "cts.log"));
  std::size_t regular_file_count = 0U;
  for (const auto& entry : std::filesystem::directory_iterator(output_dir)) {
    regular_file_count += entry.is_regular_file() ? 1U : 0U;
  }
  EXPECT_EQ(regular_file_count, 1U);

  std::error_code error_code;
  std::filesystem::remove_all(output_dir, error_code);
}

}  // namespace
}  // namespace icts_test
