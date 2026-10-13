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
 * @file FastSTACharTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-09-13
 * @brief Characterization preparation, sample dependencies and handle invalidation.
 */

#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <utility>

#include "FastSTA.hh"
#include "IdbLayout.h"
#include "idm.h"
#include "io/Wrapper.hh"

namespace icts_test {
namespace {

auto ExpectSameSample(const icts::FastStaCharSampleResult& actual, const icts::FastStaCharSampleResult& expected) -> void
{
  ASSERT_TRUE(actual.valid);
  ASSERT_TRUE(expected.valid);
  EXPECT_DOUBLE_EQ(actual.delay_ns, expected.delay_ns);
  EXPECT_DOUBLE_EQ(actual.output_slew_ns, expected.output_slew_ns);
  EXPECT_DOUBLE_EQ(actual.power_w, expected.power_w);
  EXPECT_DOUBLE_EQ(actual.source_boundary_net_switch_power_w, expected.source_boundary_net_switch_power_w);
}

class FastSTACharTestInterface : public ::testing::Test
{
 protected:
  void SetUp() override
  {
    _liberty_path = (std::filesystem::path(__FILE__).parent_path() / "FastSTAChar.lib").string();
    dmInst->get_config().set_lib_paths({_liberty_path});
    ASSERT_TRUE(dmInst->readLib({_liberty_path}));
    _layout.get_units()->set_microns_dbu(1000);
    auto* master = _layout.get_cell_master_list()->set_cell_master("CHARBUF");
    master->set_width(1000U);
    master->set_height(1000U);
    auto* layer = dynamic_cast<idb::IdbLayerRouting*>(_layout.get_layers()->set_layer("M1", "ROUTING"));
    ASSERT_NE(layer, nullptr);
    layer->set_width(100);
    layer->set_resistance(0.1);
    layer->set_capacitance(0.0001);
    layer->set_edge_capacitance(0.00002);
    _layout.get_layers()->add_routing_layer(layer);
    _wrapper.set_idb_layout(&_layout);
    _spec = {.wrapper = &_wrapper,
             .source_cell_master = "CHARBUF",
             .sink_cell_master = "CHARBUF",
             .buffer_cell_masters = {"CHARBUF", "CHARBUF"},
             .wire_segments_um = {2.0, 3.0, 4.0},
             .dbu_per_um = 1000,
             .routing_layer = 1,
             .wire_width_um = std::nullopt,
             .clock_period_ns = 10.0,
             .root_input_slew_ns = 0.03};
  }

