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
 * @file FastSTAIncrementalTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-09-13
 * @brief Incremental timing equivalence, batch rollback and scaling regressions.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "FastSTAIncremental.hh"
#include "FastSTAPower.hh"
#include "FastSTATiming.hh"
#include "fixture/FastSTATestModel.hh"

namespace icts_test {
namespace {

auto MakeScaleContext(std::size_t node_count) -> icts::FastStaContext
{
  icts::FastStaContext context;
  context.clock_name = "scale_clk";
  context.clock_net_name = "scale_source_net";
  context.clock_period_ns = 10.0;
  context.root_input_slew_ns = 0.1;
  context.liberty_cell_by_master["BUF_X1"] = MakeCell("BUF_X1", 0.20, 1.5, 0.01);
  context.liberty_cell_by_master["BUF_X2"] = MakeCell("BUF_X2", 0.40, 2.5, 0.02);

  const auto buffer_count = (node_count - 2U) / 2U;
  const auto buffer_input_id = [](std::size_t buffer_id) -> icts::FastStaNodeId { return 1U + 2U * buffer_id; };
  const auto buffer_output_id = [](std::size_t buffer_id) -> icts::FastStaNodeId { return 2U + 2U * buffer_id; };
  context.nodes.reserve(node_count);
  context.nets.reserve(buffer_count + 1U);
  context.node_id_by_name.reserve(node_count);
  context.buffer_input_node_id_by_inst.reserve(buffer_count);
  context.buffer_output_node_id_by_inst.reserve(buffer_count);
  context.net_id_by_name.reserve(buffer_count + 1U);

  context.source_node_id = 0U;
  context.nodes.push_back(MakeNode(icts::FastStaNodeKind::kSource, "scale_source", "", "scale_source", "", {}, 0.0, icts::kInvalidFastStaNetId, {0U}));
  context.node_id_by_name.emplace("scale_source", 0U);
  for (std::size_t buffer_id = 0U; buffer_id < buffer_count; ++buffer_id) {
    const auto inst_name = "scale_buf_" + std::to_string(buffer_id);
    const auto input_name = inst_name + "/A";
    const auto output_name = inst_name + "/Y";
    const auto input_id = buffer_input_id(buffer_id);
    const auto output_id = buffer_output_id(buffer_id);
    const auto incoming_net_id = buffer_id == 0U ? 0U : (buffer_id - 1U) / 2U + 1U;
    context.nodes.push_back(MakeNode(icts::FastStaNodeKind::kBufferInput, input_name, inst_name, "A", "BUF_X1", {}, 0.20, incoming_net_id, {}));
    context.nodes.push_back(
        MakeNode(icts::FastStaNodeKind::kBufferOutput, output_name, inst_name, "Y", "BUF_X1", {}, 0.0, icts::kInvalidFastStaNetId, {buffer_id + 1U}));
    context.node_id_by_name.emplace(input_name, input_id);
    context.node_id_by_name.emplace(output_name, output_id);
    context.buffer_input_node_id_by_inst.emplace(inst_name, input_id);
    context.buffer_output_node_id_by_inst.emplace(inst_name, output_id);
  }

  const auto sink_node_id = context.nodes.size();
  context.nodes.push_back(MakeNode(icts::FastStaNodeKind::kSink, "scale_sink/CLK", "scale_sink", "CLK", "", {}, 0.10, buffer_count, {}));
  context.node_id_by_name.emplace("scale_sink/CLK", sink_node_id);

  context.nets.push_back(MakeNet("scale_source_net", 0U, {buffer_input_id(0U)}, 3.0));
  context.net_id_by_name.emplace("scale_source_net", 0U);
  for (std::size_t buffer_id = 0U; buffer_id < buffer_count; ++buffer_id) {
    std::vector<icts::FastStaNodeId> load_node_ids;
    const auto left_child = 2U * buffer_id + 1U;
    const auto right_child = left_child + 1U;
    if (left_child < buffer_count) {
      load_node_ids.push_back(buffer_input_id(left_child));
    }
    if (right_child < buffer_count) {
      load_node_ids.push_back(buffer_input_id(right_child));
    }
    if (buffer_id + 1U == buffer_count) {
      load_node_ids.push_back(sink_node_id);
    }
    const auto net_name = "scale_net_" + std::to_string(buffer_id);
    context.nets.push_back(MakeNet(net_name, buffer_output_id(buffer_id), std::move(load_node_ids), 3.0));
    context.net_id_by_name.emplace(net_name, buffer_id + 1U);
  }
  return context;
}

auto MakeScaleChanges(std::size_t node_count) -> std::vector<icts::FastStaBufferMasterChange>
{
  const auto buffer_count = (node_count - 2U) / 2U;
  const auto parent_buffer_id = buffer_count / 2U - 1U;
  const auto left_child = 2U * parent_buffer_id + 1U;
  const auto right_child = left_child + 1U;
  return {
      {.node_id = 1U + 2U * left_child, .cell_master = "BUF_X2"},
      {.node_id = 1U + 2U * right_child, .cell_master = "BUF_X2"},
  };
}

auto MeasureScaleRoutes(const icts::FastStaContext& baseline_context, const std::vector<icts::FastStaBufferMasterChange>& changes, double& full_replay_us,
                        double& incremental_replay_us) -> bool
{
  auto full_context = baseline_context;
  const auto full_start = std::chrono::steady_clock::now();
  const auto full_ok = icts::FastStaIncremental::changeBufferMasters(full_context, changes) && icts::FastStaTiming::update(full_context);
  const auto full_finish = std::chrono::steady_clock::now();

  auto incremental_context = baseline_context;
  const auto incremental_start = std::chrono::steady_clock::now();
  const auto dirty_region = icts::FastStaIncremental::changeBufferMastersIncremental(incremental_context, changes);
  const auto incremental_ok = dirty_region.has_value() && icts::FastStaTiming::updateRegion(incremental_context, dirty_region.value());
  const auto incremental_finish = std::chrono::steady_clock::now();

  full_replay_us = std::chrono::duration<double, std::micro>(full_finish - full_start).count();
  incremental_replay_us = std::chrono::duration<double, std::micro>(incremental_finish - incremental_start).count();
  return full_ok && incremental_ok && TimingStatesMatch(full_context, incremental_context);
}

auto Median(std::vector<double> samples) -> double
{
  std::ranges::sort(samples);
  return samples.at(samples.size() / 2U);
}

TEST(FastSTATest, IncrementalMasterChangeMatchesFullRecompute)
{
  auto incremental_context = MakeTwoLevelContext();
  ASSERT_TRUE(icts::FastStaTiming::update(incremental_context));
  ASSERT_TRUE(icts::FastStaPower::update(incremental_context));

  auto full_context = incremental_context;

  const auto dirty_region_opt = icts::FastStaIncremental::changeBufferMasterIncremental(incremental_context, 3U, "BUF_X2");
  if (!dirty_region_opt.has_value()) {
    ADD_FAILURE() << "Expected incremental dirty region.";
    return;
  }
  const auto& dirty_region = *dirty_region_opt;
  ASSERT_TRUE(dirty_region.valid);
  EXPECT_EQ(dirty_region.start_node_id, 1U);
  EXPECT_FALSE(dirty_region.net_ids.empty());

  ASSERT_TRUE(icts::FastStaTiming::updateRegion(incremental_context, dirty_region));
  ASSERT_TRUE(icts::FastStaPower::updateRegion(incremental_context, dirty_region));

  ASSERT_TRUE(icts::FastStaIncremental::changeBufferMaster(full_context, 3U, "BUF_X2"));
  ASSERT_TRUE(icts::FastStaTiming::update(full_context));
  ASSERT_TRUE(icts::FastStaPower::update(full_context));

  ASSERT_TRUE(incremental_context.skew.valid);
  ASSERT_TRUE(full_context.skew.valid);
  EXPECT_NEAR(incremental_context.nodes.at(3U).input_cap_pf, full_context.nodes.at(3U).input_cap_pf, 1e-12);
  EXPECT_NEAR(incremental_context.nets.at(1U).load_cap_pf, full_context.nets.at(1U).load_cap_pf, 1e-12);
  EXPECT_NEAR(incremental_context.nodes.at(5U).timing.arrival_ns, full_context.nodes.at(5U).timing.arrival_ns, 1e-12);
  EXPECT_NEAR(incremental_context.nodes.at(5U).timing.slew_ns, full_context.nodes.at(5U).timing.slew_ns, 1e-12);
  EXPECT_NEAR(incremental_context.power.switching_power_w, full_context.power.switching_power_w, 1e-18);
  EXPECT_NEAR(incremental_context.power.internal_power_w, full_context.power.internal_power_w, 1e-18);
  EXPECT_NEAR(incremental_context.power.leakage_power_w, full_context.power.leakage_power_w, 1e-18);
  EXPECT_NEAR(incremental_context.power.area_um2, full_context.power.area_um2, 1e-12);
}

TEST(FastSTATest, BatchIncrementalMasterChangeAndRestoreMatchFullRecompute)
{
  auto original_context = MakeTwoLevelContext();
  ASSERT_TRUE(icts::FastStaTiming::update(original_context));
  auto incremental_context = original_context;
  auto full_context = original_context;
  const std::vector<icts::FastStaBufferMasterChange> changes{
      {.node_id = 1U, .cell_master = "BUF_X2"},
      {.node_id = 3U, .cell_master = "BUF_X2"},
  };

  ASSERT_TRUE(icts::FastStaIncremental::validateBufferMasterChanges(incremental_context, changes));
  const auto changed_region = icts::FastStaIncremental::changeBufferMastersIncremental(incremental_context, changes);
  if (!changed_region.has_value()) {
    ADD_FAILURE() << "Expected a dirty region for the validated buffer-master batch.";
    return;
  }
  ASSERT_TRUE(icts::FastStaTiming::updateRegion(incremental_context, *changed_region));
  ASSERT_TRUE(icts::FastStaIncremental::changeBufferMasters(full_context, changes));
  ASSERT_TRUE(icts::FastStaTiming::update(full_context));
  EXPECT_TRUE(TimingStatesMatch(incremental_context, full_context));

  const std::vector<icts::FastStaBufferMasterChange> restore{
      {.node_id = 1U, .cell_master = "BUF_X1"},
      {.node_id = 3U, .cell_master = "BUF_X1"},
  };
  ASSERT_TRUE(icts::FastStaIncremental::validateBufferMasterChanges(incremental_context, restore));
  const auto restored_region = icts::FastStaIncremental::changeBufferMastersIncremental(incremental_context, restore);
  if (!restored_region.has_value()) {
    ADD_FAILURE() << "Expected a dirty region when restoring the original buffer masters.";
    return;
  }
  ASSERT_TRUE(icts::FastStaTiming::updateRegion(incremental_context, *restored_region));
  EXPECT_TRUE(TimingStatesMatch(incremental_context, original_context));
}

TEST(FastSTATest, MissingBufferPairIndexesFailClosedWithoutNameRecovery)
{
  auto context = MakeTwoLevelContext();
  context.buffer_input_node_id_by_inst.clear();
  context.buffer_output_node_id_by_inst.clear();
  std::vector<std::string> original_masters;
  original_masters.reserve(context.nodes.size());
  for (const auto& node : context.nodes) {
    original_masters.push_back(node.cell_master);
  }
  const std::vector<icts::FastStaBufferMasterChange> changes{
      {.node_id = 2U, .cell_master = "BUF_X2"},
      {.node_id = 3U, .cell_master = "BUF_X2"},
  };

  EXPECT_FALSE(icts::FastStaTiming::update(context));
  EXPECT_FALSE(icts::FastStaPower::update(context));
  EXPECT_FALSE(icts::FastStaIncremental::validateBufferMasterChanges(context, changes));
  EXPECT_FALSE(icts::FastStaIncremental::changeBufferMastersIncremental(context, changes).has_value());
  ASSERT_EQ(context.nodes.size(), original_masters.size());
  for (std::size_t node_id = 0U; node_id < context.nodes.size(); ++node_id) {
    EXPECT_EQ(context.nodes.at(node_id).cell_master, original_masters.at(node_id));
  }
}

TEST(FastSTATest, BatchPrevalidationRejectsWholeChangeWithoutMutation)
{
  auto context = MakeTwoLevelContext();
  ASSERT_TRUE(icts::FastStaTiming::update(context));
  const auto original_context = context;
  const std::vector<icts::FastStaBufferMasterChange> changes{
      {.node_id = 1U, .cell_master = "BUF_X2"},
      {.node_id = context.nodes.size(), .cell_master = "BUF_X2"},
  };

  EXPECT_FALSE(icts::FastStaIncremental::validateBufferMasterChanges(context, changes));
  EXPECT_FALSE(icts::FastStaIncremental::changeBufferMastersIncremental(context, changes).has_value());
  EXPECT_TRUE(TimingStatesMatch(context, original_context));
}

TEST(FastSTATest, BatchIncrementalTimingScale)
{
  const auto run_scale = [](std::size_t node_count, std::size_t measured_rounds) -> std::pair<double, double> {
    auto baseline_context = MakeScaleContext(node_count);
    EXPECT_EQ(baseline_context.nodes.size(), node_count);
    EXPECT_TRUE(icts::FastStaTiming::update(baseline_context));
    const auto changes = MakeScaleChanges(node_count);

    double warmup_full_us = 0.0;
    double warmup_incremental_us = 0.0;
    EXPECT_TRUE(MeasureScaleRoutes(baseline_context, changes, warmup_full_us, warmup_incremental_us));

    std::vector<double> full_samples_us;
    std::vector<double> incremental_samples_us;
    full_samples_us.reserve(measured_rounds);
    incremental_samples_us.reserve(measured_rounds);
    for (std::size_t round = 0U; round < measured_rounds; ++round) {
      double full_replay_us = 0.0;
      double incremental_replay_us = 0.0;
      EXPECT_TRUE(MeasureScaleRoutes(baseline_context, changes, full_replay_us, incremental_replay_us)) << "node_count=" << node_count << " round=" << round;
      full_samples_us.push_back(full_replay_us);
      incremental_samples_us.push_back(incremental_replay_us);
    }

    const auto full_median_us = Median(full_samples_us);
    const auto incremental_median_us = Median(incremental_samples_us);
    std::cout << "FASTSTA_SCALE node_count=" << node_count << " full_us=";
    for (const auto sample : full_samples_us) {
      std::cout << sample << ',';
    }
    std::cout << " incremental_us=";
    for (const auto sample : incremental_samples_us) {
      std::cout << sample << ',';
    }
    std::cout << " full_median_us=" << full_median_us << " incremental_median_us=" << incremental_median_us
              << " ratio=" << incremental_median_us / full_median_us << '\n';
    return {full_median_us, incremental_median_us};
  };

  const auto [full_10k_us, incremental_10k_us] = run_scale(10'000U, 5U);
  EXPECT_GT(full_10k_us, 0.0);
  EXPECT_GT(incremental_10k_us, 0.0);
  const auto [full_100k_us, incremental_100k_us] = run_scale(100'000U, 3U);
  EXPECT_GT(full_100k_us, 0.0);
  EXPECT_GT(incremental_100k_us, 0.0);
  EXPECT_LE(incremental_100k_us, full_100k_us * 0.80);
}

}  // namespace
}  // namespace icts_test
