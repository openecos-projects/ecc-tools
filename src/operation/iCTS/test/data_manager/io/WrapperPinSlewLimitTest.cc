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
 * @file WrapperPinSlewLimitTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-11-02
 * @brief Precedence of an SDC transition limit against the configured fallback.
 *
 * These cases run without Liberty, so they exercise the precedence a pin takes when it
 * declares no rule of its own. The Liberty branch is pinned by the real-tech
 * characterization suite, which needs the PDK.
 */

#include <initializer_list>
#include <optional>
#include <string>

#include "Wrapper.hh"
#include "config/Config.hh"
#include "design/Pin.hh"
#include "gtest/gtest.h"

namespace icts_test {
namespace {

auto MakePin() -> icts::Pin
{
  return icts::Pin("u1/A", icts::PinType::kIn);
}

TEST(WrapperPinSlewLimitTest, SdcLimitBecomesTheLimitWithoutACellRule)
{
  icts::Wrapper wrapper;
  const auto pin = MakePin();
  const auto limit = wrapper.queryPinSlewLimit({.pin = &pin, .configured_max_sink_tran_ns = 1.5, .sdc_max_transition_ns = 0.4});

  EXPECT_DOUBLE_EQ(limit.value_or(-1.0), 0.4);
}

TEST(WrapperPinSlewLimitTest, SdcLimitOutranksATighterConfiguredValue)
{
  // SDC is the authority: a configured value can no longer narrow it either, which is
  // what makes the precedence predictable instead of a silent minimum.
  icts::Wrapper wrapper;
  const auto pin = MakePin();
  const auto limit = wrapper.queryPinSlewLimit({.pin = &pin, .configured_max_sink_tran_ns = 0.2, .sdc_max_transition_ns = 0.4});

  EXPECT_DOUBLE_EQ(limit.value_or(-1.0), 0.4);
}

TEST(WrapperPinSlewLimitTest, ConfiguredValueRemainsTheFallback)
{
  icts::Wrapper wrapper;
  const auto pin = MakePin();
  const auto limit = wrapper.queryPinSlewLimit({.pin = &pin, .configured_max_sink_tran_ns = 0.9});

  EXPECT_DOUBLE_EQ(limit.value_or(-1.0), 0.9);
}

TEST(WrapperPinSlewLimitTest, UnusableSdcLimitFallsBackToTheConfiguredValue)
{
  icts::Wrapper wrapper;
  const auto pin = MakePin();
  for (const double value : {0.0, -1.0}) {
    const auto limit = wrapper.queryPinSlewLimit({.pin = &pin, .configured_max_sink_tran_ns = 0.9, .sdc_max_transition_ns = value});
    EXPECT_DOUBLE_EQ(limit.value_or(-1.0), 0.9);
  }
}

TEST(WrapperPinSlewLimitTest, NoLimitAtAllIsReportedAsAbsent)
{
  icts::Wrapper wrapper;
  const auto pin = MakePin();
  EXPECT_FALSE(wrapper.queryPinSlewLimit({.pin = &pin}).has_value());
}

TEST(WrapperPinSlewLimitTest, ConfigurationProvenanceReachesTheQuery)
{
  icts::Wrapper wrapper;
  const auto pin = MakePin();

  icts::Config configured;
  configured.set_max_sink_tran(0.9);
  EXPECT_DOUBLE_EQ(wrapper.queryPinSlewLimit(configured, &pin).value_or(-1.0), 0.9);

  icts::Config from_sdc;
  from_sdc.set_max_sink_tran(0.9);
  from_sdc.set_sdc_max_transition_ns(0.3);
  EXPECT_DOUBLE_EQ(wrapper.queryPinSlewLimit(from_sdc, &pin).value_or(-1.0), 0.3);
}

TEST(WrapperCapLimitTest, SdcLimitBecomesTheLimitWithoutACellRule)
{
  icts::Wrapper wrapper;
  const auto pin = MakePin();
  const auto limit = wrapper.queryClockSourceDriveCapLimit({.clock_source = &pin, .configured_max_cap_pf = 1.5, .sdc_max_cap_pf = 0.12});

  EXPECT_DOUBLE_EQ(limit.value_or(-1.0), 0.12);
}

TEST(WrapperCapLimitTest, SdcLimitOutranksATighterConfiguredValue)
{
  icts::Wrapper wrapper;
  const auto pin = MakePin();
  const auto limit = wrapper.queryClockSourceDriveCapLimit({.clock_source = &pin, .configured_max_cap_pf = 0.05, .sdc_max_cap_pf = 0.12});

  EXPECT_DOUBLE_EQ(limit.value_or(-1.0), 0.12);
}

TEST(WrapperCapLimitTest, ConfiguredValueRemainsTheFallback)
{
  icts::Wrapper wrapper;
  const auto pin = MakePin();
  EXPECT_DOUBLE_EQ(wrapper.queryClockSourceDriveCapLimit({.clock_source = &pin, .configured_max_cap_pf = 0.9}).value_or(-1.0), 0.9);
}

TEST(WrapperCapLimitTest, ConfigurationProvenanceReachesTheQuery)
{
  icts::Wrapper wrapper;
  const auto pin = MakePin();

  icts::Config configured;
  configured.set_max_cap(0.9);
  EXPECT_DOUBLE_EQ(wrapper.queryClockSourceDriveCapLimit(configured, &pin).value_or(-1.0), 0.9);

  icts::Config from_sdc;
  from_sdc.set_max_cap(0.9);
  from_sdc.set_sdc_max_cap_pf(0.25);
  EXPECT_DOUBLE_EQ(wrapper.queryClockSourceDriveCapLimit(from_sdc, &pin).value_or(-1.0), 0.25);
}

}  // namespace
}  // namespace icts_test