  std::string _liberty_path;
  idb::IdbLayout _layout;
  icts::Wrapper _wrapper;
  icts::FastSTA _fast_sta;
  icts::FastStaCharTopologySpec _spec;
};

TEST_F(FastSTACharTestInterface, WarmLoadAndSlewSamplesMatchFreshContexts)
{
  const auto warm = _fast_sta.buildCharContext(_spec);
  if (!warm.context_id.has_value()) {
    FAIL() << warm.failure_reason;
  }
  const std::array<std::pair<double, double>, 4U> samples{{{0.01, 0.03}, {0.01, 0.07}, {0.2, 0.07}, {0.01, 0.03}}};
  std::optional<double> previous_load;
  std::optional<icts::FastStaCharSampleResult> first;
  for (const auto& [load_pf, slew_ns] : samples) {
    if (!previous_load.has_value() || *previous_load != load_pf) {
      ASSERT_TRUE(_fast_sta.setCharLoad(*warm.context_id, load_pf));
      previous_load = load_pf;
    }
    const auto actual = _fast_sta.runCharSample(*warm.context_id, slew_ns);
    const auto cold = _fast_sta.buildCharContext(_spec);
    if (!cold.context_id.has_value()) {
      FAIL() << cold.failure_reason;
    }
    ASSERT_TRUE(_fast_sta.setCharLoad(*cold.context_id, load_pf));
    ExpectSameSample(actual, _fast_sta.runCharSample(*cold.context_id, slew_ns));
    ASSERT_TRUE(_fast_sta.eraseCharContext(*cold.context_id));
    if (!first.has_value()) {
      first = actual;
    }
    if (load_pf != samples.front().first) {
      EXPECT_NE(actual.delay_ns, first->delay_ns);
      EXPECT_NE(actual.power_w, first->power_w);
    }
  }
  if (!first.has_value()) {
    FAIL() << "Expected at least one characterization sample.";
  }
  ExpectSameSample(_fast_sta.runCharSample(*warm.context_id, samples.front().second), *first);
}

TEST_F(FastSTACharTestInterface, InvalidSampleInputsPreserveThePreparedContext)
{
  const auto context = _fast_sta.buildCharContext(_spec);
  if (!context.context_id.has_value()) {
    FAIL() << context.failure_reason;
  }
  ASSERT_TRUE(_fast_sta.setCharLoad(*context.context_id, 0.1));
  const auto expected = _fast_sta.runCharSample(*context.context_id, 0.03);
  ASSERT_TRUE(expected.valid);
  EXPECT_FALSE(_fast_sta.setCharLoad(*context.context_id, -0.1));
  EXPECT_FALSE(_fast_sta.setCharLoad(*context.context_id, std::numeric_limits<double>::infinity()));
  EXPECT_FALSE(_fast_sta.runCharSample(*context.context_id, -0.1).valid);
  EXPECT_FALSE(_fast_sta.runCharSample(*context.context_id, std::numeric_limits<double>::quiet_NaN()).valid);
  ExpectSameSample(_fast_sta.runCharSample(*context.context_id, 0.03), expected);
}

TEST_F(FastSTACharTestInterface, FailedLoadReductionRejectsSamplingAndAllowsValidRetry)
{
  const auto context = _fast_sta.buildCharContext(_spec);
  if (!context.context_id.has_value()) {
    FAIL() << context.failure_reason;
  }
  ASSERT_TRUE(_fast_sta.setCharLoad(*context.context_id, 0.1));
  const auto expected = _fast_sta.runCharSample(*context.context_id, 0.03);
  ASSERT_TRUE(expected.valid);
  // This finite load overflows the RC moments; stale reductions must not be
  // sampled, and the next valid load must prepare its own RC state.
  EXPECT_FALSE(_fast_sta.setCharLoad(*context.context_id, std::numeric_limits<double>::max()));
  EXPECT_FALSE(_fast_sta.runCharSample(*context.context_id, 0.03).valid);
  ASSERT_TRUE(_fast_sta.setCharLoad(*context.context_id, 0.1));
  ExpectSameSample(_fast_sta.runCharSample(*context.context_id, 0.03), expected);
}

TEST_F(FastSTACharTestInterface, RebuiltTopologyAndClockPeriodUseTheirOwnDependencies)
{
  const auto original = _fast_sta.buildCharContext(_spec);
  if (!original.context_id.has_value()) {
    FAIL() << original.failure_reason;
  }
  ASSERT_TRUE(_fast_sta.setCharLoad(*original.context_id, 0.1));
  const auto original_sample = _fast_sta.runCharSample(*original.context_id, 0.03);
  ASSERT_TRUE(original_sample.valid);

  auto changed_spec = _spec;
  changed_spec.clock_period_ns = 5.0;
  const auto changed_period = _fast_sta.buildCharContext(changed_spec);
  if (!changed_period.context_id.has_value()) {
    FAIL() << changed_period.failure_reason;
  }
  ASSERT_TRUE(_fast_sta.setCharLoad(*changed_period.context_id, 0.1));
  const auto period_sample = _fast_sta.runCharSample(*changed_period.context_id, 0.03);
  ASSERT_TRUE(period_sample.valid);
  EXPECT_DOUBLE_EQ(period_sample.delay_ns, original_sample.delay_ns);
  EXPECT_DOUBLE_EQ(period_sample.output_slew_ns, original_sample.output_slew_ns);
  EXPECT_DOUBLE_EQ(period_sample.source_boundary_net_switch_power_w, 2.0 * original_sample.source_boundary_net_switch_power_w);
  EXPECT_GT(period_sample.power_w, original_sample.power_w);

  changed_spec.wire_segments_um = {20.0, 30.0, 40.0};
  const auto changed_route = _fast_sta.buildCharContext(changed_spec);
  if (!changed_route.context_id.has_value()) {
    FAIL() << changed_route.failure_reason;
  }
  ASSERT_TRUE(_fast_sta.setCharLoad(*changed_route.context_id, 0.1));
  const auto route_sample = _fast_sta.runCharSample(*changed_route.context_id, 0.03);
  ASSERT_TRUE(route_sample.valid);
  EXPECT_NE(route_sample.delay_ns, period_sample.delay_ns);
  ExpectSameSample(_fast_sta.runCharSample(*original.context_id, 0.03), original_sample);
}

TEST_F(FastSTACharTestInterface, ReloadAndResetInvalidateWarmHandles)
{
  const auto original = _fast_sta.buildCharContext(_spec);
  if (!original.context_id.has_value()) {
    FAIL() << original.failure_reason;
  }
  ASSERT_TRUE(_fast_sta.setCharLoad(*original.context_id, 0.1));
  const auto expected = _fast_sta.runCharSample(*original.context_id, 0.03);
  ASSERT_TRUE(expected.valid);
  ASSERT_TRUE(dmInst->readLib({_liberty_path}));
  EXPECT_FALSE(_fast_sta.setCharLoad(*original.context_id, 0.1));
  EXPECT_FALSE(_fast_sta.runCharSample(*original.context_id, 0.03).valid);

  const auto reloaded = _fast_sta.buildCharContext(_spec);
  if (!reloaded.context_id.has_value()) {
    FAIL() << reloaded.failure_reason;
  }
  ASSERT_TRUE(_fast_sta.setCharLoad(*reloaded.context_id, 0.1));
  ExpectSameSample(_fast_sta.runCharSample(*reloaded.context_id, 0.03), expected);
  _fast_sta.reset();
  const auto after_reset = _fast_sta.buildCharContext(_spec);
  if (!after_reset.context_id.has_value()) {
    FAIL() << after_reset.failure_reason;
  }
  EXPECT_NE(after_reset.context_id, reloaded.context_id);
  EXPECT_FALSE(_fast_sta.setCharLoad(*reloaded.context_id, 0.1));
  EXPECT_FALSE(_fast_sta.runCharSample(*reloaded.context_id, 0.03).valid);
}

TEST_F(FastSTACharTestInterface, InvalidTopologyAndUnavailableModelDoNotPublishContexts)
{
  auto invalid_spec = _spec;
  invalid_spec.wire_segments_um.clear();
  EXPECT_FALSE(_fast_sta.buildCharContext(invalid_spec).ok());

  invalid_spec = _spec;
  invalid_spec.source_cell_master = "UNAVAILABLE";
  EXPECT_FALSE(_fast_sta.buildCharContext(invalid_spec).ok());
}

}  // namespace
}  // namespace icts_test
