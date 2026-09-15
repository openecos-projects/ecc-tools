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
 * @file UtilityTest.cc
 * @author Dawn Li (dawnli619215645@gmail.com)
 * @date 2026-08-10
 * @brief Tests for CTS process-memory sampling and allocator release evidence.
 */

#include <gtest/gtest.h>

#include "Utility.hh"

namespace icts_test {
namespace {

TEST(UtilityTest, CurrentRssReportsPlatformAvailability)
{
  const auto rss_mb = icts::Utility::currentRssMb();
#ifdef __linux__
  ASSERT_TRUE(rss_mb.has_value());
  EXPECT_GT(rss_mb.value_or(0.0), 0.0);
#else
  EXPECT_FALSE(rss_mb.has_value());
#endif
}

TEST(UtilityTest, ReleaseMemoryReportsEvidenceWithoutRequiringRssReduction)
{
  const auto stats = icts::Utility::releaseMemory();
#if defined(__linux__) && defined(__GLIBC__)
  EXPECT_TRUE(stats.supported);
  EXPECT_GT(stats.rss_before_mb, 0.0);
  EXPECT_GT(stats.rss_after_mb, 0.0);
#else
  EXPECT_FALSE(stats.supported);
  EXPECT_DOUBLE_EQ(stats.rss_before_mb, 0.0);
  EXPECT_DOUBLE_EQ(stats.rss_after_mb, 0.0);
#endif
}

}  // namespace
}  // namespace icts_test
